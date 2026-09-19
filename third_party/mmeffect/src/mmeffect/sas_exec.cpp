// sas_exec.cpp - the SAS script compiler and the runtime technique executor.
//
// Ports:
//   FUN_180018200 [0x180018200] SasCompileScript: normalize + tokenize the
//       script ("^\s*((\w+)\s*=\s*([^=;\s]*))\s*;\s*(.*)", [0x1800B46C8]),
//       dispatch to the flat command list. The command token is compared
//       RAW (memcmp against the lowercase literals - "Clear=Color" warns
//       "unsupported script command"); the only tolower (sub_1800231B0
//       @0x180018d50) hits the VALUE (m[5]), and only for the enum compares
//       (color/depth/geometry/buffer). STANDARDSGLOBAL (FUN_18000c470) is
//       the opposite: it strips ALL whitespace ("\s+" -> "") and lowercases
//       the command (sub_1800233C0 @0x18000d6dc) - two different grammars.
//       "pass=Name" compiles to an id-0 entry (the original's compiler
//       references the token "pass" and the error "Error: unknown pass
//       name: "), "scriptexternal" to id 12, loops stay as runtime ids.
//   FUN_18001b7b0 [0x18001b7b0] SasCreateRunState: save the color targets
//       0..3, the depth stencil and the viewport; stage the host clear color
//       and z = 1.0.
//   FUN_18001bbc0 [0x18001bbc0] the technique command walk - a RUNTIME state
//       machine over the flat list with a persisted command index and a loop
//       stack. Case 0xC (ScriptExternal) in step mode (a4=1) stores the next
//       command index and returns; in full mode (a4=0) it is a no-op. Cases
//       0xD/0xE/0xF are LoopByCount/LoopGetIndex/LoopEnd with runtime count
//       evaluation and the loop stack in the run context. The LABEL_13
//       epilogue restores the redirected targets / depth / viewport.
//   FUN_18001bf80 [0x18001bf80] SasRunPassCommands: one pass entry (its own
//       script, or the default host draw for a script-less pass).
//   FUN_18001c760 [0x18001c760] command applier: render-target switching with
//       the shared redirect bookkeeping, clears with the staged values.
#include "sas_exec.h"
#include "mme_context.h"   // [R8] MmeStageSceneClearRuntime (g_context)
#include "mme_globals.h"    // g_offscreenSurface (qword_1800D9A38)

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cctype>
#include <unordered_map>

// effect_engine.cpp exports this [0x180093660]; the script failure reporter
// uses it exactly like the original's "DirectX Error: <desc> [<hr>]" lines.
namespace mme {
const char* MmeDxErrDescription(unsigned long hr);
}

namespace mme {

// ===========================================================================
// Host callbacks (the RunPass slots draw through; the clear hooks feed the
// staged defaults. Registered once by the pass planner.)
// ===========================================================================

static SasHostCallbacks g_host;

void SasSetHostCallbacks(const SasHostCallbacks& callbacks) {
    g_host = callbacks;
}

// ===========================================================================
// Process-wide depth-target registry [qword_1800D9C48]
// ===========================================================================

// The original's global std::map (red-black tree at qword_1800D9C48). The
// port uses an unordered_map - only point lookups/insert/erase exist, no
// iteration order is observable. Raw pointers, no AddRef - the map merely
// records texture -> surface associations; the OWNER (SasResource) outlives
// every lookup because scripts never run between the resource's release and
// its rebuild.
static std::unordered_map<IDirect3DBaseTexture9*, IDirect3DSurface9*>
    g_depthSurfaceRegistry;

void SasRegisterDepthSurface(IDirect3DBaseTexture9* texture,
                             IDirect3DSurface9* surface) {
    if (texture == nullptr) {
        return;  // [0x1800140ed] the creation-time gate: texture must exist
    }
    // [sub_18001EC90 operator[] + store / sub_180014270 0x180014326]
    // insert-or-overwrite semantics: a device-reset rebuild re-registers the
    // NEW texture key, overwriting any same-key entry.
    g_depthSurfaceRegistry[texture] = surface;
}

void SasUnregisterDepthSurface(IDirect3DBaseTexture9* texture) {
    if (texture == nullptr) {
        return;
    }
    // [sub_18001ED20] map::erase(key) - removes the entry keyed by the
    // record's CURRENT texture at unload.
    g_depthSurfaceRegistry.erase(texture);
}

bool SasFindDepthSurface(IDirect3DBaseTexture9* texture,
                         IDirect3DSurface9** surface) {
    *surface = nullptr;
    if (texture == nullptr) {
        return false;
    }
    // [sub_18001EF30 count + sub_18001EC90 operator[]]: a miss and a hit
    // with a null value are DIFFERENT outcomes (synthetic code 1 vs a quiet
    // NULL bind) - the caller distinguishes them by the return value.
    std::unordered_map<IDirect3DBaseTexture9*, IDirect3DSurface9*>::iterator
        it = g_depthSurfaceRegistry.find(texture);
    if (it == g_depthSurfaceRegistry.end()) {
        return false;
    }
    *surface = it->second;
    return true;
}

// [FUN_18000B210 case 39 -> sub_18001ED20] the record destructor's registry
// erase (see the SasResource declaration note): only a 0x27 record that owns
// a live texture erases; parse-time copies carry null textures by the
// SasResource invariant documented in the header.
SasResource::~SasResource() {
    if (semanticId == 0x27 && texture != nullptr) {
        SasUnregisterDepthSurface(texture);
    }
}

// ===========================================================================
// Logging
// ===========================================================================

void SasLogLine(SasEffect* sas, const char* line) {
    if (sas != nullptr && line != nullptr) {
        sas->log += line;
    }
}

// [FUN_180005760/0x180005830 chain] the original concatenates fixed string
// fragments; this helper produces the identical line text.
void SasLogFormat(SasEffect* sas, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    SasLogLine(sas, buf);
}

// ---------------------------------------------------------------------------
// Port-side observability for the sas+0x39 run-failed latch (divergence,
// additive logging only): the original latches silently, and once latched
// every script walk - including the frame-end epilogue that restores the
// render targets to the back buffer - is skipped forever.  A latched ray
// therefore renders into a suspended offscreen target with a frozen/black
// viewport and NO diagnostic anywhere in MMEffect.txt.  Record the first
// latch cause once per effect load (the flag is one-way until unload, so
// logging only when it is still clear matches the latch's own semantics).
namespace {

const char* SasCommandDisplayName(int id) {
    switch (id) {
        case kSasCmdPass:               return "Pass";
        case kSasCmdRenderColorTarget0: return "RenderColorTarget0";
        case kSasCmdRenderColorTarget1: return "RenderColorTarget1";
        case kSasCmdRenderColorTarget2: return "RenderColorTarget2";
        case kSasCmdRenderColorTarget3: return "RenderColorTarget3";
        case kSasCmdRenderDepthTarget:  return "RenderDepthStencilTarget";
        case kSasCmdClearSetColor:      return "ClearSetColor";
        case kSasCmdClearSetDepth:      return "ClearSetDepth";
        case kSasCmdClear:              return "Clear";
        case kSasCmdClearEx:            return "Clear(extended)";
        case kSasCmdDrawGeometry:       return "Draw=Geometry";
        case kSasCmdDrawBuffer:         return "Draw=Buffer";
        case kSasCmdScriptExternal:     return "ScriptExternal";
        case kSasCmdLoopByCount:        return "LoopByCount";
        case kSasCmdLoopGetIndex:       return "LoopGetIndex";
        case kSasCmdLoopEnd:            return "LoopEnd";
        default:                        return "PassCommands";
    }
}

void SasNoteRunFailureFirstCause(SasEffect* sas, const char* where,
                                 int commandIndex, int commandId,
                                 HRESULT hr) {
    if (sas == nullptr || sas->runFailed) {
        return;
    }
    SasLogFormat(sas,
                 "Error: script run failed, effect disabled (runFailed "
                 "latch): %s, command %d (%s), hr=0x%08X  %s\n",
                 where, commandIndex, SasCommandDisplayName(commandId),
                 static_cast<unsigned int>(hr), sas->path.c_str());
}

} // namespace

// ===========================================================================
// Script normalization [FUN_180018200 L19885-19941]
// ===========================================================================

// append ';' ; regex_replace("\\s*(;\\s*)+", ";") [DAT_1800b46b8] ;
// regex_replace("^;", "") [DAT_1800b3d2c]. The fold only eats whitespace
// ADJACENT to a ';' run - whitespace inside a token or around the '='
// SURVIVES and is consumed by the tokenizer's own \s* anchors ("draw =
// geometry;" is legal), while whitespace inside a value or command word
// fails the token regex (see SasCompileTokenizer). Do NOT copy
// FUN_18000c470's "\s+"-stripping STANDARDSGLOBAL normalization here.
static std::string SasNormalizeScriptExec(const std::string& in) {
    std::string s = in;
    s += ';';
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        size_t j = i;
        while (j < s.size() && isspace(static_cast<unsigned char>(s[j]))) {
            ++j;
        }
        if (j < s.size() && s[j] == ';') {
            // [i, k) matches "\s*(;\s*)+": the whitespace run at i plus the
            // whole following ';'/whitespace run collapses to one ';'.
            size_t k = j;
            while (k < s.size() && (s[k] == ';' ||
                                    isspace(static_cast<unsigned char>(s[k])))) {
                ++k;
            }
            out += ';';
            i = k;
        } else {
            out += s[i];
            ++i;
        }
    }
    if (!out.empty() && out[0] == ';') {
        out = out.substr(1);  // regex_replace("^;", "")
    }
    return out;
}

// ===========================================================================
// FUN_180018200 - the compiler (flat command list, no loop unrolling)
// ===========================================================================

namespace {

struct SasCompiler {
    SasEffect* sas;
    D3DXHANDLE techniqueHandle = nullptr;  // owning technique ("pass=" lookup)
    std::string techniqueName;
    std::string passName;
    bool hasPass;

    std::string ContextSuffix() const {
        // [FUN_180018200 L19741-19743] "(technique: X)" / "(pass: Y, technique: X)"
        if (!hasPass) {
            std::string s = "(technique: ";
            s += techniqueName;
            s += ")";
            return s;
        }
        std::string s = "(pass: ";
        s += passName;
        s += ", technique: ";
        s += techniqueName;
        s += ")";
        return s;
    }
};

// Case-insensitive compare for script enum values ("Color"/"Buffer"/...).
// ray.fx writes them capitalized; the original MME compares case-blind for
// the command values while parameter-name lookups stay case-sensitive.
static bool SasValueEqualsNoCase(const std::string& value, const char* literal) {
    return _stricmp(value.c_str(), literal) == 0;
}

// Type-check a loop count/index parameter. [FUN_180018200 0x180019aea-
// 0x180019b19] accepts Type in {BOOL, INT, FLOAT}, Class in {SCALAR, VECTOR}
// (the `cmp 1 / test` pair on desc+0x10) and Columns == 1 (desc+0x1C), i.e.
// any single-component bool/int/float. Error texts: [0x1800B49B8] "Error:
// type of loop count must be 'int' or 'bool': " / [0x1800B49F0] "...index...".
static bool SasIsLoopParameterType(SasEffect* sas, D3DXHANDLE param) {
    ID3DXEffect* effect = sas->effect;
    D3DXPARAMETER_DESC pd;
    memset(&pd, 0, sizeof(pd));
    if (param == nullptr || effect->GetParameterDesc(param, &pd) != S_OK) {
        return false;
    }
    if (pd.Class != D3DXPC_SCALAR && pd.Class != D3DXPC_VECTOR) {
        return false;
    }
    if (pd.Columns != 1) {
        return false;
    }
    return pd.Type == D3DXPT_BOOL || pd.Type == D3DXPT_INT ||
           pd.Type == D3DXPT_FLOAT;
}

// Evaluate the loop count at RUNTIME. [FUN_18001bbc0 case 0xD 0x18001be16-
// 0x18001be38] zeroes an int, calls ID3DXEffect::GetInt unconditionally (the
// D3DX getter converts BOOL/FLOAT parameters) and skips the loop when the
// signed value is <= 0; a failed read leaves the zero (skip as well).
static bool SasReadLoopParameterNow(ID3DXEffect* effect, D3DXHANDLE param,
                                    int* out) {
    int count = 0;
    if (param == nullptr) {
        return false;
    }
    effect->GetInt(param, &count);  // failure leaves count at 0 like the original
    *out = count;
    return true;
}

// Compile one token into `out`. Mirrors the FUN_180018200 dispatch.
static bool SasCompileToken(SasCompiler* c, const std::string& command,
                            const std::string& value, const std::string& whole,
                            std::vector<SasScriptCmd>* out) {
    ID3DXEffect* effect = c->sas->effect;
    const std::string ctx = c->ContextSuffix();
    SasScriptCmd cmd;
    cmd.text = value;

    // --- render targets [L20341-20380] ---
    int slot = -1;
    bool isColor = false;
    bool isTarget = false;
    if (command == "rendercolortarget" || command == "rendercolortarget0") {
        slot = 0;
        isColor = true;
        isTarget = true;
    } else if (command == "rendercolortarget1") {
        slot = 1;
        isColor = true;
        isTarget = true;
    } else if (command == "rendercolortarget2") {
        slot = 2;
        isColor = true;
        isTarget = true;
    } else if (command == "rendercolortarget3") {
        slot = 3;
        isColor = true;
        isTarget = true;
    } else if (command == "renderdepthstenciltarget") {
        isTarget = true;
    }
    if (isTarget) {
        cmd.id = isColor ? (kSasCmdRenderColorTarget0 + slot) : kSasCmdRenderDepthTarget;
        if (value.empty()) {
            cmd.param = nullptr;  // reset to the saved target
            out->push_back(cmd);
            return true;
        }
        // regex "^(\w+)\[([0-5])\]$" [DAT_1800b47b8]
        std::string name = value;
        int element = -1;
        size_t bracket = value.find('[');
        if (bracket != std::string::npos && value.size() >= bracket + 3 &&
            value[value.size() - 1] == ']') {
            char idx = value[bracket + 1];
            if (idx >= '0' && idx <= '5') {
                name = value.substr(0, bracket);
                element = idx - '0';
            }
        }
        D3DXHANDLE param = effect->GetParameterByName(nullptr, name.c_str());
        if (param == nullptr) {
            // [L20449] "Error: unknown texture name: %s  (ctx)\n"
            SasLogFormat(c->sas, "Error: unknown texture name: %s  %s\n", value.c_str(),
                         ctx.c_str());
            c->sas->hasErrors = true;
            return false;
        }
        D3DXPARAMETER_DESC pd;
        memset(&pd, 0, sizeof(pd));
        effect->GetParameterDesc(param, &pd);
        int t = static_cast<int>(pd.Type);
        const char* want = isColor ? "RENDERCOLORTARGET" : "RENDERDEPTHSTENCILTARGET";
        const char* what = isColor ? "invalid render color target"
                                   : "invalid render depth stencil target";
        // [FUN_180018200 0x18001a6a1-0x18001a6c2] the element-index path
        // ("name[i]") demands a cube-capable parameter type (TEXTURE or
        // TEXTURECUBE); the plain path demands TEXTURE or TEXTURE2D.
        bool typeOk;
        if (element >= 0) {
            typeOk = (t == D3DXPT_TEXTURE || t == D3DXPT_TEXTURECUBE);
        } else {
            typeOk = (t == D3DXPT_TEXTURE || t == D3DXPT_TEXTURE2D);
        }
        if (!typeOk || pd.Semantic == nullptr ||
            _stricmp(pd.Semantic, want) != 0) {
            // [0x1800B4858 / 0x1800B4810]
            SasLogFormat(c->sas, "Error: %s: %s  %s\n", what, value.c_str(),
                         ctx.c_str());
            c->sas->hasErrors = true;
            return false;
        }
        cmd.param = param;
        cmd.value = element;
        out->push_back(cmd);
        return true;
    }

    // --- clearsetcolor / clearsetdepth [L20731-20735] ---
    if (command == "clearsetcolor") {
        D3DXHANDLE param = effect->GetParameterByName(nullptr, value.c_str());
        bool ok = false;
        if (param != nullptr) {
            D3DXPARAMETER_DESC pd;
            memset(&pd, 0, sizeof(pd));
            effect->GetParameterDesc(param, &pd);
            // [FUN_180018200 0x180019f3b-0x180019f77] Class == VECTOR,
            // Columns == 4 and Type in {BOOL, INT, FLOAT} (any 4-component
            // numeric vector; the runtime reads it with GetFloatArray).
            if (pd.Class == D3DXPC_VECTOR && pd.Columns == 4 &&
                (pd.Type == D3DXPT_BOOL || pd.Type == D3DXPT_INT ||
                 pd.Type == D3DXPT_FLOAT)) {
                cmd.id = kSasCmdClearSetColor;
                cmd.param = param;
                out->push_back(cmd);
                ok = true;
            }
        }
        if (!ok) {
            // [0x1800B48A0] the message stays 'float4' for every rejection.
            SasLogFormat(c->sas, "Error: type of clear color must be 'float4': %s  %s\n",
                         value.c_str(), ctx.c_str());
            c->sas->hasErrors = true;
            return false;
        }
        return true;
    }
    if (command == "clearsetdepth") {
        D3DXHANDLE param = effect->GetParameterByName(nullptr, value.c_str());
        bool ok = false;
        if (param != nullptr) {
            D3DXPARAMETER_DESC pd;
            memset(&pd, 0, sizeof(pd));
            effect->GetParameterDesc(param, &pd);
            // [FUN_180018200 0x18001a07e] only Type in {BOOL, INT, FLOAT} is
            // required - no Class or shape constraint (scalars, vectors and
            // matrices all pass; the runtime reads the first element).
            if (pd.Type == D3DXPT_BOOL || pd.Type == D3DXPT_INT ||
                pd.Type == D3DXPT_FLOAT) {
                cmd.id = kSasCmdClearSetDepth;
                cmd.param = param;
                out->push_back(cmd);
                ok = true;
            }
        }
        if (!ok) {
            // [0x1800B48D0] the message stays 'float' for every rejection.
            SasLogFormat(c->sas, "Error: type of clear depth must be 'float': %s  %s\n",
                         value.c_str(), ctx.c_str());
            c->sas->hasErrors = true;
            return false;
        }
        return true;
    }
    // NOTE: no "clearsetstencil" token exists in the original compiler
    // (FUN_180018200 never compares against it), so it falls through to the
    // "Warning: unsupported script command" path below like any other
    // unknown command [0x1800B3DB0].

    // --- clear [L20839-20847] ---
    if (command == "clear") {
        if (SasValueEqualsNoCase(value, "color")) {
            cmd.id = kSasCmdClear;
            cmd.value = D3DCLEAR_TARGET;
            out->push_back(cmd);
            return true;
        }
        if (SasValueEqualsNoCase(value, "depth")) {
            // The original compiles clear=color to record id 8 and
            // clear=depth to record id 9 [FUN_180018200 0x180019907 v215=8 /
            // 0x180019926 v215=9] and hardcodes the flags per id in the
            // executor; case 9 @0x18001dae3 passes 6 = ZBUFFER|STENCIL (the
            // stencil plane clears to 0). This port keeps the single id-8
            // record and carries the flags in value.
            cmd.id = kSasCmdClear;
            cmd.value = D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL;
            out->push_back(cmd);
            return true;
        }
        // [0x1800B4910] "Warning: unknown clear type: %s"
        SasLogFormat(c->sas, "Warning: unknown clear type: %s  %s\n", whole.c_str(),
                     ctx.c_str());
        return true;  // warning only, not an error
    }

    // --- scriptexternal [L20871-20873]: technique-level only. FUN_180018200
    // compares the token at 0x180019652 on the technique-script path entered
    // at 0x1800194bf (jz loc_180019649 when the pass handle is null); in a
    // pass script the command fails the draw check (0x1800194dc) and falls to
    // LABEL_235 (0x180019820) -> "Warning: unsupported script command: "
    // [0x1800B3DB0] without pushing the command. ---
    if (command == "scriptexternal" && !c->hasPass) {
        if (SasValueEqualsNoCase(value, "color")) {
            cmd.id = kSasCmdScriptExternal;
            out->push_back(cmd);
            return true;
        }
        // [0x1800196a0-0x18001971b] the concatenated token is var_10D0 =
        // wrapper-slot m[5] = the raw VALUE (the same string the case-blind
        // "color" compare runs against); the whole-match token (m[3]) is only
        // used by the clear/draw/unsupported warnings.
        SasLogFormat(c->sas, "Warning: unknown script external type: %s  %s\n",
                     value.c_str(), ctx.c_str());
        return true;
    }

    // --- loopbycount / loopgetindex / loopend [L20895-20969]: technique-
    // level only, same gate as scriptexternal (the token compares at
    // 0x180019797 / 0x1800197c2 / 0x1800197ef sit behind the 0x1800194bf
    // jz; a pass script falls to LABEL_235 and only warns). ---
    if (command == "loopbycount" && !c->hasPass) {
        D3DXHANDLE param = effect->GetParameterByName(nullptr, value.c_str());
        if (param == nullptr || !SasIsLoopParameterType(c->sas, param)) {
            SasLogFormat(c->sas,
                         "Error: type of loop count must be 'int' or 'bool': %s  %s\n",
                         value.c_str(), ctx.c_str());
            c->sas->hasErrors = true;
            return false;
        }
        // The count is read at RUNTIME every entry (FUN_18001bbc0 case 0xD);
        // nothing is unrolled at compile time.
        cmd.id = kSasCmdLoopByCount;
        cmd.param = param;
        out->push_back(cmd);
        return true;
    }
    if (command == "loopgetindex" && !c->hasPass) {
        D3DXHANDLE param = effect->GetParameterByName(nullptr, value.c_str());
        if (param == nullptr || !SasIsLoopParameterType(c->sas, param)) {
            // [0x1800B49F0] "Error: type of loop index must be 'int' or 'bool': "
            SasLogFormat(c->sas,
                         "Error: type of loop index must be 'int' or 'bool': %s  %s\n",
                         value.c_str(), ctx.c_str());
            c->sas->hasErrors = true;
            return false;
        }
        cmd.id = kSasCmdLoopGetIndex;
        cmd.param = param;
        out->push_back(cmd);
        return true;
    }
    if (command == "loopend" && !c->hasPass) {
        cmd.id = kSasCmdLoopEnd;
        out->push_back(cmd);
        return true;
    }

    // --- draw [L20973-21027]: pass-level only. FUN_180018200 checks "draw"
    // inside the `if (v158)` (pass script) branch at 0x1800194bf; a technique-
    // level draw= never matches and falls to the unsupported-command warning
    // (0x1800B3DB0). ---
    if (command == "draw" && c->hasPass) {
        if (SasValueEqualsNoCase(value, "geometry")) {
            cmd.id = kSasCmdDrawGeometry;
            c->sas->drawsGeometry = true;  // [0x180019515] sas+0x3c flag, pass-level branch only
            out->push_back(cmd);
            return true;
        }
        if (SasValueEqualsNoCase(value, "buffer")) {
            cmd.id = kSasCmdDrawBuffer;
            out->push_back(cmd);
            return true;
        }
        SasLogFormat(c->sas, "Warning: unknown draw type: %s  %s\n", whole.c_str(),
                     ctx.c_str());
        return true;
    }

    // --- pass [FUN_180018200 token "pass", error "Error: unknown pass
    // name: "]: technique-level only (the "pass" compare sits inside
    // `if (!v158)` at 0x180018eda); a pass-level pass= falls to the
    // unsupported-command warning. Compile to the id-0 entry with the pass
    // index. ---
    if (command == "pass" && !c->hasPass) {
        SasTechnique* tech = nullptr;
        for (size_t t = 0; t < c->sas->techniques.size(); ++t) {
            if (c->sas->techniques[t].handle == c->techniqueHandle) {
                tech = &c->sas->techniques[t];
                break;
            }
        }
        if (tech != nullptr) {
            for (size_t p = 0; p < tech->passes.size(); ++p) {
                SasPass& pass = tech->passes[p];
                if (pass.name != value) {
                    continue;
                }
                cmd.id = kSasCmdPass;
                cmd.value = pass.index;
                out->push_back(cmd);
                return true;
            }
        }
        SasLogFormat(c->sas, "Error: unknown pass name: %s  %s\n", value.c_str(),
                     ctx.c_str());
        c->sas->hasErrors = true;
        return false;
    }

    // --- renderport [L20976-20981]: accepted and ignored ---
    if (command == "renderport") {
        return true;
    }

    // [0x1800B3DB0] "Warning: unsupported script command: %s  (ctx)\n"
    SasLogFormat(c->sas, "Warning: unsupported script command: %s  %s\n",
                 whole.c_str(), ctx.c_str());
    return true;  // warnings never abort the compile
}

// The tokenizer loop [FUN_180018200 L19956-21052]. Commands compile flat -
// the loop commands stay in the list and execute at runtime with the loop
// stack (the original's runtime state machine, FUN_18001bbc0 cases
// 0xD/0xE/0xF).
//
// [0x1800188f0-0x180018d78] every token dispatches through FOUR submatch
// extractions - wrapper slots m[3]/m[4]/m[5]/m[6] (the original's regex
// wrapper reserves m[0..2], so the pattern's own groups 1..4 land at slots
// 3..6): m[3] = the whole "cmd=value", m[4] = the command (\w+), m[5] = the
// raw value, m[6] = the rest after ';'.
//
// [LABEL_188] every HARD compile error (unknown pass name, unknown texture
// name, invalid target type, loop parameter missing/mistyped, clearsetcolor
// parameter missing) logs its line, sets v29 = 1 and CONTINUES the loop -
// already-pushed commands are kept and later tokens still compile (the
// partial script still runs). Warnings (LABEL_238) continue without v29.
//
// [0x18001ae60 tail] when the regex stops matching and the residue is
// non-empty, the residue is truncated at the FIRST ';' (sub_18001FA80 is a
// forward memchr-based find, sub_18001E6D0 the substr - NOT rfind),
// "Error: script syntax error: <residue>  <ctx>\n" is logged and v29 = 1;
// nothing after the failure point compiles.
static bool SasCompileTokenizer(SasCompiler* c, const std::string& script,
                                std::vector<SasScriptCmd>* out) {
    bool hadErrors = false;
    std::string rest = script;
    while (!rest.empty()) {
        // regex ^\s*((\w+)\s*=\s*([^=;\s]*))\s*;\s*(.*) over the folded
        // text. Greedy spans need no backtracking: a [^=;\s] char can never
        // be consumed by a \s*, so a failed "\s*;" after the value or a
        // failed "\s*=" after the command fails the whole match.
        size_t i = 0;
        while (i < rest.size() && isspace(static_cast<unsigned char>(rest[i]))) {
            ++i;  // ^\s* (leading whitespace survived the fold)
        }
        size_t cmdStart = i;
        while (i < rest.size() &&
               (isalnum(static_cast<unsigned char>(rest[i])) || rest[i] == '_')) {
            ++i;  // (\w+) - at least one word char before the '='
        }
        if (i == cmdStart) {
            break;  // no match -> tail (e.g. "=x;")
        }
        size_t cmdEnd = i;
        while (i < rest.size() && isspace(static_cast<unsigned char>(rest[i]))) {
            ++i;  // \s* before '='
        }
        if (i >= rest.size() || rest[i] != '=') {
            break;  // no match -> tail (whitespace inside the command word)
        }
        ++i;  // '='
        while (i < rest.size() && isspace(static_cast<unsigned char>(rest[i]))) {
            ++i;  // \s* after '='
        }
        size_t valStart = i;
        while (i < rest.size() && rest[i] != '=' && rest[i] != ';' &&
               !isspace(static_cast<unsigned char>(rest[i]))) {
            ++i;  // ([^=;\s]*) - e.g. "a=b=c;" fails below
        }
        size_t valEnd = i;
        size_t j = i;
        while (j < rest.size() && isspace(static_cast<unsigned char>(rest[j]))) {
            ++j;  // \s* before ';'
        }
        if (j >= rest.size() || rest[j] != ';') {
            break;  // no match -> tail (whitespace inside the value)
        }
        ++j;  // ';'
        while (j < rest.size() && isspace(static_cast<unsigned char>(rest[j]))) {
            ++j;  // \s* after ';'
        }
        std::string whole = rest.substr(cmdStart, valEnd - cmdStart);    // m[3]
        std::string command = rest.substr(cmdStart, cmdEnd - cmdStart);  // m[4]
        std::string value = rest.substr(valStart, valEnd - valStart);    // m[5]
        rest = rest.substr(j);                                           // m[6]
        // [原版事实，勿再错记] 命令 token 不经过任何 tolower：
        // sub_1800231B0 反编译为 string clear/init（Src[3]=15; Src[2]=0;
        // *Src=0），不是 tolower；命令分发全部是 memcmp /
        // sub_18000A640（std::string::compare）对 token 原文与小写字面量
        // 的字节敏感比较 + 精确长度检查（0x180018efc "pass"、
        // 0x1800191dd "rendercolortarget0"、0x1800194d5 "draw" 等）——
        // 即原版命令匹配大小写敏感，"Clear=Color" 会走 LABEL_238 警告
        // "unsupported script command"。下面这个整体 tolower 是移植的
        // 放宽偏差（旧实现残留，比原版宽松），行为暂按现状保留；
        // 枚举 VALUE 的大小写不敏感比较见 SasValueEqualsNoCase。
        for (size_t k = 0; k < command.size(); ++k) {
            command[k] = static_cast<char>(
                tolower(static_cast<unsigned char>(command[k])));
        }
        // A false return now means "error line logged" - the loop continues
        // either way (LABEL_188).
        if (!SasCompileToken(c, command, value, whole, out)) {
            hadErrors = true;
        }
    }
    if (!rest.empty()) {
        size_t semi = rest.find(';');
        if (semi != std::string::npos) {
            rest = rest.substr(0, semi);
        }
        SasLogFormat(c->sas, "Error: script syntax error: %s  %s\n",
                     rest.c_str(), c->ContextSuffix().c_str());
        hadErrors = true;
    }
    return hadErrors;
}

}  // namespace

bool SasCompileScript(SasEffect* sas, const std::string& script,
                      D3DXHANDLE technique, D3DXHANDLE pass,
                      const std::string& techniqueName, const std::string& passName,
                      std::vector<SasScriptCmd>* out) {
    (void)pass;
    if (sas == nullptr || out == nullptr) {
        return false;
    }
    out->clear();
    SasCompiler c;
    c.sas = sas;
    c.techniqueHandle = technique;
    c.techniqueName = techniqueName;
    c.passName = passName;
    c.hasPass = (pass != nullptr);

    std::string normalized = SasNormalizeScriptExec(script);
    if (normalized.empty()) {
        return false;
    }
    // v29: nonzero once ANY hard error was logged (the compile itself always
    // runs to completion). FUN_1800169D0/FUN_180017A80 OR it into their error
    // returns and sub_18000BC0 merely SKIPS the sub_18001DD20 scan step - the
    // effect is not rejected.
    bool hadErrors = SasCompileTokenizer(&c, normalized, out);
    if (hadErrors) {
        sas->hasErrors = true;
    }
    return hadErrors;
}

// ===========================================================================
// FUN_18001b7b0 / FUN_180059a30 - the run state
// ===========================================================================

SasRunState* SasCreateRunState(IDirect3DDevice9* device) {
    SasRunState* state = new SasRunState();
    if (device != nullptr) {
        for (int i = 0; i < 4; ++i) {
            device->GetRenderTarget(static_cast<UINT>(i), &state->savedColor[i]);
        }
        device->GetDepthStencilSurface(&state->savedDepth);
        device->GetViewport(&state->savedViewport);
        // [FUN_18001b7b0 0x18001b883-0x18001ba1] the "current" viewport mirror
        // starts as a full copy of the saved viewport (all six fields).
        state->currentViewport = state->savedViewport;
        // [FUN_18001b7b0 L56-59] stage the host clear defaults (MMHack
        // GetClearColor / z = 1.0).
        if (g_host.GetClearColor != nullptr) {
            state->clearColor = g_host.GetClearColor(g_host.ctx);
        }
        state->clearDepth = 1.0f;
    }
    return state;
}

void SasDestroyRunState(SasRunState* state) {
    if (state == nullptr) {
        return;
    }
    for (int i = 0; i < 4; ++i) {
        if (state->savedColor[i] != nullptr) {
            state->savedColor[i]->Release();
        }
    }
    if (state->savedDepth != nullptr) {
        state->savedDepth->Release();
    }
    delete state;
}

void SasRestoreRunState(IDirect3DDevice9* device, SasRunState* state) {
    if (device == nullptr || state == nullptr || state->redirectedMask == 0) {
        return;
    }
    // [FUN_18001bbc0 LABEL_13] bits 1..4 = color slots 0..3, bit 0 = depth;
    // the saved viewport is re-asserted with the restore.
    for (int i = 0; i < 4; ++i) {
        unsigned int bit = 1u << (i + 1);
        if ((state->redirectedMask & bit) != 0) {
            // MRT1..3 are normally unbound. nullptr is the saved state and
            // must be actively restored; leaving Ray's auxiliary MRT bound
            // makes the next back-buffer draw invalid/frozen. RT0 itself may
            // not be null, so only issue its restore when capture succeeded.
            if (i != 0 || state->savedColor[i] != nullptr) {
                device->SetRenderTarget(static_cast<UINT>(i),
                                        state->savedColor[i]);
            }
        }
    }
    if ((state->redirectedMask & 1u) != 0) {
        device->SetDepthStencilSurface(state->savedDepth);
    }
    device->SetViewport(&state->savedViewport);
    state->redirectedMask = 0;
    state->suspended = false;
}

// ===========================================================================
// FUN_18001c760 - command applier (ids 1..9)
// ===========================================================================

// [FUN_18001c760 case 5 0x18001d351-0x18001d390] the depth redirect resolves
// the surface ONLY through the process-wide registry (see
// SasFindDepthSurface): the LIVE texture bound to the parameter is the map
// key, so a depth texture created by ANY loaded effect resolves
// (cross-effect depth sharing), while color targets / offscreen targets /
// file textures - textures that never registered - fail with the synthetic
// code 1. A registered key whose surface is null binds NULL without error
// (0x18001d383 `test rdi, rdi; jz loc_18001D3B5`). The former per-effect
// resource-table search was a pre-registry stopgap; the original has no
// such fallback.

// [FUN_18001c760 LABEL_44 (color) / LABEL_160 (depth)] the render-target
// failure report, assembled at 0x18001cab1-0x18001cfa4 / 0x18001d56c-0x18001d5ce:
//   "<fullPath>:\n"
//   "Error: invalid render target: RenderColorTarget<N>=<paramName>[ [XXXXXXXX]]  (technique: <tech>)\n"
// (the depth variant spells "RenderDepthStencilTarget=" with no <N>; the
// " [XXXXXXXX]" part is appended only when the code is not the synthetic 1).
// The original dispatches the line to the global message list (sub_180009080)
// and shows a deduplicated MessageBox (byte_1800D99D8 gates the dedup set).
// The port has no message-box machinery: the line lands in the effect log
// once per occurrence (each failure aborts the walk anyway).
static void SasReportTargetError(SasEffect* sas, D3DXHANDLE technique,
                                 D3DXHANDLE param, bool isDepth, int slot,
                                 HRESULT hr) {
    const char* paramName = "";
    D3DXPARAMETER_DESC pd;
    memset(&pd, 0, sizeof(pd));
    if (param != nullptr && sas->effect->GetParameterDesc(param, &pd) == S_OK &&
        pd.Name != nullptr) {
        paramName = pd.Name;
    }
    const char* techName = "";
    D3DXTECHNIQUE_DESC td;
    memset(&td, 0, sizeof(td));
    if (technique != nullptr &&
        sas->effect->GetTechniqueDesc(technique, &td) == S_OK &&
        td.Name != nullptr) {
        techName = td.Name;
    }
    char hrbuf[24] = "";
    if (hr != 1) {
        _snprintf_s(hrbuf, sizeof(hrbuf), _TRUNCATE, " [%08X]",
                    static_cast<unsigned int>(hr));
    }
    SasLogFormat(sas, "%s:\n", sas->path.c_str());
    if (isDepth) {
        SasLogFormat(sas,
                     "Error: invalid render target: RenderDepthStencilTarget=%s%s  (technique: %s)\n",
                     paramName, hrbuf, techName);
    } else {
        SasLogFormat(sas,
                     "Error: invalid render target: RenderColorTarget%d=%s%s  (technique: %s)\n",
                     slot, paramName, hrbuf, techName);
    }
}

// The two QI targets FUN_18001c760 passes at 0x18001c855 / 0x18001c8d7
// (bytes at 0x1800B2550 / 0x1800B2560): IID_IDirect3DTexture9 and
// IID_IDirect3DCubeTexture9. Defined locally like the dshow GUID constants
// so the link does not depend on dxguid.lib.
static const GUID kIidD3DTexture9 = {
    0x85c31227, 0x3de5, 0x4f00, {0x9b, 0x3a, 0xf1, 0x1a, 0xc3, 0x8c, 0x18, 0xb5}};
static const GUID kIidD3DCubeTexture9 = {
    0xfff32f81, 0xd953, 0x473a, {0x92, 0x23, 0x93, 0xd6, 0x52, 0xab, 0xa9, 0x3f}};

// [FUN_18001c760 0x18001c811-0x18001c931] acquire the surface for a color
// target redirect from the LIVE texture bound to the parameter (GetTexture,
// so ANIMATEDTEXTURE rebinding is picked up). Returns S_OK with an AddRef'd
// surface in *surface (owned by the caller):
//   - element < 0: the texture must be D3DRTYPE_TEXTURE; QI to
//     IDirect3DTexture9 and GetSurfaceLevel(0) [0x18001c88d].
//   - element >= 0: the texture must be D3DRTYPE_CUBETEXTURE; QI to
//     IDirect3DCubeTexture9 and GetCubeMapSurface(element, 0) - the script
//     index selects the cube FACE, the mip level stays 0 [0x18001c913].
// Any other resource type fails with the original's synthetic code 1.
// *unbound is set when the parameter exists but has no texture bound.
static HRESULT SasAcquireColorSurface(ID3DXEffect* effect, D3DXHANDLE param,
                                      int element,
                                      IDirect3DSurface9** surface,
                                      bool* unbound) {
    *surface = nullptr;
    *unbound = false;
    IDirect3DBaseTexture9* tex = nullptr;
    HRESULT hr = effect->GetTexture(param, &tex);
    if (FAILED(hr)) {
        return hr;  // [0x18001c816] the GetTexture failure is reported as-is
    }
    if (tex == nullptr) {
        *unbound = true;  // [0x18001c93c] handled by the caller (RT0 fallback)
        return S_OK;
    }
    if (element >= 0) {
        if (tex->GetType() != D3DRTYPE_CUBETEXTURE) {
            tex->Release();
            return 1;
        }
        IDirect3DCubeTexture9* cube = nullptr;
        if (FAILED(tex->QueryInterface(kIidD3DCubeTexture9,
                                       reinterpret_cast<void**>(&cube))) ||
            cube == nullptr) {
            tex->Release();
            return 1;
        }
        hr = cube->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(element), 0,
                                     surface);
        cube->Release();
    } else {
        if (tex->GetType() != D3DRTYPE_TEXTURE) {
            tex->Release();
            return 1;
        }
        IDirect3DTexture9* tex2d = nullptr;
        if (FAILED(tex->QueryInterface(kIidD3DTexture9,
                                       reinterpret_cast<void**>(&tex2d))) ||
            tex2d == nullptr) {
            tex->Release();
            return 1;
        }
        hr = tex2d->GetSurfaceLevel(0, surface);
        tex2d->Release();
    }
    tex->Release();
    return hr;
}

// [FUN_18000a8e0 0x18000a96e / qword_1800D9A38] the RT0 fallback for an
// unbound target is MMEffect's global scratch surface - the single 16x16
// D3DFMT_X8R8G8B8 (fmt 0x16) render target the engine init creates once
// next to the effect pool (CreateRenderTarget(16, 16, 22, 0, 0, 0, ...),
// recreated on device reset by MmeEngineInit). The fallback path never
// creates a surface: 0x18001c93e-0x18001c973 AddRefs the singleton
// (vtable+8), swaps it into the slot and Releases the previous occupant
// (vtable+0x10); the caller here drops its acquisition ref right after the
// bind (SetRenderTarget holds its own reference).
static HRESULT SasCreateFallbackSurface(IDirect3DDevice9* /*device*/,
                                        IDirect3DSurface9** surface) {
    *surface = g_offscreenSurface;                 // qword_1800D9A38
    if (*surface == nullptr) {
        return 1;   // engine init never produced the scratch target
    }
    (*surface)->AddRef();                          // [0x18001c95a]
    return S_OK;
}

// The per-command applier (FUN_18001c760 switch). Render-target redirects
// and clear staging live in the SHARED run state - they persist across the
// whole technique execution (step + suspension + resume), matching the
// original's single run-context object.
static HRESULT SasApplyCommandImpl(SasEffect* sas, const SasScriptCmd& cmd,
                                   IDirect3DDevice9* device, SasRunState* state,
                                   D3DXHANDLE techniqueHandle, int passIndex) {
    ID3DXEffect* effect = sas->effect;

    switch (cmd.id) {
        case kSasCmdRenderColorTarget0:
        case kSasCmdRenderColorTarget1:
        case kSasCmdRenderColorTarget2:
        case kSasCmdRenderColorTarget3: {
            int slot = cmd.id - kSasCmdRenderColorTarget0;
            unsigned int bit = 1u << (slot + 1);
            if (cmd.param == nullptr) {
                // "=;" reset: rebind the saved surface and drop the redirect
                // bit (FUN_18001c760 0x18001c984-0x18001ca67). A null saved
                // surface is only representable for slots 1..3 (bind null);
                // slot 0 takes the LABEL_31 synthetic code 1 + report.
                if (slot != 0 || state->savedColor[slot] != nullptr) {
                    HRESULT hr = device->SetRenderTarget(
                        static_cast<UINT>(slot), state->savedColor[slot]);
                    if (hr != S_OK) {
                        // [LABEL_44] no mask clear, no viewport re-assert.
                        SasReportTargetError(sas, techniqueHandle, cmd.param,
                                             false, slot, hr);
                        return hr;
                    }
                    state->redirectedMask &= ~bit;
                    state->activeColor[slot] = nullptr;
                    if (slot == 0) {
                        // [0x18001c9f0-0x18001ca22] the reset copies the saved
                        // viewport back in full (all six fields).
                        state->currentViewport = state->savedViewport;
                    }
                    // [0x18001ca74] every color target switch re-asserts the
                    // current viewport.
                    device->SetViewport(&state->currentViewport);
                    return hr;
                }
                SasReportTargetError(sas, techniqueHandle, cmd.param, false,
                                     slot, 1);
                return 1;
            }
            // [0x18001c811] resolve the surface from the live bound texture;
            // the element index (cmd.value) selects a cube face.
            IDirect3DSurface9* surf = nullptr;
            bool unbound = false;
            HRESULT hr = SasAcquireColorSurface(effect, cmd.param,
                                                cmd.value, &surf, &unbound);
            if (hr != S_OK) {
                // includes the original's synthetic failure code 1 (LABEL_31)
                SasReportTargetError(sas, techniqueHandle, cmd.param, false,
                                     slot, hr);
                return hr;
            }
            bool owned = (surf != nullptr);
            if (unbound) {
                // [0x18001c93c-0x18001c973] RT0 falls back to the global 16x16
                // scratch target; RT1..3 rebind an empty target.
                if (slot == 0) {
                    hr = SasCreateFallbackSurface(device, &surf);
                    if (hr != S_OK) {
                        SasReportTargetError(sas, techniqueHandle, cmd.param,
                                             false, slot, hr);
                        return hr;  // fallback unavailable (code 1 like LABEL_31)
                    }
                    owned = true;
                }
            }
            // [FUN_18001c760 0x18001c7dc-0x18001c9c0] saved 只在 run state
            // 构造期（FUN_18001b7b0 0x18001b818-0x18001b83c）一次性定格，
            // 命令实施器的 bind 路径从不 GetRenderTarget 回读当前目标。
            // 这里绝不重捕获：MRT1..3 的 saved 合法定格值就是 nullptr（宿主
            // 未绑多目标），若在 redirect 时现抓，会把前一个 pass 留下的
            // 中间渲染目标写进 saved，LABEL_13 恢复（0x18001bd5a 起）就会
            // 恢复到错误目标（ray-MMD 每 pass 重设 MRT 必踩）。
            hr = device->SetRenderTarget(static_cast<UINT>(slot), surf);
            if (hr != S_OK) {
                // [LABEL_44] failure: the redirect bit, the active-target
                // mirror and the viewport all stay untouched.
                if (owned && surf != nullptr) {
                    surf->Release();
                }
                SasReportTargetError(sas, techniqueHandle, cmd.param, false,
                                     slot, hr);
                return hr;
            }
            state->redirectedMask |= bit;
            state->activeColor[slot] = surf;
            if (slot == 0 && surf != nullptr) {
                // [0x18001ca38-0x18001ca51] GetDesc, then rewrite ONLY
                // X/Y/Width/Height of the current viewport - MinZ/MaxZ keep
                // their current values. (The original does not check the
                // GetDesc return; the port keeps the guard - unreachable on
                // a live surface.)
                D3DSURFACE_DESC desc;
                memset(&desc, 0, sizeof(desc));
                if (SUCCEEDED(surf->GetDesc(&desc))) {
                    state->currentViewport.X = 0;
                    state->currentViewport.Y = 0;
                    state->currentViewport.Width = desc.Width;
                    state->currentViewport.Height = desc.Height;
                }
            }
            // [0x18001ca74] LABEL_40: re-assert the current viewport after
            // every color target switch.
            device->SetViewport(&state->currentViewport);
            if (owned && surf != nullptr) {
                surf->Release();  // [0x18001ca8f] drop the acquisition ref
            }
            return hr;
        }
        case kSasCmdRenderDepthTarget: {
            if (cmd.param == nullptr) {
                // "=;" reset [0x18001d0f5-0x18001d13f]: AddRef + rebind the
                // saved depth (a null saved depth binds NULL - not an error).
                HRESULT hr = device->SetDepthStencilSurface(state->savedDepth);
                if (hr != S_OK) {
                    // [LABEL_160] no mask clear, no viewport re-assert.
                    SasReportTargetError(sas, techniqueHandle, cmd.param, true,
                                         -1, hr);
                    return hr;
                }
                state->redirectedMask &= ~1u;
                state->activeDepth = nullptr;
                // [0x18001d3eb] the depth switch re-asserts the current
                // viewport (unchanged - the depth path never rewrites it).
                device->SetViewport(&state->currentViewport);
                return hr;
            }
            // [FUN_18001c760 case 5 0x18001d2f4-0x18001d40c] resolve the
            // surface from the LIVE texture bound to the parameter, then the
            // process-wide depth-target registry (see SasFindDepthSurface):
            //   GetTexture failed          -> LABEL_160 report, return hr
            //   tex == null                -> bind NULL (no error)
            //   tex not registered         -> synthetic code 1 + report
            //   registered, surface == null-> bind NULL (no error)
            IDirect3DBaseTexture9* tex = nullptr;
            HRESULT hr = effect->GetTexture(cmd.param, &tex);
            if (hr != S_OK) {
                SasReportTargetError(sas, techniqueHandle, cmd.param, true, -1,
                                     hr);
                return hr;
            }
            IDirect3DSurface9* surf = nullptr;
            bool owned = false;
            if (tex != nullptr) {
                bool registered = SasFindDepthSurface(tex, &surf);
                if (!registered) {
                    tex->Release();
                    SasReportTargetError(sas, techniqueHandle, cmd.param, true,
                                         -1, 1);
                    return 1;
                }
                if (surf != nullptr) {
                    surf->AddRef();  // [0x18001d38b] IUnknown::AddRef (vtbl+8)
                    owned = true;
                }
                tex->Release();      // [case 5 tail] the GetTexture reference
            }
            // [FUN_18001c760 case 5 0x18001d2f4-0x18001d3b5] 与 color 路径
            // 同理：savedDepth 只在构造期（0x18001b86a）定格，bind 路径无
            // GetDepthStencilSurface 回读；redirect 时不得现抓当前深度面。
            hr = device->SetDepthStencilSurface(surf);
            if (hr != S_OK) {
                // [LABEL_160] failure: the redirect bit and the active-depth
                // mirror stay untouched, no viewport re-assert.
                if (owned) {
                    surf->Release();
                }
                SasReportTargetError(sas, techniqueHandle, cmd.param, true, -1,
                                     hr);
                return hr;
            }
            state->redirectedMask |= 1u;
            state->activeDepth = surf;
            // [0x18001d3eb] the depth switch re-asserts the current viewport
            // (unchanged - the depth path never rewrites it).
            device->SetViewport(&state->currentViewport);
            if (owned) {
                surf->Release();
            }
            return hr;
        }
        case kSasCmdClearSetColor: {
            // [FUN_18001c760 case 6 0x18001d9db-0x18001dab0] GetFloatArray of
            // 4 (converted from bool4/int4/float4 alike), clamp every
            // component to [0,1], then compose the staged D3DCOLOR with a
            // TRUNCATING (int)(f * 255.0f) cast - no +0.5 rounding.
            float f[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            effect->GetFloatArray(cmd.param, f, 4);
            auto clamp01 = [](float v) -> float {
                if (!(v > 0.0f)) return 0.0f;   // NaN folds to 0 like the
                if (v > 1.0f) return 1.0f;      // original's compare chain
                return v;
            };
            DWORD r = static_cast<DWORD>(static_cast<int>(clamp01(f[0]) * 255.0f));
            DWORD g = static_cast<DWORD>(static_cast<int>(clamp01(f[1]) * 255.0f));
            DWORD b = static_cast<DWORD>(static_cast<int>(clamp01(f[2]) * 255.0f));
            DWORD a = static_cast<DWORD>(static_cast<int>(clamp01(f[3]) * 255.0f));
            state->clearColor = D3DCOLOR_ARGB(a, r, g, b);
            state->clearColorStaged = true;
            return S_OK;
        }
        case kSasCmdClearSetDepth: {
            // [FUN_18001c760 case 7 0x18001dac7] GetFloatArray of 1 into the
            // staged z - the first element of whatever numeric parameter the
            // (type-only) compile check accepted.
            float f = 1.0f;
            effect->GetFloatArray(cmd.param, &f, 1);
            state->clearDepth = f;
            state->clearDepthStaged = true;
            return S_OK;
        }
        case kSasCmdClear: {
            DWORD flags = static_cast<DWORD>(cmd.value);
            // [FUN_18001c760 case 8 0x18001dacf] clear=color: flags=1, color
            // = the staged +0x60 color, z = the 1.0 constant [0x1800B5B28]
            // (D3D ignores z without D3DCLEAR_ZBUFFER). Case 9 0x18001dae3
            // clear=depth: flags=6 = ZBUFFER|STENCIL, color = 0, z = the
            // staged +0x64 depth. Both go through the host clear dispatcher
            // sub_18005AEE0, which passes stencil = 0 literally. The staged
            // values are the run-state ones (host defaults at creation,
            // ClearSetColor/ClearSetDepth overwrite them) - the original
            // never re-reads the host at clear time.
            D3DCOLOR color = 0;
            float depth = 1.0f;
            if ((flags & D3DCLEAR_TARGET) != 0) {
                color = state->clearColor;
            }
            if ((flags & D3DCLEAR_ZBUFFER) != 0) {
                depth = state->clearDepth;
            }
            // [sub_18005AEE0 0x18005af06-0x18005af2b] the host clear
            // dispatcher first registers the RUNTIME values into the per
            // -target staged-clear maps when the CURRENTLY-WALKING carrier
            // is scene class (its ModelData+0x364 == 2, the port's
            // renderClass() == 2 via ctx->sceneWalkCarrier): flags & 1 ->
            // sub_18005E4F0 (the four current color targets get the staged
            // ClearSetColor value), flags & 2 -> sub_18005E570 (the current
            // depth surface gets the staged ClearSetDepth value). Object
            // (class 0) and preprocess (class 1) carriers stage nothing -
            // their only map writer stays the repeat-boundary annotation
            // apply (FUN_18005c970 / MmeApplyPassRecord). Registered BEFORE
            // the device Clear, exactly the original's ordering.
            MmeStageSceneClearRuntime(flags, color, depth);
            device->Clear(0, nullptr, flags, color, depth, 0);
            // [FUN_18001c760 LABEL_222 0x18001db5b] the dispatcher call's
            // return is DISCARDED - cases 8/9 always yield success.
            return S_OK;
        }
        case kSasCmdClearEx:
            // FUN_18001c760 case 9 is the original's clear=depth record;
            // this port compiles clear=depth into kSasCmdClear with
            // value = D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL, so id 9 is never
            // compiled and this arm stays unreachable.
            return S_OK;
        case kSasCmdDrawGeometry:
        case kSasCmdDrawBuffer: {
            // [FUN_18001bf80 L21774-21781] SetTechnique(technique) + host
            // slot 0 (geometry draw) / slot 1 (buffer draw).
            effect->SetTechnique(techniqueHandle);
            if (g_host.RunPass != nullptr) {
                int kind = (cmd.id == kSasCmdDrawGeometry) ? 0 : 1;
                return g_host.RunPass(g_host.ctx, sas, kind, passIndex);
            }
            return S_OK;
        }
        default:
            return S_OK;  // loop / scriptexternal ids are handled by the walk
    }
}

// [FUN_18001bf80 L21796-21872] the "DirectX Error:" reporter.
static void SasReportError(SasEffect* sas, const SasTechnique* tech,
                           const SasPass* pass, HRESULT hr) {
    const char* desc = MmeDxErrDescription(static_cast<unsigned long>(hr));
    char hrbuf[16];
    _snprintf_s(hrbuf, sizeof(hrbuf), _TRUNCATE, "%08X", static_cast<unsigned int>(hr));
    if (pass != nullptr) {
        SasLogFormat(sas, "technique: %s, pass: %s:\n",
                     tech != nullptr ? tech->name.c_str() : "?",
                     pass->name.c_str());
    } else {
        SasLogFormat(sas, "technique: %s:\n",
                     tech != nullptr ? tech->name.c_str() : "?");
    }
    SasLogFormat(sas, "DirectX Error: %s [%s]\n", (desc != nullptr) ? desc : "Unknown",
                 hrbuf);
    SasLogLine(sas, "Failed to run the effect script:\n");
}

// ===========================================================================
// FUN_18001bf80 - one pass entry
// ===========================================================================

HRESULT SasRunPassCommands(SasEffect* sas, SasTechnique* tech, SasPass* pass,
                           IDirect3DDevice9* device, SasRunState* state) {
    if (sas == nullptr || device == nullptr || state == nullptr) {
        return E_POINTER;
    }
    HRESULT finalHr = S_OK;
    if (!pass->hasScript) {
        // [FUN_18001bf80 no-script path 0x18001bffd-0x18001c012] the pass-script
        // default is Draw=Geometry (REFERENCE.txt L1300): SetTechnique + host
        // slot 0.
        sas->effect->SetTechnique(tech != nullptr ? tech->handle : nullptr);
        if (g_host.RunPass != nullptr) {
            HRESULT hr = g_host.RunPass(g_host.ctx, sas, 0, pass->index);
            if (hr != S_OK) {
                SasReportError(sas, tech, pass, hr);
                finalHr = hr;
            }
        }
        return finalHr;
    }
    // [FUN_18001bf80 0x18001bfef / 0x18001c047] hasScript 为真时命令表为空
    // （(end-begin)/24==0）→ 直接 return 0，不绘制：一个带空脚本的 pass
    // 不是默认 host 绘制（只有无 Script 注解的 pass 才默认绘制）。
    // 下面的命令循环对空表自然空转返回 S_OK，与原版一致。
    for (size_t i = 0; i < pass->cmds.size(); ++i) {
        HRESULT hr = SasApplyCommandImpl(sas, pass->cmds[i], device, state,
                                         tech != nullptr ? tech->handle : nullptr,
                                         pass->index);
        if (hr != S_OK) {
            // [FUN_18001bf80 0x18001c11f-0x18001c129] ids 1..9 return WITHOUT
            // the pass-level report - the applier already produced the
            // render-target failure report (LABEL_44/LABEL_160). Only the
            // draw= failures take the FUN_18001BF80 report (LABEL_16).
            if (pass->cmds[i].id == kSasCmdDrawGeometry ||
                pass->cmds[i].id == kSasCmdDrawBuffer) {
                SasReportError(sas, tech, pass, hr);
            }
            finalHr = hr;
            break;
        }
    }
    return finalHr;
}

// ===========================================================================
// FUN_18001bbc0 - the technique command walk (runtime state machine)
// ===========================================================================

namespace {

// [FUN_18001bbc0 case 0xD skip-forward] when the runtime count is <= 0, jump
// past the matching LoopEnd (nesting-aware static scan of the flat list).
static unsigned int SasSkipLoopBody(const std::vector<SasScriptCmd>& cmds,
                                    unsigned int loopAtIndex) {
    unsigned int i = loopAtIndex + 1;
    int depth = 1;
    while (i < cmds.size()) {
        int id = cmds[i].id;
        if (id == kSasCmdLoopByCount) {
            ++depth;
        } else if (id == kSasCmdLoopEnd) {
            --depth;
            if (depth == 0) {
                return i + 1;  // first command after the matching LoopEnd
            }
        }
        ++i;
    }
    return static_cast<unsigned int>(cmds.size());
}

SasPass* SasFindPassByIndex(SasTechnique* tech, int index) {
    for (size_t p = 0; p < tech->passes.size(); ++p) {
        if (tech->passes[p].index == index) {
            return &tech->passes[p];
        }
    }
    return nullptr;
}

// The shared walk. stepMode = FUN_18001bbc0's a4: in step mode the
// ScriptExternal command suspends (store the next index, return WITHOUT the
// restore epilogue so the scene renders into the bound targets); in full
// mode it is a no-op and the run completes with the LABEL_13 restore.
HRESULT SasRunTechniqueWalk(SasEffect* sas, SasTechnique* tech,
                            IDirect3DDevice9* device, SasRunState* state,
                            bool stepMode, bool* suspendedOut) {
    if (sas == nullptr || tech == nullptr || device == nullptr || state == nullptr) {
        return E_POINTER;
    }
    ID3DXEffect* effect = sas->effect;
    const std::vector<SasScriptCmd>& cmds = tech->cmds;
    HRESULT finalHr = S_OK;
    if (suspendedOut != nullptr) {
        *suspendedOut = false;
    }

    if (!tech->hasScript) {
        // [FUN_18001bbc0 0x18001bc22-0x18001bc8b] a technique with NO Script
        // annotation has no compiled command table. In step mode (a4=1) the
        // walk returns 0 immediately at 0x18001bc2a - no suspension, no
        // restore - so the technique "completes" for the step recorder. In
        // full mode (a4=0) the walk takes GetTechniqueDesc.Passes
        // (vtable+40) and runs every pass through ID3DXEffect::GetPass
        // (vtable+112) + FUN_18001bf80, stopping at the first failure, then
        // reaches the LABEL_13 restore epilogue.
        if (stepMode) {
            return S_OK;
        }
        for (size_t p = 0; p < tech->passes.size(); ++p) {
            HRESULT hr = SasRunPassCommands(sas, tech, &tech->passes[p], device,
                                            state);
            if (hr != S_OK) {
                finalHr = hr;
                break;
            }
        }
        state->suspended = false;
        SasRestoreRunState(device, state);   // LABEL_13
        // [FUN_18001bbc0 tail 0x18001bdb7] a walk that ends at LABEL_13 with
        // a nonzero result latches the sas+0x39 run-failed flag (one-way
        // until the effect is unloaded; see SasEffect::runFailed).
        if (finalHr != S_OK) {
            SasNoteRunFailureFirstCause(sas, "technique pass loop",
                                        -1, -1, finalHr);
            sas->runFailed = true;
        }
        return finalHr;
    }

    // A technique WITH a Script annotation walks its (possibly empty - e.g.
    // renderport-only) compiled table; an exhausted list falls straight
    // through to the epilogue like the original's 0x18001bccc bounds check.
    while (state->commandIndex < cmds.size()) {
        const SasScriptCmd& cmd = cmds[state->commandIndex];
        bool advance = true;
        switch (cmd.id) {
            case kSasCmdPass: {
                // [case 0] FUN_18001bf80 for the named pass.
                SasPass* pass = SasFindPassByIndex(tech, cmd.value);
                if (pass != nullptr) {
                    HRESULT hr = SasRunPassCommands(sas, tech, pass, device, state);
                    if (hr != S_OK) {
                        finalHr = hr;
                        // LABEL_13 early-out: latch the run-failed flag.
                        SasNoteRunFailureFirstCause(
                            sas, "Pass command loop",
                            static_cast<int>(state->commandIndex),
                            kSasCmdPass, hr);
                        sas->runFailed = true;
                        SasRestoreRunState(device, state);
                        return finalHr;
                    }
                }
                break;
            }
            case kSasCmdScriptExternal:
                if (stepMode) {
                    // [case 0xC, a4 = 1] store the next command index and
                    // return - the suspension point. The redirects stay
                    // bound so the host scene turn renders into them.
                    state->suspended = true;
                    if (suspendedOut != nullptr) {
                        *suspendedOut = true;
                    }
                    return S_OK;
                }
                // [a4 = 0] full mode: the scene render already happened
                // between the step and this resume - a no-op.
                break;
            case kSasCmdLoopByCount: {
                // [case 0xD] evaluate the count at runtime; <= 0 skips the
                // whole body, > 0 pushes a loop frame.
                int count = 0;
                if (!SasReadLoopParameterNow(effect, cmd.param, &count) ||
                    count <= 0) {
                    state->commandIndex = SasSkipLoopBody(cmds, state->commandIndex);
                    advance = false;
                    break;
                }
                SasLoopFrame frame;
                frame.commandIndex = static_cast<int>(state->commandIndex);
                frame.iteration = 0;
                frame.count = count;
                state->loopStack.push_back(frame);
                break;
            }
            case kSasCmdLoopGetIndex: {
                // [case 0xE 0x18001bd7c-0x18001bd94] the SetInt is
                // UNCONDITIONAL: v30 starts at 0 and only takes the top
                // frame's counter when the loop stack is non-empty - an empty
                // stack writes 0.
                int iteration = 0;
                if (!state->loopStack.empty()) {
                    iteration = state->loopStack.back().iteration;
                }
                effect->SetInt(cmd.param, iteration);
                break;
            }
            case kSasCmdLoopEnd: {
                // [case 0xF] advance the counter; pop when exhausted,
                // otherwise jump back to just after the LoopByCount entry.
                if (!state->loopStack.empty()) {
                    SasLoopFrame& frame = state->loopStack.back();
                    frame.iteration += 1;
                    if (frame.iteration >= frame.count) {
                        state->loopStack.pop_back();
                    } else {
                        state->commandIndex =
                            static_cast<unsigned int>(frame.commandIndex);
                        advance = true;  // the ++ below lands after LoopByCount
                        break;
                    }
                }
                break;
            }
            case kSasCmdDrawGeometry:
            case kSasCmdDrawBuffer:
                // Technique level: pass-script-only commands per REFERENCE.txt
                // (FUN_18001bbc0's default case skips them).
                break;
            default: {
                if (cmd.id >= kSasCmdRenderColorTarget0 && cmd.id <= kSasCmdClearEx) {
                    // [cases 1..9] FUN_18001c760 against the shared state.
                    // On failure the walk goes STRAIGHT to the LABEL_13
                    // epilogue - no extra report here (the applier's own
                    // render-target report is the only one) - and the
                    // run-failed flag latches like every LABEL_13 exit with
                    // a nonzero result.
                    HRESULT hr = SasApplyCommandImpl(sas, cmd, device, state,
                                                     tech->handle, -1);
                    if (hr != S_OK) {
                        finalHr = hr;
                        SasNoteRunFailureFirstCause(
                            sas, "script command",
                            static_cast<int>(state->commandIndex),
                            cmd.id, hr);
                        sas->runFailed = true;
                        SasRestoreRunState(device, state);   // LABEL_13 early-out
                        return finalHr;
                    }
                }
                break;
            }
        }
        if (advance) {
            state->commandIndex += 1;
        }
    }

    // Script complete: the LABEL_13 epilogue (restore redirects + viewport).
    state->suspended = false;
    SasRestoreRunState(device, state);
    return finalHr;
}

}  // namespace

bool SasExecuteTechniqueStep(SasEffect* sas, SasTechnique* tech,
                             IDirect3DDevice9* device, SasRunState* state) {
    bool suspended = false;
    SasRunTechniqueWalk(sas, tech, device, state, true, &suspended);
    return suspended;
}

HRESULT SasResumeTechnique(SasEffect* sas, SasTechnique* tech,
                           IDirect3DDevice9* device, SasRunState* state) {
    return SasRunTechniqueWalk(sas, tech, device, state, false, nullptr);
}

// ===========================================================================
// Parent-facing execution wrapper (object-class techniques)
// ===========================================================================

void SasExecuteTechnique(SasEffect* sas, IDirect3DDevice9* device, int techIndex,
                         int subset) {
    if (sas == nullptr || device == nullptr || techIndex < 0 ||
        techIndex >= static_cast<int>(sas->techniques.size())) {
        return;
    }
    // [FUN_18001B940 0x18001b9ec `cmp byte ptr [rbx+39h], 0`] the object-draw
    // entry never walks a latched-failed script: the original falls straight
    // to the host slot-0 plain draw `(**a7)(a7, 0, 0)`. The port's draw
    // wrapper (callbacks.cpp) consumes the flag at its technique gate and
    // forwards the raw host draw - the same observable degradation - so this
    // entry only needs the walk-side skip.
    if (sas->runFailed) {
        return;
    }
    SasTechnique& t = sas->techniques[static_cast<size_t>(techIndex)];
    if (t.empty) {
        return;  // the "empty technique" convention suppresses drawing
    }
    if (!SasIsSubsetAllowed(sas, techIndex, subset)) {
        return;
    }
    SasRunState* state = SasCreateRunState(device);
    SasResumeTechnique(sas, &t, device, state);
    SasDestroyRunState(state);
}

// ===========================================================================
// Host geometry-draw record [FUN_18005a740, the ModelData+0x08 slot-0
// callback] - see the header note. Staged by the object draw path in
// callbacks.cpp around SasExecuteTechnique, consumed by the RunPass kind-0
// callback in pass_planner.cpp.
// ===========================================================================

static SasHostDrawRecord* g_hostDrawRecord = nullptr;

void SasSetHostDrawRecord(SasHostDrawRecord* record) {
    g_hostDrawRecord = record;
}

SasHostDrawRecord* SasHostDrawRecordCurrent() {
    return g_hostDrawRecord;
}

int SasFindTechniqueIndex(const SasEffect* sas, D3DXHANDLE handle) {
    if (sas == nullptr || handle == nullptr) {
        return -1;
    }
    for (size_t i = 0; i < sas->techniques.size(); ++i) {
        if (sas->techniques[i].handle == handle) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace mme
