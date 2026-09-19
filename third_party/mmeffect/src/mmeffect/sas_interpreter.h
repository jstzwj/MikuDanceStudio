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
// queries the technique/pass model and drives the step/resume walk.
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

// [0x1800169d0 L18599-18693] technique "MmdPass" annotation values
// (0x1800B456C: object=0, object_ss=1, shadow=2, edge=3, zplot=4).
enum SasMmdPass {
    kSasPassObject = 0,    // "object"    - plain object draw
    kSasPassObjectSS = 1,  // "object_ss" - object draw while self-shadow is on
    kSasPassShadow = 2,    // "shadow"    - shadow map draw
    kSasPassEdge = 3,      // "edge"      - outline draw
    kSasPassZplot = 4      // "zplot"     - z-plot draw
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
                    IDirect3DDevice9* device, std::string* outFailureLog = nullptr);

// [FUN_18000b210 unload path] release the parsed representation. Does NOT
// release the ID3DXEffect (owned by effect_engine).
void SasUnload(SasEffect* sas);

// Device-reset hook (PHASE3 note #7): drop every D3DPOOL_DEFAULT resource
// (offscreen render targets / depth stencils) and re-create it against the
// reset device, re-binding the texture parameters.
void SasRecreateResources(SasEffect* sas, IDirect3DDevice9* device);

// Lost-device half for resources created in D3DPOOL_DEFAULT. Must run before
// IDirect3DDevice9::Reset; recreation alone after Reset is too late.
void SasReleaseDeviceResources(SasEffect* sas);

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
    // Use* filters. The original bytes default to 0xFF = "wildcard"
    // (sub_1800169D0 0x180016a41 *(v4+4)=-256; consumed as SIGNED by the
    // technique selector sub_18001DB50 0x18001dc65-0x18001dcb1: negative
    // matches any material state). The bools here report "nonzero" -
    // true for an explicit Use*=true AND for the no-annotation default.
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

// Techniques in the order MME uses them: every technique in declaration
// order (the initial GetTechnique(i) enumeration loop), REPLACED by ONLY
// the STANDARDSGLOBAL "Script=Technique=A?B:C" names when a valid Script
// annotation is present (FUN_18000c470: vector clear at 0x18000d072,
// then the LAB_18000d3d0 block pushes the listed names back).
int SasGetTechniqueCount(const SasEffect* sas);
bool SasGetTechniqueInfo(const SasEffect* sas, int index, SasTechniqueInfo* out);
bool SasGetPassInfo(const SasEffect* sas, int techIndex, int passIndex,
                    SasPassInfo* out);

// Subset restriction of a technique ("Subset" annotation, e.g. "0-4,7").
// Returns true when `subset` may be drawn with `techIndex`.
bool SasIsSubsetAllowed(const SasEffect* sas, int techIndex, int subset);

// [sub_18001DB50 0x18001DB50-0x18001DD1F] the technique selector. One query
// = (draw mode, subset, material Use* state):
//   drawMode     - SasMmdPass value 0..4 (object/object_ss/shadow/edge/zplot)
//   subset       - material subset index; negative selects nothing
//   useTexture/useSpheremap/useToon - the CURRENT material's state flags
// (FUN_18001b940's snapshot+0x56 / 0<sphere_mode / snapshot+0x54).
// Iterates the technique order vector (sas+0xF8: every technique in
// declaration order, or ONLY the STANDARDSGLOBAL Script=Technique names
// when a valid Script annotation replaced the list at 0x18000d072) and
// returns the
// FIRST technique whose MmdPass == drawMode, whose Subset ranges contain
// `subset`, and whose Use* bytes match (0xFF = wildcard, matches either
// state; an explicit annotation byte requires equality with the material
// state). A technique that passes those filters but fails the validity check
// - [System] SkipValidation=false: the ValidateTechnique result (record+8);
// true: the vs_3_0/ps_3_0 mix check (record+9) [byte_1800D99D9 select at
// 0x18001dcb3] - is skipped and remembered; when NO valid technique matched,
// the LAST remembered invalid one is returned as a fallback (the original
// still draws through it). *validMatch receives the "found" byte (false only
// for the invalid fallback; true for a valid match and for the no-match
// null). Returns D3DXHANDLE; nullptr when nothing matched.
D3DXHANDLE SasSelectTechnique(const SasEffect* sas, int drawMode, int subset,
                              bool useTexture, bool useSpheremap, bool useToon,
                              bool* validMatch);

// --- execution (implemented in sas_exec.cpp) ---
//
// Scene/postprocess techniques execute through the RUNTIME state machine
// (FUN_18001bbc0 port) with a persisted run state: the pass planner STEPS a
// technique to the ScriptExternal suspension (the scene turn then renders
// into the targets the script bound) and RESUMES it after the turn to run
// the lighting/composite passes. The run-state API (SasRunState /
// SasCreateRunState / SasExecuteTechniqueStep / SasResumeTechnique) is
// declared in sas_exec.h, which the engine sources include.

// Execute one technique's script + passes for object-class effects (a fresh
// run state, full run, restore) - the object-draw-side entry.
void SasExecuteTechnique(SasEffect* sas, IDirect3DDevice9* device, int techIndex,
                         int subset);

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
