// sas_interpreter.h - public interface of the MMEffect SAS interpreter
// (Standard Attachments and Semantics effect-script engine).
//
// Reconstruction of the MMEffect.dll (MikuMikuEffect 0.37, x64) SAS region
// 0x18000C000-0x18001E000. Evidence:
//   - FUN_18000c470 [0x18000c470] STANDARDSGLOBAL scan + parameter validation
//   - FUN_18000f3a0 [0x18000f3a0] semantic -> resource-object dispatch
//   - FUN_180011960 [0x180011960] resource object build (ResourceType/format/
//     dimensions/clear annotations of RENDERCOLORTARGET /
//     RENDERDEPTHSTENCILTARGET / OFFSCREENRENDERTARGET / ANIMATEDTEXTURE)
//   - FUN_1800143d0 [0x1800143d0] texture creation + hardware capability checks
//   - FUN_1800169d0 [0x1800169d0] per-technique scan (MmdPass/UseTexture/
//     UseSpheremap/UseToon/Subset/Script annotations)
//   - FUN_180017a80 [0x180017a80] per-pass scan (shader version mix, PSIZE15,
//     pass Script annotation)
//   - FUN_180018200 [0x180018200] script compiler / command executor
//     (implemented in sas_exec.cpp; see sas_exec.h)
//
// This header is self-contained: it needs only <d3d9.h>, <d3dx9.h>, <string>.
// The parent (effect_engine.cpp) calls SasParse right after
// D3DXCreateEffectFromFileW succeeds and SasUnload on unload; the pass planner
// queries the technique/pass model and drives SasExecutePostEffect.
#ifndef MME_SAS_INTERPRETER_H_
#define MME_SAS_INTERPRETER_H_

#include <d3d9.h>
#include <d3dx9.h>

#include <string>

namespace mme {

struct SasEffect;  // opaque parsed representation (defined in sas_exec.h)

// [0x18000c470 L12055-12139] STANDARDSGLOBAL "ScriptClass" annotation values.
// Stored at sas+0x30 in the original (0=object, 1=scene, 2=sceneorobject).
enum SasScriptClass {
    kSasClassObject = 0,        // "object"        (default)
    kSasClassScene = 1,         // "scene"
    kSasClassSceneOrObject = 2  // "sceneorobject"
};

// [0x18000c470 L12190-12274] STANDARDSGLOBAL "ScriptOrder" annotation values.
// Stored at sas+0x34 in the original (0=standard, 1=preprocess, 2=postprocess).
enum SasScriptOrder {
    kSasOrderStandard = 0,     // "standard"
    kSasOrderPreprocess = 1,   // "preprocess"
    kSasOrderPostprocess = 2   // "postprocess"
};

// [0x1800169d0 L18599-18693] technique "MmdPass" annotation values (the record
// field in the original holds object=0, object_ss=1, shadow=2, zplot=3/4 - the
// decompile shows two distinct numeric encodings for the zplot-like entry;
// UNCERTAIN: the original distinguishes a 4-char pass-mode string stored as 3
// from "zplot" stored as 4. This port normalizes both to kSasPassZplot).
enum SasMmdPass {
    kSasPassObject = 0,    // "object"    - plain object draw
    kSasPassObjectSS = 1,  // "object_ss" - object draw while self-shadow is on
    kSasPassShadow = 2,    // "shadow"    - shadow map draw
    kSasPassZplot = 3      // "zplot"     - z-plot draw
};

// Parse (validate + model) an effect after D3DXCreateEffectFromFileW
// succeeded. Port of FUN_18000c470 (+ FUN_1800169d0/FUN_180017a80 scans that
// build the technique/pass model). Every error/warning the original appends to
// its per-effect log string (sas+0x70) is appended to the returned object's
// log; retrieve it with SasGetLog. Returns nullptr when the effect is
// unusable (invalid SAS version / bad ScriptClass / ScriptOrder / a hard
// parameter error), mirroring the original's failure path at 0x18000ea41.
//   effect    - the live ID3DXEffect (annotation reads go to the real object)
//   pathAnsi  - effect file path as requested from the loader (log context)
//   device    - device the effect was created against (texture creation)
SasEffect* SasParse(ID3DXEffect* effect, const std::string& pathAnsi,
                    IDirect3DDevice9* device);

// [FUN_18000b210 unload path] release the parsed representation. Does NOT
// release the ID3DXEffect (owned by effect_engine).
void SasUnload(SasEffect* sas);

// Does this effect define post-effect (screen) passes? True when the
// STANDARDSGLOBAL scan saw ScriptClass "scene"/"sceneorobject" with
// ScriptOrder "postprocess". Drives the pass_planner N+1 repeat decision
// (MME_RebuildRenderPassPlan tail).
bool SasHasPostEffect(const SasEffect* sas);

// ScriptClass/ScriptOrder accessors (sas+0x30 / sas+0x34 in the original).
SasScriptClass SasGetScriptClass(const SasEffect* sas);
SasScriptOrder SasGetScriptOrder(const SasEffect* sas);

// The accumulated parse log (English lines with trailing '\n', exactly the
// strings the original appended at sas+0x70 - "Error: SAS version is
// invalid...", "Warning: unknown technique name: ..." etc.). The parent
// writes this into MMEffect.txt. Never returns nullptr; empty when clean.
const char* SasGetLog(const SasEffect* sas);

// True when the parse recorded at least one "Error: ..." line
// (the original's per-effect error flag at sas+0x38).
bool SasHadErrors(const SasEffect* sas);

// --- technique/pass model enumeration (FUN_1800169d0 / FUN_180017a80) ---

struct SasPassInfo {
    const char* name;         // pass name (GetPassDesc)
    D3DXHANDLE  handle;       // pass handle
    int         index;        // pass index inside the technique
    bool        hasScript;    // pass carries a "Script" annotation
    bool        hasCustomShaders;  // pass sets VertexShader/PixelShader
    bool        needsPsize15; // VS declares input PSIZE15 [0x180017a80 L19128]
};

struct SasTechniqueInfo {
    const char* name;             // technique name (GetTechniqueDesc)
    D3DXHANDLE  handle;
    int         mmdPass;          // SasMmdPass (kSasPassObject when absent)
    bool        useTexture;       // "UseTexture" annotation
    bool        useSpheremap;     // "UseSpheremap" annotation
    bool        useToon;          // "UseToon" annotation
    bool        hardwareOk;       // ID3DXEffect::ValidateTechnique returned S_OK
    bool        shaderMixOk;      // vs_3_0/ps_3_0 not mixed with other SMs
    bool        hasScript;        // technique carries a "Script" annotation
    bool        empty;            // zero passes ("empty technique" convention,
                                  // REFERENCE.txt Tips - suppresses the draw)
    int         passCount;        // GetTechniqueDesc.Passes
};

// Techniques in the order MME uses them: the STANDARDSGLOBAL
// "Script=Technique=A?B:C" order first (FUN_18000c470 LAB_18000d3d0 block),
// then any remaining techniques in declaration order (the initial
// GetTechnique(i) enumeration loop).
int SasGetTechniqueCount(const SasEffect* sas);
bool SasGetTechniqueInfo(const SasEffect* sas, int index, SasTechniqueInfo* out);
bool SasGetPassInfo(const SasEffect* sas, int techIndex, int passIndex,
                    SasPassInfo* out);

// Subset restriction of a technique ("Subset" annotation, e.g. "0-4,7").
// Returns true when `subset` may be drawn with `techIndex`.
bool SasIsSubsetAllowed(const SasEffect* sas, int techIndex, int subset);

// --- execution (implemented in sas_exec.cpp) ---

// Execute the post-effect chain for one repeat iteration (the OnEndScene
// path): runs the postprocess technique's compiled script - render target
// switches, ClearSetColor/ClearSetDepth/Clear, ScriptExternal=Color,
// loopbycount (unrolled at compile time), Pass=/Draw=Buffer execution.
//   passIndex - pass of the postprocess technique to draw for the final
//               "Pass=" step (-1 = every pass with a script, the default).
// The clearSetColor/clearDepth defaults come from the host (MMHack
// GetClearColor / the MME context); register them with SasSetHostCallbacks.
void SasExecutePostEffect(SasEffect* sas, IDirect3DDevice9* device,
                          int passIndex);

// Execute one technique's script + passes for object-class effects (the
// OnDrawIndexedPrimitive-side entry the parent may use after wiring).
void SasExecuteTechnique(SasEffect* sas, IDirect3DDevice9* device,
                         int techIndex, int subset);

// Host callbacks for the steps that live outside the SAS module. All are
// optional; the defaults implement the D3D-visible behavior directly.
struct SasHostCallbacks {
    void* ctx;  // opaque host context, passed back verbatim
    // [FUN_18001bf80 L21775-21781] the original invokes a vtable'd host object
    // (slot 0 / slot 1) after SetTechnique to actually draw the pass: slot 0
    // = the MMD-standard-shader pass draw, slot 1 = the effect-pass draw.
    // Return S_OK (0) on success; nonzero fails the script with the
    // "DirectX Error:" reporter. kind: 0 = standard-shader draw, 1 = effect
    // pass draw (BeginPass/EndPass + fullscreen quad for post effects).
    long (*RunPass)(void* ctx, SasEffect* sas, int kind, int passIndex);
    // [Tips: (2) ScriptExternal=Color] render preprocess effects + objects +
    // other post effects into the currently bound render target. Default:
    // no-op returning S_OK (the parent's pass planner owns scene rendering).
    long (*ScriptExternalColor)(void* ctx, SasEffect* sas);
    // Default clear color/z when a technique clears without ClearSetColor
    // (MMHack GetClearColor / host state).
    unsigned long (*GetClearColor)(void* ctx);
    float (*GetClearDepth)(void* ctx);
};

// Install the host callbacks (pass_planner wiring point). Passing a
// default-constructed struct restores the built-ins.
void SasSetHostCallbacks(const SasHostCallbacks& callbacks);

}  // namespace mme

#endif  // MME_SAS_INTERPRETER_H_
