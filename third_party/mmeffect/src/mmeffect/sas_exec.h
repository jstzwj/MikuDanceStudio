// sas_exec.h - internal shared model of the SAS interpreter and the
// script compile/execute API (FUN_180018200 / FUN_18001bf80 / FUN_18001c760 /
// FUN_18001dd20 ports). Included by sas_interpreter.cpp and sas_exec.cpp;
// public consumers only need sas_interpreter.h (SasEffect stays opaque there
// but the definition below is what makes the two translation units wireable).
//
// Evidence notes on the original layout (FUN_18000c470 locals):
//   - sas+0x00 : ID3DXEffect*
//   - sas+0x30 : scriptClass (0/1/2)   sas+0x34 : scriptOrder (0/1/2)
//   - sas+0x38 : error flag            sas+0x48/0x70 : log std::string
//   - sas+0x88 : technique map (FUN_18001f1a0 keyed by handle)
//   - sas+0xb8 : technique handle order vector (param_1+0x17)
//   - sas+0xd8 : pass record map (FUN_18001f2d0 keyed by pass handle)
//   - sas+0xf8 : technique handle vector (param_1+0x1f..0x21)
//   - sas+0x1b8: resource/texture object vector (param_1+0x37)
// The std containers below replace the original's raw MSVC layouts
// (behavioral equivalence, documented divergence - see
// PHASE3_IMPLEMENTATION_NOTES.md).
#ifndef MME_SAS_EXEC_H_
#define MME_SAS_EXEC_H_

#include <d3d9.h>
#include <d3dx9.h>

#include <map>
#include <string>
#include <vector>

#include "sas_interpreter.h"

namespace mme {

// Script command ids as compiled by FUN_180018200. The numeric values follow
// the original's runtime dispatch (FUN_18001bf80 L21766-21781: ids 1..9 are
// applied by FUN_18001c760's switch(cmd-1); 10/11 are the SetTechnique +
// host-draw variants; the loop ids exist only inside the compiler).
enum SasCommandId {
    kSasCmdRenderColorTarget0 = 1,   // "rendercolortarget0" / "rendercolortarget"
    kSasCmdRenderColorTarget1 = 2,   // "rendercolortarget1"
    kSasCmdRenderColorTarget2 = 3,   // "rendercolortarget2"
    kSasCmdRenderColorTarget3 = 4,   // "rendercolortarget3"
    kSasCmdRenderDepthTarget = 5,    // "renderdepthstenciltarget"
    kSasCmdClearSetColor = 6,        // "clearsetcolor"
    kSasCmdClearSetDepth = 7,        // "clearsetdepth"
    kSasCmdClear = 8,                // "clear" (color|depth value)
    // NOTE: MME 0.37 has no "Pass=" script command; passes execute through the
    // draw entries (FUN_180018200 L21000-21027) and the plain no-script path
    // of FUN_18001bf80. "renderport" compiles to a no-op (accepted and
    // ignored, L20976-20981).
    kSasCmdDrawGeometry = 10,        // "draw=geometry" -> SetTechnique + host
                                     //   standard-geometry pass draw (vtbl[0]);
                                     //   sets the sas+0x3c geometry-draw flag
    kSasCmdDrawBuffer = 11,          // "draw=buffer"  -> SetTechnique + host
                                     //   screen-buffer pass draw (vtbl[1])
    kSasCmdScriptExternal = 12,      // "scriptexternal" (color)
    kSasCmdLoopByCount = 13,         // "loopbycount" (unrolled at compile time)
    kSasCmdLoopGetIndex = 14,        // "loopgetindex"
    kSasCmdLoopEnd = 15              // "loopend"
};

// One compiled script command. The original stores 0x18-byte records
// {int id, ...tail..., D3DXHANDLE param} in a vector inside the technique /
// pass record (FUN_18001bf80 walks them with a +0x18 stride).
struct SasScriptCmd {
    int        id = 0;      // SasCommandId
    int        value = 0;   // id-dependent (clear flag / pass index / 0)
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
    D3DFORMAT   format = D3DFMT_UNKNOWN;   // from "string Format" ("A8R8G8B8"
                                        // / "D24S8" defaults per REFERENCE.txt)
    float       clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float       clearDepth = 1.0f;
    bool        antiAlias = false;
    bool        hasClearColor = false;
    bool        hasClearDepth = false;
    std::string description;
    std::string defaultEffect;
    std::string resourceName;   // ANIMATEDTEXTURE "ResourceName" (Phase 3b)
    float       offset = 0.0f;  // ANIMATEDTEXTURE "Offset"
    float       speed = 1.0f;   // ANIMATEDTEXTURE "Speed"
    std::string seekVariable;   // ANIMATEDTEXTURE "SeekVariable"
    std::string textureValueName;  // TEXTUREVALUE "TextureName" (Phase 3b)
    IDirect3DBaseTexture9* texture = nullptr;  // created by SasEnsureResource
    IDirect3DSurface9*     surface = nullptr;  // level 0 / face 0 surface
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
    bool        useTexture = false;
    bool        useSpheremap = false;
    bool        useToon = false;
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
    bool          drawsGeometry = false; // sas+0x3c "script contains
                                         // draw=geometry" marker
    int           scriptClass = kSasClassObject;    // sas+0x30
    int           scriptOrder = kSasOrderStandard;  // sas+0x34
    bool          postEffect = false;    // scene/postprocess combination seen
    std::vector<D3DXHANDLE>  techniqueOrder;  // sas+0xb8 (Script= order first)
    std::vector<SasTechnique> techniques;
    std::vector<SasResource>  resources;       // sas+0x1b8
};

// --- compile API (FUN_180018200 port, called by the scan in
// sas_interpreter.cpp once the technique/pass records exist) ---

// Normalize (append ';', strip whitespace, collapse ';;'), tokenize with the
// original's "^((\\w+)=([^=;]*));(.*)" loop, unroll loopbycount bodies and
// store the command list into `out`. techniqueName/passName are the error
// context ("(technique: X)" / "(pass: Y, technique: X)"). Returns false when
// a hard error was logged (the original keeps compiling but flags the effect).
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

// --- execute API ---

// [FUN_18001bf80] run one pass's compiled command list (the pass draw itself
// happens through the host RunPass callback). `redirectedMask` receives the
// render-target redirect bookkeeping (bit 0..3 = SetRenderTarget slots,
// bit 31 = depth stencil).
HRESULT SasRunPassCommands(SasEffect* sas, SasTechnique* tech, SasPass* pass,
                           IDirect3DDevice9* device, int passIndex,
                           unsigned int* redirectedMask);

// [FUN_18001dd20] run a technique-level command list (draw=geometry /
// draw=buffer entries forward to the pass draw or the host callbacks).
HRESULT SasRunTechniqueCommands(SasEffect* sas, SasTechnique* tech,
                                IDirect3DDevice9* device, int passIndexFilter);

}  // namespace mme

#endif  // MME_SAS_EXEC_H_
