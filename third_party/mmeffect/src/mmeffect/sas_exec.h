// sas_exec.h - internal shared model of the SAS interpreter and the
// script compile/execute API (FUN_180018200 / FUN_18001bbc0 / FUN_18001bf80 /
// FUN_18001c760 / FUN_18001b7b0 ports). Included by sas_interpreter.cpp and
// sas_exec.cpp; public consumers only need sas_interpreter.h (SasEffect stays
// opaque there but the definition below is what makes the two translation
// units wireable).
//
// Evidence notes on the original layout (FUN_18000c470 locals):
//   - sas+0x00 : ID3DXEffect*
//   - sas+0x30 : scriptClass (0/1/2)   sas+0x34 : scriptOrder (0/1/2)
//   - sas+0x38 : error flag            sas+0x48/0x70 : log std::string
//   - sas+0x88 : technique map (FUN_18001f1a0 keyed by handle)
//   - sas+0xf8 : technique handle order vector - every technique in
//     declaration order (filled 0x18000c513-0x18000c598), REPLACED by only
//     the STANDARDSGLOBAL Script= names when a valid Script annotation
//     clears it at 0x18000d072. The selector sub_18001DB50 iterates this
//     vector (a1[31]/a1[32]) and nothing else; sas+0xb8 is not referenced
//     on the SAS parse path.
//   - sas+0xd8 : pass record map (FUN_18001f2d0 keyed by pass handle)
//   - sas+0x1b8: resource/texture object vector (param_1+0x37)
// The std containers below replace the original's raw MSVC layouts
// (behavioral equivalence, documented divergence - see
// PHASE3_IMPLEMENTATION_NOTES.md).
//
// Script execution model (FUN_18001bbc0, verified against the v0.37 x64
// binary): techniques execute through a RUNTIME state machine, not a compile
// -time unrolled list. The command walk keeps a persisted run state (command
// index + loop stack + saved render targets), so a technique can SUSPEND at
// ScriptExternal (case 0xC, single-step mode a4=1: store the next command
// index and return) while the host renders the scene into the targets the
// pre-suspension commands bound, then RESUME later (a4=0) to run the post
// -scene half. LoopByCount/LoopGetIndex/LoopEnd (ids 13/14/15) execute at
// runtime with a loop stack and read the live parameter values, and "Pass="
// compiles to an id-0 entry like the original (the compiler at 0x180018200
// references the token "pass" and the error "Error: unknown pass name: ").
#ifndef MME_SAS_EXEC_H_
#define MME_SAS_EXEC_H_

#include <d3d9.h>
#include <d3dx9.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "sas_interpreter.h"

namespace mme {

// --- process-wide depth-target registry [qword_1800D9C48] ---
// The original keeps ONE global std::map<IDirect3DBaseTexture9*,
// IDirect3DSurface9*> for the whole process. Every RenderDepthStencilTarget
// (semantic 0x27) resource registers globalMap[texture] = surface at
// creation time [FUN_180011960 0x1800140e4-0x180014109: gated on
// semanticId == 0x27 && record texture != null, via sub_18001EC90 =
// operator[] + store], re-registers after a device-reset rebuild
// [sub_180014270 0x180014326 / sub_180016660 0x18001672a: the same
// insert-or-overwrite for every rebuilt 0x27 default-pool record], and
// ERASES the entry keyed by the record's CURRENT texture at effect unload
// [FUN_18000B210 case 39: v21[0] = record texture, sub_18001ED20 = the
// map's equal-range erase, BEFORE sub_18000B660 releases the D3D objects].
// The depth redirect then consults ONLY this registry, BY TEXTURE POINTER,
// across effect boundaries [FUN_18001C760 case 5 0x18001d35a-0x18001d38e:
// sub_18001EF30 = count(key) - zero hits fail with the synthetic code 1;
// sub_18001EC90 = operator[] - a hit whose surface is null binds NULL
// without error], so an effect may redirect its depth target into a depth
// texture ANOTHER effect created (the cross-effect G-buffer depth share
// published effects rely on). A texture that never registered (color
// targets, offscreen targets, plain file textures) fails with code 1.
void SasRegisterDepthSurface(IDirect3DBaseTexture9* texture,
                             IDirect3DSurface9* surface);
void SasUnregisterDepthSurface(IDirect3DBaseTexture9* texture);
// Returns whether `texture` is registered at all; *surface receives the
// registered surface (possibly null - a null surface is a legitimate
// "bind NULL" outcome, not a miss).
bool SasFindDepthSurface(IDirect3DBaseTexture9* texture,
                         IDirect3DSurface9** surface);


// Script command ids as compiled by FUN_180018200 and dispatched by
// FUN_18001bbc0 (technique walk) / FUN_18001bf80 (pass walk).
enum SasCommandId {
    kSasCmdPass = 0,                 // "pass=Name" -> run that pass now
                                     //   (value = the pass's index; the
                                     //   original stores the pass handle in
                                     //   the id-0 entry's param slot)
    kSasCmdRenderColorTarget0 = 1,   // "rendercolortarget0" / "rendercolortarget"
    kSasCmdRenderColorTarget1 = 2,   // "rendercolortarget1"
    kSasCmdRenderColorTarget2 = 3,   // "rendercolortarget2"
    kSasCmdRenderColorTarget3 = 4,   // "rendercolortarget3"
    kSasCmdRenderDepthTarget = 5,    // "renderdepthstenciltarget"
    kSasCmdClearSetColor = 6,        // "clearsetcolor"
    kSasCmdClearSetDepth = 7,        // "clearsetdepth"
    kSasCmdClear = 8,                // "clear" (value = D3DCLEAR_* flags).
                                     //   The original instead compiles
                                     //   clear=color to record id 8 and
                                     //   clear=depth to id 9 (FUN_180018200
                                     //   0x180019907 / 0x180019926) with the
                                     //   flags hardcoded per id in the
                                     //   executor; this port folds both into
                                     //   id 8 and carries the flags in value.
    kSasCmdClearEx = 9,              // FUN_18001c760 case 9 = the original's
                                     //   clear=depth record (Clear flags 6 =
                                     //   ZBUFFER|STENCIL, color 0, staged z,
                                     //   stencil 0 via sub_18005AEE0). Never
                                     //   compiled by this port - clear=depth
                                     //   rides kSasCmdClear with value =
                                     //   D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL.
    kSasCmdDrawGeometry = 10,        // "draw=geometry" -> SetTechnique + host
                                     //   standard-geometry pass draw (vtbl[0]);
                                     //   sets the sas+0x3c geometry-draw flag
    kSasCmdDrawBuffer = 11,          // "draw=buffer"  -> SetTechnique + host
                                     //   screen-buffer pass draw (vtbl[1])
    kSasCmdScriptExternal = 12,      // "scriptexternal" (color) - the
                                     //   suspension point in step mode
    kSasCmdLoopByCount = 13,         // "loopbycount" (count read at runtime)
    kSasCmdLoopGetIndex = 14,        // "loopgetindex"
    kSasCmdLoopEnd = 15              // "loopend"
};

// One compiled script command. The original stores 0x18-byte records
// {int id, ...tail..., D3DXHANDLE param} in a vector inside the technique /
// pass record (FUN_18001bf80 walks them with a +0x18 stride).
struct SasScriptCmd {
    int        id = 0;      // SasCommandId (0 requires an explicit push - a
                            // default-constructed record is never executed)
    int        value = 0;   // id-dependent: clear flags / pass index / RT
                            //   element index ("tex[3]" -> cube face 3, -1 =
                            //   no index, matching the original's record
                            //   field at +0x10)
    D3DXHANDLE param = 0;   // parameter the command references (0 = none)
    std::string text;       // raw value text (error context / clear type)
};

// Texture resource object (FUN_180011960 record + FUN_1800143d0 creation).
// Covers RENDERCOLORTARGET / RENDERDEPTHSTENCILTARGET /
// OFFSCREENRENDERTARGET / ANIMATEDTEXTURE / plain textures (semantic id 0x2c)
// and TEXTUREVALUE holders (Phase 3b fills the texel arrays).
struct SasResource {
    D3DXHANDLE  param = 0;
    std::string name;
    int         semanticId = 0;      // 0x26/0x27/0x2c/0x2d/0x2e/0x33
    int         textureType = 0;     // D3DXPT_TEXTURE / _2D / _3D / _CUBE
    int         reqWidth = 0;        // resolved request (viewport ratio applied)
    int         reqHeight = 0;
    int         reqDepth = 0;
    int         mipLevels = 1;
    // [sub_180011960 0x1800123bb-0x1800125ee / 0x1800129bf] the RAW
    // "Dimensions"/"Width"/"Height" triple and "Miplevels"/"Levels" value
    // as stored in the original record (+0x58/+0x5C/+0x6C): every component
    // defaults to -1 (= D3DX_DEFAULT) and negative annotation values are
    // normalized back to -1. sub_1800143D0's file-texture path
    // (0x180015195, D3DXCreateTextureFromFileExA) passes them verbatim.
    int         texWidth = -1;
    int         texHeight = -1;
    int         texMips = -1;
    // [sub_180014270 0x1800142e7] the reset-time rebuild only touches
    // records whose cached desc flag at +100 is set (= created in
    // D3DPOOL_DEFAULT). MANAGED file textures survive the reset.
    bool        defaultPool = false;
    D3DFORMAT   format = D3DFMT_UNKNOWN;   // from "string Format" (defaults:
                                        // A8R8G8B8; 0x27 takes the current
                                        // depth-stencil format, D24S8 fallback)
    // [FUN_180011960 0x2E block, offscreen record +0x10/+0x14/+0x18..0x1A]
    // ClearColor is read as a 4-component vector, clamped per component to
    // [0,1] and packed ONCE at parse time into a D3DCOLOR (A=int(w*255)<<24 |
    // R=u8(x*255)<<16 | G=u8(y*255)<<8 | B=u8(z*255)). ClearDepth stores the
    // single float verbatim (no clamping). The original never initializes
    // +0x14 when the annotation is absent yet still clears Z with it (see
    // MmeApplyPassRecord); a fresh MSVC small-heap page reads as 0.0f, so the
    // port pins that effective default.
    unsigned long clearColorArgb = 0;   // +0x10 packed ARGB
    float       clearDepth = 0.0f;      // +0x14 (annotation value; no clamp)
    bool        antiAlias = false;      // +0x1A "AntiAlias"
    bool        hasClearColor = false;  // +0x18 annotation present
    bool        hasClearDepth = false;  // +0x19 annotation present
    // [sub_1800143D0 case 46, offscreen record +8/+0x1C/+0x20] EVERY
    // OFFSCREENRENDERTARGET gets a depth stencil paired to its texture
    // (D3DFMT_D24S8, the created texture's own desc size/multisample,
    // Discard=FALSE) stored at offscreen+8; the AntiAlias annotation only
    // decides whether the CURRENT render target's MultiSampleType/Quality are
    // mirrored into +0x1C/+0x20 (the creation call keeps using the texture's
    // own desc values - textures cannot be multisampled, so the surface is
    // always created with D3DMULTISAMPLE_NONE).
    IDirect3DSurface9*     offscreenDepth = nullptr;  // +8 paired D24S8
    D3DMULTISAMPLE_TYPE    offscreenMsType = D3DMULTISAMPLE_NONE;  // +0x1C
    DWORD                  offscreenMsQuality = 0;               // +0x20
    std::string description;
    std::string defaultEffect;
    // [sub_180011960 0x180013301-0x1800135F4, offscreen record +0x78] the
    // DefaultEffect annotation parsed into ORDERED (key -> value) rows (the
    // original vector's 80-byte pair elements, appended in annotation order).
    // The key is the text before '=' trim()ed of " \t\v\r\n"; the value is
    // "none" / "hide" / "main_default" (compared case-insensitively after
    // trim, an empty value becoming "none") or the ABSOLUTE path of the
    // referenced effect file resolved against the .fx directory (a row whose
    // path cannot be opened fails the whole effect load - the original
    // destroys the offscreen record and returns failure).
    std::vector<std::pair<std::string, std::string>> defaultEffectMap;
    // Per-target assignments: path and visibility are independent, and a
    // material inherits the whole-object entry (-1) when it has no override.
    std::map<std::pair<unsigned long long, int>, std::string> effectOverrides;
    std::map<std::pair<unsigned long long, int>, bool> shownOverrides;
    std::string resourceName;   // ANIMATEDTEXTURE "ResourceName" (Phase 3b)
    float       offset = 0.0f;  // ANIMATEDTEXTURE "Offset"
    float       speed = 1.0f;   // ANIMATEDTEXTURE "Speed"
    std::string seekVariable;   // ANIMATEDTEXTURE "SeekVariable"
    // [sub_18000F3A0 case 45, 0x1800105xx] the parse-time-resolved handle of
    // the parameter named by "SeekVariable" (GetParameterByName; its Type
    // must be BOOL/INT/FLOAT). The per-frame consumer (sub_18001B5B0 case 45)
    // reads this parameter's live value through GetFloatArray(handle, &v, 1)
    // and feeds it as the animation TIME in place of the host clock.
    D3DXHANDLE  seekParam = 0;
    std::string textureValueName;  // TEXTUREVALUE "TextureName" (Phase 3b)
    // [sub_18000F3A0 case 51, 0x1800114xx] the parse-time-resolved handle of
    // the TEXTURE(5)/TEXTURE2D(7) parameter named by "TextureName", plus the
    // TextureValue parameter's Elements count. The per-frame consumer
    // (sub_18001B0A0 case 51 -> sub_180063AA0) reads the texture's first
    // min(elements, w*h) texels back into a float4 array and applies them
    // with SetVectorArray(param, values, elements). No D3D texture is ever
    // created for the 0x33 record itself (0x33 never reaches sub_1800143D0).
    D3DXHANDLE  textureValueParam = 0;
    int         textureValueElements = 0;
    // [FUN_180011960 0x180012F43/0x180012F7D, record +0x70/+0x98] the 0x2C
    // texture-generation path: "Function" names the shader entry point that
    // is compiled FROM THE EFFECT'S OWN .fx FILE (pSrcFile = the loader's
    // FullPath string at sas+0x48, NOT a separate file); "Target" is the
    // compile profile and defaults to "tx_1_0" when the annotation is absent
    // [0x180012FAD-0x180012FC3]. An empty Function means "not generated" -
    // the availability check consumes it (see sas_interpreter.cpp).
    std::string functionName;   // "Function" annotation (entry point name)
    std::string functionTarget; // "Target" annotation (profile; tx_1_0 default)
    IDirect3DBaseTexture9* texture = nullptr;  // created by SasEnsureResourceTexture
    IDirect3DSurface9*     surface = nullptr;  // level 0 / face 0 surface

    // [sub_18002CA80 0x18002d349-0x18002d3fe / FUN_18005c970 0x18005c9b6 /
    // FUN_18005cac0 gate WORD / FUN_18005e210 0x18005e41b] the render-turn
    // wrapper's two state bytes, carried on the 0x2E resource the wrapper
    // names (wrapper+0x3C/+0x3D): the repeat-boundary apply recomputes the
    // gate every turn as adaptive && AntiAlias && !failed, the post-effect
    // snapshot executor latches the failure bit once sub_180001880 fails for
    // this turn's wrapper (after which the gate stays 0 - the original keeps
    // the latch for the wrapper's mgr-lifetime). Per turn-id granularity:
    // one failing offscreen turn must not kill its siblings' gates.
    unsigned char passSnapshotGate = 0;    // wrapper+0x3C
    unsigned char passSnapshotFailed = 0;  // wrapper+0x3D (one-way latch)

    // [FUN_18000B210 case 39] the unload path erases the record's entry from
    // the process-wide depth registry (sub_18001ED20, keyed by the CURRENT
    // texture pointer) before releasing the D3D objects. The port fires the
    // same erase from the record destructor: a SasEffect dies only through
    // SasUnload's `delete sas`, so this runs exactly at the original's unload
    // point. INVARIANT: SasResource copies/moves only exist during the parse
    // build (SasBuildResourceObject's local + the vector's relocation) - at
    // that stage texture is still null (SasEnsureResourceTexture fills the
    // vector elements IN PLACE afterwards and nothing appends to
    // sas->resources after that), so only the final owner ever carries a
    // non-null texture here and no premature erase can fire. Like the
    // original, the 0x27 create path erases the record's CURRENT
    // depth-registry key before releasing/replacing the old surface
    // (sub_1800143D0 prologue 0x180014434-0x180014441), so no stale key
    // survives a device-reset rebuild; the port mirrors this with
    // SasUnregisterDepthSurface before the release.
    ~SasResource();
};

// One CONTROLOBJECT parameter collected at parse time (the original resolves
// these per frame through the ModelData "EffectFrameParamSetter" vtable,
// sub_180057BC0). `param` is the effect parameter handle; objectName/itemName
// come from the "Name"/"Item" string annotations [0x1800B40F8/0x1800B4100,
// case-insensitive; Name mandatory, Item optional].
struct SasControlObject {
    D3DXHANDLE  param = 0;
    std::string objectName;
    std::string itemName;
};

// Per-pass record (FUN_180017a80, stored in the original's pass map sas+0xd8).
struct SasPass {
    D3DXHANDLE  handle = 0;
    std::string name;
    std::string scriptText; // raw "Script" annotation before compilation
    int         index = 0;
    bool        hasScript = false;
    bool        needsPsize15 = false;      // VS input (usage 4, index 15)
    bool        hasCustomShaders = false;  // pass sets VS/PS (GetPassDesc)
    bool        shaderMixOk = true;        // vs_3_0/ps_3_0 consistency
    std::vector<SasScriptCmd> cmds;
};

// Per-technique record (FUN_1800169d0, technique map sas+0x88).
struct SasTechnique {
    D3DXHANDLE  handle = 0;
    std::string name;
    int         mmdPass = kSasPassObject;
    // [sub_1800169D0 0x180016a41] the record head is initialized with
    // *(v4+4) = -256: byte +4 (hasScript) clears to 0 while the three
    // Use* bytes (+5/+6/+7) default to 0xFF. The consumer
    // [sub_18001DB50 0x18001dc65-0x18001dcb1] compares them as SIGNED
    // bytes: negative (0xFF) = wildcard (the technique matches any
    // material state); an explicit UseTexture/UseSpheremap/UseToon
    // annotation stores 0/1 and filters by (flag != 0) == material-has-it.
    signed char useTexture = -1;   // 0xFF wildcard / annotation 0 or 1
    signed char useSpheremap = -1;
    signed char useToon = -1;
    bool        hardwareOk = true;   // ValidateTechnique() == S_OK
    bool        shaderMixOk = true;  // every pass consistent (FUN_1800167d0)
    bool        hasScript = false;
    bool        empty = false;       // zero passes
    bool        allSubsets = true;   // no "Subset" annotation
    std::map<int, int> subsets;      // min -> max inclusive ranges
    std::vector<SasPass> passes;
    std::vector<SasScriptCmd> cmds;  // technique-level script
};

// The parsed effect (opaque through sas_interpreter.h).
struct SasEffect {
    ID3DXEffect*  effect = nullptr;      // sas+0x00 (not AddRef'd; engine owns)
    IDirect3DDevice9* device = nullptr;  // captured from the parse call
    std::string   path;                  // loader path (log context)
    std::string   log;                   // sas+0x70 per-effect log text
    bool          hasErrors = false;     // sas+0x38 error flag
    // [sas+0x39, FUN_18001BBC0 tail 0x18001bdb7 `mov byte ptr [r12+39h], 1`]
    // the script RUN-FAILED latch: set when the technique walk ends with a
    // nonzero command result (FUN_18001BF80 pass walk / FUN_18001C760
    // applier failure - a failing RenderColorTarget/RenderDepthStencilTarget
    // bind, an unregistered depth texture, a failed host draw callback). A
    // step-mode suspension returns 0 early and does NOT latch. Consumers:
    // FUN_18001B940 (object draw: `effect && tech && !sas+0x39` else the host
    // slot-0 plain draw `(**a7)(a7,0,0)`), FUN_18001BAB0 (full-run scene
    // entry: flagged -> return without drawing at all), FUN_18005A410
    // (step: the bookkeeping still runs, only the walk is skipped),
    // FUN_18005A5C0 (resume: walk skipped, the run state still destroyed).
    // The latch is one-way for the effect's lifetime; the original clears it
    // only in FUN_18000B880 (the load/poll entry) where a set flag triggers a
    // FULL effect unload (FUN_18000B210) - i.e. a script error degrades the
    // draws until the effect is unloaded, never retried in place.
    bool          runFailed = false;
    bool          drawsGeometry = false; // sas+0x3c "script contains
                                         // draw=geometry" marker
    int           scriptClass = kSasClassObject;    // sas+0x30
    int           scriptOrder = kSasOrderStandard;  // sas+0x34
    bool          postEffect = false;    // scene/postprocess combination seen
    // sas+0xf8: every technique in declaration order; REPLACED by only the
    // STANDARDSGLOBAL Script= names (clear at 0x18000d072) when the Script
    // annotation is present and a string - NOT "Script= order first".
    std::vector<D3DXHANDLE>  techniqueOrder;
    std::vector<SasTechnique> techniques;
    std::vector<SasResource>  resources;       // sas+0x1b8
    std::vector<SasControlObject> controls;    // CONTROLOBJECT parameters
};

// --- compile API (FUN_180018200 port, called by the scan in
// sas_interpreter.cpp once the technique/pass records exist) ---

// Normalize (append ';', regex_replace "\\s*(;\\s*)+" -> ";" folding only
// the semicolon runs and their adjacent whitespace, then strip one leading
// ';'), tokenize with the original's "^\\s*((\\w+)\\s*=\\s*([^=;\\s]*))\\s*;\\s*(.*)"
// loop and store the flat command list into `out` (loops are NOT unrolled -
// they execute at runtime with the loop stack). Whitespace around '=' is
// legal; whitespace inside a command word or value is a syntax error
// ("Error: script syntax error: ..."). The command token is matched
// case-sensitively (FUN_180018200 memcmps it raw; only the VALUE gets the
// tolower for the enum compares). techniqueName/passName are the error
// context ("(technique: X)" / "(pass: Y, technique: X)").
//
// Returns TRUE when at least one hard error was logged (FUN_180018200's v29:
// the compile itself always runs to completion - a bad token never aborts
// the tokenizer; FUN_1800169D0/FUN_180017A80 OR v29 into their error
// returns). [原版事实，勿再错记] 编译错误会使加载被拒绝，而不是只跳过
// 后续扫描：sub_180016900（0x180016938-0x1800169b9）把 ScanTechnique/
// ScanPass 的任何非零 OR 成返回值 1，FUN_18000BC90（0x18000c423-0x18000c432）
// 在其非零时直接向上返回非零错误码（加载失败）——只有全部扫描通过
// 才会继续 sub_18001DD20，效果绝不会被带着脚本错误加载。
bool SasCompileScript(SasEffect* sas, const std::string& script,
                      D3DXHANDLE technique, D3DXHANDLE pass,
                      const std::string& techniqueName,
                      const std::string& passName,
                      std::vector<SasScriptCmd>* out);

// Append `line` to the effect log (the FUN_180006060 sas+0x70 append; the
// original also mirrors error lines into a message list - the parent shows
// them, see notes). Called by both translation units.
void SasLogLine(SasEffect* sas, const char* line);

// printf-style log append producing the identical line texts the original
// assembled from its string fragments (shared by both translation units).
void SasLogFormat(SasEffect* sas, const char* fmt, ...);

// --- run state (FUN_18001b7b0 ctor / FUN_180059a30 dtor; the original's
// 0x88-byte object persisted at ModelData+0x358 while a scene-effect
// technique is suspended at ScriptExternal) ---

// One loop-stack frame. `commandIndex` points at the LoopByCount entry; the
// jump-back target is commandIndex + 1 (FUN_18001bbc0 case 0xF stores the
// loop-start index and re-enters at start + 1).
struct SasLoopFrame {
    int commandIndex = 0;
    int iteration = 0;
    int count = 0;
};

struct SasRunState {
    unsigned int commandIndex = 0;            // +0x00 next command to run
    unsigned int redirectedMask = 0;          // +0x04 bit0 = depth,
                                              //      bits 1..4 = color 0..3
    IDirect3DSurface9* savedColor[4] = {nullptr, nullptr, nullptr, nullptr};
                                              // +0x08..0x20 (GetRenderTarget)
    IDirect3DSurface9* savedDepth = nullptr;  // +0x28
    // Currently redirected surfaces. These are borrowed from SasResource;
    // they let the repeat-boundary record apply re-assert the exact targets
    // selected before ScriptExternal, as MMEffect's record does.
    IDirect3DSurface9* activeColor[4] = {nullptr, nullptr, nullptr, nullptr};
    IDirect3DSurface9* activeDepth = nullptr;
    D3DVIEWPORT9 savedViewport = {};          // +0x30 (GetViewport)
    // +0x48 "current" viewport mirror. FUN_18001b7b0 initializes it as a full
    // copy of the saved viewport (0x18001b883-0x18001ba1); the RT0 redirect
    // update in FUN_18001c760 only rewrites X/Y/Width/Height (0x18001ca3b-
    // 0x18001ca51) leaving MinZ/MaxZ at whatever the current viewport holds,
    // and the "=;" reset copies the saved viewport back in full (0x18001c9f0-
    // 0x18001ca22). Every color/depth target switch re-asserts it through
    // SetViewport (0x18001ca74 / 0x18001d3eb).
    D3DVIEWPORT9 currentViewport = {};
    D3DCOLOR clearColor = 0;                  // +0x60 staged clear color
    float clearDepth = 1.0f;                  // +0x64 staged clear z
    bool clearColorStaged = false;            // ClearSetColor/ClearSetDepth ran
    bool clearDepthStaged = false;
    std::vector<SasLoopFrame> loopStack;      // +0x68 vector<SasLoopFrame>
    bool suspended = false;                   // stopped at ScriptExternal
};

// [FUN_18001b7b0] create the run state: save the current color targets 0..3,
// the depth stencil and the viewport; stage the host clear color / z=1.
SasRunState* SasCreateRunState(IDirect3DDevice9* device);

// [FUN_180059a30] destroy the run state (releases the saved surfaces; does
// NOT restore device state - the resume epilogue or the abandon path does).
void SasDestroyRunState(SasRunState* state);

// [FUN_18001bbc0 LABEL_13 epilogue] restore the redirected render targets,
// the depth stencil and the saved viewport; clear the redirect mask.
void SasRestoreRunState(IDirect3DDevice9* device, SasRunState* state);

// --- execute API (FUN_18001bbc0 port) ---

// Step mode (a4 = 1, FUN_18005a410): run the technique's commands from the
// saved position until ScriptExternal suspends the walk (returns true - the
// scene must render into the currently bound targets before the resume) or
// the script completes (returns false; the redirects are restored).
bool SasExecuteTechniqueStep(SasEffect* sas, SasTechnique* tech,
                             IDirect3DDevice9* device, SasRunState* state);

// Full mode (a4 = 0, FUN_18005a5c0): run from the saved position to
// completion, then restore the redirected targets / viewport. Safe on a fresh
// state (runs the whole script; ScriptExternal is a no-op in full mode).
HRESULT SasResumeTechnique(SasEffect* sas, SasTechnique* tech,
                           IDirect3DDevice9* device, SasRunState* state);

// [FUN_18001bf80] run one pass's compiled command list for a technique-level
// "Pass=Name" (id 0) entry: the pass's own script, or the default
// Draw=Geometry host draw for a script-less pass. Render-target redirects
// and clear staging live in the caller's run state (they persist for the
// whole technique execution, matching the original's shared context).
HRESULT SasRunPassCommands(SasEffect* sas, SasTechnique* tech, SasPass* pass,
                           IDirect3DDevice9* device, SasRunState* state);

// Execute one technique's script + passes for object-class effects (the
// object-draw-side entry: a fresh run state, full run, restore).
void SasExecuteTechnique(SasEffect* sas, IDirect3DDevice9* device, int techIndex,
                         int subset);

// --- host geometry-draw record [FUN_18005a740, the ModelData+0x08 slot-0
// callback] ---
// The original's walk invokes a vtable'd host object (slot 0 = draw the
// standard geometry, slot 1 = draw the screen buffer, slot 2 = clear). The
// slot-0 implementation lives inside ModelData: it replays the DrawIndexed-
// Primitive currently recorded at ModelData+0x138 (the snapshot the draw
// handler staged), wrapped in the effect sandwich SetTechnique (done by the
// caller, FUN_18001bf80) / Begin / BeginPass(passIndex) / DIP / EndPass /
// End. A recorded draw whose ModelData+0x160 valid handle is negative returns
// 0 WITHOUT drawing - the scene-effect carriers never record a draw, so
// Draw=Geometry inside a scene technique is a silent no-op there.
// The port replaces the embedded callback with this module-scope record: the
// object draw path (callbacks.cpp) stages the current DIP parameters around
// SasExecuteTechnique, and the RunPass kind-0 callback (pass_planner.cpp)
// consumes them. A null record is the original's "invalid DrawState" -> the
// callback returns 0 and nothing draws.
struct SasHostDrawRecord {
    IDirect3DDevice9* device = nullptr;   // device the DIP replays on
    unsigned int primitiveType = 0;       // D3DPRIMITIVETYPE
    int baseVertexIndex = 0;
    unsigned int minVertexIndex = 0;
    unsigned int vertexCount = 0;
    unsigned int startIndex = 0;
    unsigned int primitiveCount = 0;
};
// Stage the record for the technique walk that follows (nullptr clears it -
// always pair set/clear around one SasExecuteTechnique call).
void SasSetHostDrawRecord(SasHostDrawRecord* record);
// The staged record, or nullptr outside an object-draw walk.
SasHostDrawRecord* SasHostDrawRecordCurrent();

// [FUN_18001b940 table lookup -> technique object] resolve the technique
// index of a D3DXHANDLE in the SAS model's technique vector (-1 = unknown).
int SasFindTechniqueIndex(const SasEffect* sas, D3DXHANDLE handle);

}  // namespace mme

#endif  // MME_SAS_EXEC_H_
