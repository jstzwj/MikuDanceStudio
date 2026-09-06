// sas_exec.cpp - the SAS script compiler and runtime executor.
//
// Ports:
//   FUN_180018200 [0x180018200] SasCompileScript: normalize + tokenize the
//       script ("^\s*((\w+)\s*=\s*([^=;\s]*))\s*;\s*(.*)", [0x1800B46C8]),
//       lowercase the command token, dispatch, unroll loopbycount bodies.
//   FUN_18001bf80 [0x18001bf80] SasRunPassCommands: walk the compiled list,
//       apply commands 1..9 (FUN_18001c760) and run the draw=geometry /
//       draw=buffer pass execution (SetTechnique + host callbacks).
//   FUN_18001c760 [0x18001c760] command applier: render-target switching with
//       restore bookkeeping, clears, draws, ScriptExternal.
//   FUN_18001dd20 [0x18001dd20] SasRunTechniqueCommands: technique-level walk.
//   SasExecutePostEffect / SasExecuteTechnique: the parent-facing wrappers.
#include "sas_exec.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cctype>

// effect_engine.cpp exports this [0x180093660]; the script failure reporter
// uses it exactly like the original's "DirectX Error: <desc> [<hr>]" lines.
namespace mme {
const char* MmeDxErrDescription(unsigned long hr);
}

namespace mme {

// [FUN_180011960 L15276-15280] ctype do_tolower loop (per-TU static helper).
static std::string ToLowerAscii(const std::string& s) {
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>(tolower(static_cast<unsigned char>(out[i])));
    }
    return out;
}

// ===========================================================================
// Host callbacks (defaults = no-op draws; the parent overrides on wiring)
// ===========================================================================

static SasHostCallbacks g_host;

void SasSetHostCallbacks(const SasHostCallbacks& callbacks) {
    g_host = callbacks;
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

// ===========================================================================
// Script normalization [FUN_180018200 L19885-19941]
// ===========================================================================

// append ';' ; regex_replace("\\s*(;\\s*)+", ";") [DAT_1800b46b8] ;
// regex_replace("^;", "") [DAT_1800b3d2c].
static std::string SasNormalizeScriptExec(const std::string& in) {
    std::string s = in;
    s += ';';
    std::string out;
    out.reserve(s.size());
    bool lastWasSemi = false;
    bool anyTokenSeen = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (isspace(static_cast<unsigned char>(c))) {
            continue;
        }
        if (c == ';') {
            if (!lastWasSemi && anyTokenSeen) {
                out += ';';
            }
            lastWasSemi = true;
        } else {
            out += c;
            lastWasSemi = false;
            anyTokenSeen = true;
        }
    }
    if (!out.empty() && out[0] == ';') {
        out = out.substr(1);
    }
    return out;
}

// ===========================================================================
// FUN_180018200 - the compiler
// ===========================================================================

namespace {

struct SasCompiler {
    SasEffect* sas;
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

// Evaluate an int/bool scalar parameter
// ([0x1800B49B8] "Error: type of loop count must be 'int' or 'bool': ").
static bool SasReadLoopParameter(SasEffect* sas, D3DXHANDLE param, int* out) {
    ID3DXEffect* effect = sas->effect;
    D3DXPARAMETER_DESC pd;
    memset(&pd, 0, sizeof(pd));
    if (param == nullptr || effect->GetParameterDesc(param, &pd) != S_OK) {
        return false;
    }
    if (pd.Class == D3DXPC_SCALAR && pd.Type == D3DXPT_BOOL) {
        BOOL v = FALSE;
        if (effect->GetBool(param, &v) != S_OK) {
            return false;
        }
        *out = v ? 1 : 0;
        return true;
    }
    if (pd.Class == D3DXPC_SCALAR && pd.Type == D3DXPT_INT) {
        int v = 0;
        if (effect->GetInt(param, &v) != S_OK) {
            return false;
        }
        *out = v;
        return true;
    }
    return false;
}

// Compile one token into `out`. Mirrors the FUN_180018200 dispatch
// [L20330-21027].
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
            cmd.param = nullptr;  // reset to the backbuffer
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
        if ((t != 5 && t != 7) || pd.Semantic == nullptr ||
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
            // [0x1800B48A0] must be float4.
            if (pd.Class == D3DXPC_VECTOR && pd.Type == D3DXPT_FLOAT) {
                cmd.id = kSasCmdClearSetColor;
                cmd.param = param;
                out->push_back(cmd);
                ok = true;
            }
        }
        if (!ok) {
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
            // [0x1800B48D0] must be float.
            if (pd.Class == D3DXPC_SCALAR && pd.Type == D3DXPT_FLOAT) {
                cmd.id = kSasCmdClearSetDepth;
                cmd.param = param;
                out->push_back(cmd);
                ok = true;
            }
        }
        if (!ok) {
            SasLogFormat(c->sas, "Error: type of clear depth must be 'float': %s  %s\n",
                         value.c_str(), ctx.c_str());
            c->sas->hasErrors = true;
            return false;
        }
        return true;
    }

    // --- clear [L20839-20847] ---
    if (command == "clear") {
        if (value == "color") {
            cmd.id = kSasCmdClear;
            cmd.value = D3DCLEAR_TARGET;
            out->push_back(cmd);
            return true;
        }
        if (value == "depth") {
            cmd.id = kSasCmdClear;
            cmd.value = D3DCLEAR_ZBUFFER;
            out->push_back(cmd);
            return true;
        }
        // [0x1800B4910] "Warning: unknown clear type: %s"
        SasLogFormat(c->sas, "Warning: unknown clear type: %s  %s\n", whole.c_str(),
                     ctx.c_str());
        return true;  // warning only, not an error
    }

    // --- scriptexternal [L20871-20873] ---
    if (command == "scriptexternal") {
        if (value == "color") {
            cmd.id = kSasCmdScriptExternal;
            out->push_back(cmd);
            return true;
        }
        SasLogFormat(c->sas, "Warning: unknown script external type: %s  %s\n",
                     whole.c_str(), ctx.c_str());
        return true;
    }

    // --- loopbycount / loopgetindex / loopend [L20895-20969] ---
    if (command == "loopbycount") {
        D3DXHANDLE param = effect->GetParameterByName(nullptr, value.c_str());
        int count = 0;
        if (param == nullptr || !SasReadLoopParameter(c->sas, param, &count)) {
            SasLogFormat(c->sas,
                         "Error: type of loop count must be 'int' or 'bool': %s  %s\n",
                         value.c_str(), ctx.c_str());
            c->sas->hasErrors = true;
            return false;
        }
        cmd.id = kSasCmdLoopByCount;
        cmd.param = param;
        cmd.value = count;
        out->push_back(cmd);
        return true;
    }
    if (command == "loopgetindex") {
        D3DXHANDLE param = effect->GetParameterByName(nullptr, value.c_str());
        int dummy = 0;
        if (param == nullptr || !SasReadLoopParameter(c->sas, param, &dummy)) {
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
    if (command == "loopend") {
        cmd.id = kSasCmdLoopEnd;
        out->push_back(cmd);
        return true;
    }

    // --- draw [L20973-21027] ---
    if (command == "draw") {
        if (value == "geometry") {
            cmd.id = kSasCmdDrawGeometry;
            c->sas->drawsGeometry = true;  // [L21003] sas+0x3c flag
            out->push_back(cmd);
            return true;
        }
        if (value == "buffer") {
            cmd.id = kSasCmdDrawBuffer;
            out->push_back(cmd);
            return true;
        }
        SasLogFormat(c->sas, "Warning: unknown draw type: %s  %s\n", whole.c_str(),
                     ctx.c_str());
        return true;
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

// The tokenizer loop [FUN_180018200 L19956-21052]. loopbycount bodies are
// unrolled at compile time: the body tokens are replayed `count` times with
// each loopgetindex target set to 0..count-1 (SetInt), which is the
// observable equivalent of the original's re-tokenizing loop state machine.
static bool SasCompileTokenizer(SasCompiler* c, const std::string& script,
                                std::vector<SasScriptCmd>* out) {
    std::string rest = script;
    struct LoopFrame {
        std::vector<std::string> body;  // raw "cmd=value" token texts
        int count;
    };
    std::vector<LoopFrame> loops;

    while (!rest.empty()) {
        // regex ^\s*((\w+)\s*=\s*([^=;\s]*))\s*;\s*(.*)
        size_t eq = rest.find('=');
        size_t semi = rest.find(';');
        if (eq == std::string::npos || semi == std::string::npos || eq > semi) {
            break;
        }
        std::string whole = rest.substr(0, semi);
        std::string command = ToLowerAscii(rest.substr(0, eq));
        std::string value = rest.substr(eq + 1, semi - eq - 1);
        std::string remainder = (semi + 1 <= rest.size()) ? rest.substr(semi + 1) : "";
        rest = remainder;
        if (command.empty()) {
            continue;
        }

        if (command == "loopbycount") {
            D3DXHANDLE param = c->sas->effect->GetParameterByName(nullptr, value.c_str());
            int count = 0;
            if (param == nullptr || !SasReadLoopParameter(c->sas, param, &count)) {
                SasLogFormat(c->sas,
                             "Error: type of loop count must be 'int' or 'bool': "
                             "%s  %s\n",
                             value.c_str(), c->ContextSuffix().c_str());
                c->sas->hasErrors = true;
                return false;
            }
            LoopFrame frame;
            frame.count = count;
            loops.push_back(frame);
            continue;
        }
        if (command == "loopend") {
            if (loops.empty()) {
                SasLogFormat(c->sas, "Error: script syntax error: %s  %s\n",
                             whole.c_str(), c->ContextSuffix().c_str());
                c->sas->hasErrors = true;
                return false;
            }
            LoopFrame frame = loops[loops.size() - 1];
            loops.pop_back();
            for (int i = 0; i < frame.count; ++i) {
                for (size_t t = 0; t < frame.body.size(); ++t) {
                    const std::string& bodyToken = frame.body[t];
                    size_t beq = bodyToken.find('=');
                    if (beq == std::string::npos) {
                        continue;
                    }
                    std::string bcmd = ToLowerAscii(bodyToken.substr(0, beq));
                    std::string bval = bodyToken.substr(beq + 1);
                    if (bcmd == "loopgetindex") {
                        // Set the index parameter for this iteration
                        // [0x1800B4AC4/0x1800B4AC8 "%d"/"=" build].
                        D3DXHANDLE param = c->sas->effect->GetParameterByName(
                            nullptr, bval.c_str());
                        if (param != nullptr) {
                            c->sas->effect->SetInt(param, i);
                        }
                        continue;
                    }
                    if (bcmd == "loopbycount" || bcmd == "loopend") {
                        continue;  // UNCERTAIN: nested loops flatten here
                    }
                    SasCompileToken(c, bcmd, bval, bodyToken, out);
                }
            }
            continue;
        }

        if (!loops.empty()) {
            loops[loops.size() - 1].body.push_back(whole);
            continue;
        }

        SasCompileToken(c, command, value, whole, out);
    }
    return true;
}

}  // namespace

bool SasCompileScript(SasEffect* sas, const std::string& script,
                      D3DXHANDLE technique, D3DXHANDLE pass,
                      const std::string& techniqueName, const std::string& passName,
                      std::vector<SasScriptCmd>* out) {
    (void)technique;
    (void)pass;
    if (sas == nullptr || out == nullptr) {
        return false;
    }
    out->clear();
    SasCompiler c;
    c.sas = sas;
    c.techniqueName = techniqueName;
    c.passName = passName;
    c.hasPass = (pass != nullptr);

    std::string normalized = SasNormalizeScriptExec(script);
    if (normalized.empty()) {
        return true;
    }
    bool ok = SasCompileTokenizer(&c, normalized, out);
    if (!ok) {
        sas->hasErrors = true;
    }
    return ok;
}

// ===========================================================================
// FUN_18001c760 - command applier
// ===========================================================================

// Run-state bookkeeping: the surfaces replaced by render target commands, so
// the runner can restore them afterwards (the original keeps them in its ctx
// object at param_4+8+slot*8 with a redirected bitmask at +4).
struct SasRunContext {
    IDirect3DSurface9* savedColor[4];
    IDirect3DSurface9* savedDepth;
    unsigned int redirectedMask;
    D3DXVECTOR4 clearColor;
    float clearDepth;
    bool clearColorValid;
    bool clearDepthValid;
    SasRunContext() {
        for (int i = 0; i < 4; ++i) {
            savedColor[i] = nullptr;
        }
        savedDepth = nullptr;
        redirectedMask = 0;
        clearColor = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
        clearDepth = 1.0f;
        clearColorValid = false;
        clearDepthValid = false;
    }
    ~SasRunContext() { Release(); }
    void Release() {
        for (int i = 0; i < 4; ++i) {
            if (savedColor[i] != nullptr) {
                savedColor[i]->Release();
                savedColor[i] = nullptr;
            }
        }
        if (savedDepth != nullptr) {
            savedDepth->Release();
            savedDepth = nullptr;
        }
    }
};

static SasResource* SasFindResource(SasEffect* sas, D3DXHANDLE param) {
    for (size_t i = 0; i < sas->resources.size(); ++i) {
        if (sas->resources[i].param == param) {
            return &sas->resources[i];
        }
    }
    return nullptr;
}

// The per-command applier (FUN_18001c760 switch, ids 1..9 + 12).
static HRESULT SasApplyCommandImpl(SasEffect* sas, const SasScriptCmd& cmd,
                                   IDirect3DDevice9* device, SasRunContext* ctx,
                                   D3DXHANDLE techniqueHandle, int passIndex) {
    ID3DXEffect* effect = sas->effect;

    switch (cmd.id) {
        case kSasCmdRenderColorTarget0:
        case kSasCmdRenderColorTarget1:
        case kSasCmdRenderColorTarget2:
        case kSasCmdRenderColorTarget3: {
            int slot = cmd.id - kSasCmdRenderColorTarget0;
            IDirect3DSurface9* target = nullptr;
            if (cmd.param != nullptr) {
                SasResource* res = SasFindResource(sas, cmd.param);
                if (res == nullptr) {
                    return D3DERR_INVALIDCALL;
                }
                target = res->surface;  // level 0 [FUN_18001c760 L22236-22256]
            }
            unsigned int bit = 1u << slot;
            if ((ctx->redirectedMask & bit) == 0) {
                device->GetRenderTarget(static_cast<UINT>(slot), &ctx->savedColor[slot]);
                ctx->redirectedMask |= bit;
            }
            return device->SetRenderTarget(static_cast<UINT>(slot), target);
        }
        case kSasCmdRenderDepthTarget: {
            IDirect3DSurface9* target = nullptr;
            if (cmd.param != nullptr) {
                SasResource* res = SasFindResource(sas, cmd.param);
                if (res == nullptr) {
                    return D3DERR_INVALIDCALL;
                }
                target = res->surface;
            }
            if ((ctx->redirectedMask & 0x80000000u) == 0) {
                device->GetDepthStencilSurface(&ctx->savedDepth);
                ctx->redirectedMask |= 0x80000000u;
            }
            return device->SetDepthStencilSurface(target);
        }
        case kSasCmdClearSetColor: {
            D3DXVECTOR4 v(0.0f, 0.0f, 0.0f, 0.0f);
            if (effect->GetVector(cmd.param, &v) != S_OK) {
                return D3DERR_INVALIDCALL;
            }
            ctx->clearColor = v;
            ctx->clearColorValid = true;
            return S_OK;
        }
        case kSasCmdClearSetDepth: {
            float f = 0.0f;
            if (effect->GetFloat(cmd.param, &f) != S_OK) {
                return D3DERR_INVALIDCALL;
            }
            ctx->clearDepth = f;
            ctx->clearDepthValid = true;
            return S_OK;
        }
        case kSasCmdClear: {
            DWORD flags = static_cast<DWORD>(cmd.value);
            D3DCOLOR color = D3DCOLOR_ARGB(255, 0, 0, 0);
            if ((flags & D3DCLEAR_TARGET) != 0) {
                if (ctx->clearColorValid) {
                    color = D3DCOLOR_COLORVALUE(ctx->clearColor.x, ctx->clearColor.y,
                                                ctx->clearColor.z, ctx->clearColor.w);
                } else if (g_host.GetClearColor != nullptr) {
                    color = g_host.GetClearColor(g_host.ctx);
                }
            }
            float depth = 1.0f;
            if ((flags & D3DCLEAR_ZBUFFER) != 0) {
                depth = ctx->clearDepthValid
                            ? ctx->clearDepth
                            : (g_host.GetClearDepth != nullptr
                                   ? g_host.GetClearDepth(g_host.ctx)
                                   : 1.0f);
            }
            // [FUN_18001c760] one flag set per "clear=" command instance.
            return device->Clear(0, nullptr, flags, color, depth, 0);
        }
        case kSasCmdScriptExternal: {
            // [Tips (2)] preprocess + objects + other post effects render into
            // the current target. Default no-op; the parent hooks it up.
            if (g_host.ScriptExternalColor != nullptr) {
                return g_host.ScriptExternalColor(g_host.ctx, sas);
            }
            return S_OK;
        }
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
            return S_OK;  // loop ids are compile-time artifacts
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
}

HRESULT SasRunPassCommands(SasEffect* sas, SasTechnique* tech, SasPass* pass,
                           IDirect3DDevice9* device, int passIndex,
                           unsigned int* redirectedMask) {
    if (sas == nullptr || device == nullptr) {
        return E_POINTER;
    }
    SasRunContext ctx;
    HRESULT finalHr = S_OK;
    for (size_t i = 0; i < pass->cmds.size(); ++i) {
        HRESULT hr = SasApplyCommandImpl(sas, pass->cmds[i], device, &ctx,
                                         tech != nullptr ? tech->handle : nullptr,
                                         passIndex);
        if (hr != S_OK) {
            SasReportError(sas, tech, pass, hr);
            finalHr = hr;
            break;
        }
    }
    if (redirectedMask != nullptr) {
        *redirectedMask = ctx.redirectedMask;
    }
    return finalHr;  // ctx dtor restores the redirected targets + releases
}

HRESULT SasRunTechniqueCommands(SasEffect* sas, SasTechnique* tech,
                                IDirect3DDevice9* device, int passIndexFilter) {
    if (sas == nullptr || device == nullptr || tech == nullptr) {
        return E_POINTER;
    }
    HRESULT finalHr = S_OK;
    ID3DXEffect* effect = sas->effect;
    effect->SetTechnique(tech->handle);

    // Technique-level commands run in order; draw entries execute the passes.
    SasRunContext ctx;
    for (size_t i = 0; i < tech->cmds.size(); ++i) {
        const SasScriptCmd& cmd = tech->cmds[i];
        if (cmd.id == kSasCmdDrawGeometry || cmd.id == kSasCmdDrawBuffer) {
            int kind = (cmd.id == kSasCmdDrawGeometry) ? 0 : 1;
            for (size_t p = 0; p < tech->passes.size(); ++p) {
                SasPass& pass = tech->passes[p];
                if (passIndexFilter >= 0 && pass.index != passIndexFilter) {
                    continue;
                }
                if (pass.hasScript) {
                    unsigned int mask = 0;
                    HRESULT hr = SasRunPassCommands(sas, tech, &pass, device,
                                                    pass.index, &mask);
                    if (hr != S_OK) {
                        finalHr = hr;
                    }
                } else if (g_host.RunPass != nullptr) {
                    // [FUN_18001bf80 no-script path L21793-21795]
                    HRESULT hr = g_host.RunPass(g_host.ctx, sas, kind, pass.index);
                    if (hr != S_OK) {
                        finalHr = hr;
                    }
                }
            }
            continue;
        }
        HRESULT hr = SasApplyCommandImpl(sas, cmd, device, &ctx, tech->handle,
                                         passIndexFilter);
        if (hr != S_OK) {
            SasReportError(sas, tech, nullptr, hr);
            finalHr = hr;
        }
    }
    return finalHr;
}

// ===========================================================================
// Parent-facing execution wrappers
// ===========================================================================

// Post-effect techniques drive the screen: their technique script contains a
// draw=buffer entry (the REFERENCE.txt post-effect pattern).
static bool SasIsPostTechnique(const SasTechnique& t) {
    for (size_t i = 0; i < t.cmds.size(); ++i) {
        if (t.cmds[i].id == kSasCmdDrawBuffer) {
            return true;
        }
    }
    return false;
}

void SasExecutePostEffect(SasEffect* sas, IDirect3DDevice9* device,
                          int passIndex) {
    if (sas == nullptr || device == nullptr || !sas->postEffect) {
        return;
    }
    for (size_t i = 0; i < sas->techniques.size(); ++i) {
        SasTechnique& t = sas->techniques[i];
        if (!SasIsPostTechnique(t)) {
            continue;
        }
        HRESULT hr = SasRunTechniqueCommands(sas, &t, device, passIndex);
        if (hr != S_OK) {
            // [0x1800B4AA0] "Failed to run the effect script:\n" banner.
            SasLogLine(sas, "Failed to run the effect script:\n");
        }
    }
}

void SasExecuteTechnique(SasEffect* sas, IDirect3DDevice9* device, int techIndex,
                         int subset) {
    if (sas == nullptr || device == nullptr || techIndex < 0 ||
        techIndex >= static_cast<int>(sas->techniques.size())) {
        return;
    }
    SasTechnique& t = sas->techniques[static_cast<size_t>(techIndex)];
    if (t.empty) {
        return;  // the "empty technique" convention suppresses drawing
    }
    if (!SasIsSubsetAllowed(sas, techIndex, subset)) {
        return;
    }
    HRESULT hr = SasRunTechniqueCommands(sas, &t, device, -1);
    if (hr != S_OK) {
        SasLogLine(sas, "Failed to run the effect script:\n");
    }
}

}  // namespace mme
