// mme_globals.h - the file-scope state of MMEffect.dll and the Phase 2+ seams.
//
// Every DAT_1800xxxxx global cited by exports_callmap.md /
// globals_structures.md lives here with its original address. The container
// shapes are std equivalents (behavioral equivalence, documented divergence).
#pragma once

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>

#include <string>

namespace mme {

class MmeContext;
class ModelData;
class EffectOwnerManager;
struct RenderSnapshot;

// --- parameter handle indexes (resolution order of 0x1800564a0 L203-221) ---
enum MmeParamIndex {
    kParamMatWorld = 0,          // DAT_1800d9b30
    kParamMatWorldViewProj,      // DAT_1800d9b38
    kParamMatLightViewProj,      // DAT_1800d9b40
    kParamMatRotate,             // DAT_1800d9b48
    kParamEgColor,               // DAT_1800d9b50
    kParamToonColor,             // DAT_1800d9b58
    kParamLightDir,              // DAT_1800d9b60
    kParamSpcColor,              // DAT_1800d9b68
    kParamPlace,                 // DAT_1800d9b70
    kParamDifColor,              // DAT_1800d9b78
    kParamParthf,                // DAT_1800d9b80
    kParamSpadd,                 // DAT_1800d9b88
    kParamTransp,                // DAT_1800d9b90
    kParamTexCAdd,               // DAT_1800d9b98
    kParamTexCMul,               // DAT_1800d9ba0
    kParamSphCAdd,               // DAT_1800d9ba8
    kParamSphCMul,               // DAT_1800d9bb0
    kParamCount                  // 17 handles
};
extern const char* const g_paramNames[kParamCount];  // strings_evidence.md section 4

// --- module / file paths (globals_structures.md section 8) ---
extern std::string g_exeDir;             // DAT_1800d7550 (<exedir> of MikuMikuDance.exe)
extern std::string g_iniPath;            // DAT_1800d7578 (<exedir>\MMEffect.ini)

// --- ini-derived / host flags ---
extern unsigned char g_emmAutoSave;      // DAT_1800d72e0 [System] EMMAutoSave (default 1)
extern unsigned char g_skipValidation;   // DAT_1800d99d9 [System] SkipValidation
extern unsigned char g_debugMode;        // DAT_1800d99de IsDebugMode()

// --- host UI state (Phase 2 mme_ui owns the real installation) ---
extern HWND         g_mainWindow;               // DAT_1800d9b00
extern LONG_PTR     g_mainOriginalWndProc;      // DAT_1800d9b08
extern HWND         g_offscreenWindow;          // DAT_1800d9b10
extern LONG_PTR     g_offscreenOriginalWndProc; // DAT_1800d9b18
extern HMENU        g_menuState;                // DAT_1800d9b20 (the installed "MMEffect" popup menu)
extern HHOOK        g_cbtHook;                  // DAT_1800d9b28 (WH_CBT, FUN_180055800)
extern unsigned int g_autoReload;               // DAT_1800d72e1 auto-reload toggle; file-backed .data init 0x01 (RVA 0xD72E1 -> file off 0xD56E1), so default ON - not a BSS zero

typedef int (__cdecl *ExpGetEnglishModeFn)(void);
extern ExpGetEnglishModeFn g_englishModeFn;     // DAT_1800d9bf8 GetProcAddress(GetModuleHandleA(NULL), "ExpGetEnglishMode")
extern unsigned char g_uiJapaneseFlag;          // DAT_1800d99dd (inverted English mode; default 1 = Japanese)
bool MmeIsEnglishUiMode();                      // [0x180058a20 L78-82] resolution order used by OnResetDevice/IniFile

// --- singletons ---
extern MmeContext*         g_context;           // DAT_1800d9bb8
extern EffectOwnerManager* g_ownerManager;      // DAT_1800d9a40 (declared in mme_context.h; defined here)

// --- effect engine COM state (FUN_18000a8e0) ---
extern ID3DXEffectPool*    g_effectPool;        // DAT_1800d9a30 (D3DXCreateEffectPool)
extern IDirect3DSurface9*  g_offscreenSurface;  // DAT_1800d9a38 (CreateRenderTarget 16x16)
extern unsigned char       g_engineMipFilterOk; // DAT_1800d99da (GetSamplerState(1,7) result)

// --- current effect + 17 parameter handles ---
extern ID3DXEffect* g_currentEffect;                    // DAT_1800d9918 (GetCurrentEffect cache)
extern D3DXHANDLE   g_paramHandles[kParamCount];        // DAT_1800d9b30..DAT_1800d9bb0

// --- cached render state (globals_structures.md section 7) ---
extern D3DLIGHT9    g_cachedLight;         // DAT_1800d9890 (GetLight(0))
extern D3DMATRIX    g_worldAtBegin;        // DAT_1800d9d30 (D3DTS_WORLD at BeginScene)
extern D3DMATRIX    g_invWorldAtBegin;     // DAT_1800d9d70 (D3DXMatrixInverse; the "view^-1" global)
extern D3DMATRIX    g_worldViewMatrix;     // DAT_1800d9db0 (world * view)
extern D3DMATRIX    g_projMatrix;          // DAT_1800d9df0 (D3DTS_PROJECTION)
extern D3DMATRIX    g_viewProjMatrix;      // DAT_1800d9e30 ((world*view) * proj)
extern D3DMATRIX    g_lightViewMatrix;     // DAT_1800d9e70 (GetLightViewProjMatrix out 1)
extern D3DMATRIX    g_lightProjMatrix;     // DAT_1800d9eb0 (out 2)
extern D3DMATRIX    g_lightViewProjMatrix; // DAT_1800d9ef0 (out 3)

// --- per-frame flags ---
extern unsigned char g_insideModelDraw;    // DAT_1800d99df (cleared in OnBeginScene, set in OnDrawIndexedPrimitive)

// --- frame timing (FUN_180056f80 globals) ---
extern DWORD         g_timeBase;         // DAT_1800d990c
extern DWORD         g_lastTick;         // DAT_1800d9908
extern float         g_frameDelta;       // DAT_1800d98f8
extern float         g_lastFrameTime;    // DAT_1800d98fc
extern float         g_deltaSeconds;     // DAT_1800d9900
extern float         g_frameTimeBase;    // DAT_1800d9904
extern unsigned char g_timeInitialized;  // DAT_1800d9911
extern unsigned char g_wasEditMode;      // DAT_1800d9910

// --- begin-scene viewport + mouse capture (the FUN_180055b10 subclass tail;
//     captured NDC coordinates feed the MME mouse standard parameters) ---
extern D3DVIEWPORT9 g_beginViewport;     // DAT_1800d9878 (GetViewport at BeginScene)
extern float g_mouseX;                   // DAT_1800d9bc0 (NDC x of the cursor)
extern float g_mouseY;                   // DAT_1800d9bc4 (NDC y, y-up)
extern float g_mouseClickPos[3][2];      // DAT_1800d9bc8/bcc (left), bd8/bdc (right), be8/bec (middle)
extern float g_mouseClickZ[3];           // DAT_1800d9bd0/be0/bf0 (1.0 while down, 0 after up)
extern float g_mouseClickTime[3];        // DAT_1800d9bd4/be4/bf4 (g_frameTimeBase at down)

// [0x180056f80] per-frame time bookkeeping (full port; OnBeginScene).
void MmeUpdateFrameTime();

// [0x18000a8e0] effect-pool/engine init: forwards to effect_engine's
// MmeEngineInit (teardown, D3DXCreateEffectPool, CreateRenderTarget(16,16,
// fmt 0x16), mip-filter latch). Called by Initialize with the
// GetSamplerState(1,7) query result.
void MmeInitEffectEngine(IDirect3DDevice9* device, bool mipFilterAvailable);

// ---------------------------------------------------------------------------
// PHASE 2 SEAM REGISTRY (status after the Phase 2 fill)
// ---------------------------------------------------------------------------
// The Phase 1 stubs below moved to their owning Phase 2 modules; the
// declarations now live with the implementations:
//   MmeTeardownEffectEngine      -> effect_engine (MmeEngineTerm core)
//   MmePassBookkeeping           -> pass_planner   [0x18005d130]
//   MmeUpdatePassBookkeeping     -> pass_planner   [0x18005c510]
//   MmeRunPostEffect             -> pass_planner   [0x18005e210]
//   MmeAcquireBindingContext     -> pass_planner   [0x180055690]
//   MmeReportDrawError           -> pass_planner   [0x18005da50]
//   MmeAutoSaveEmmForPmm         -> emm_manager    [0x180057290]
//   MmeFindEffectFileForModel    -> emm_manager    (discovery, no logging)
//   MmeApplyModelRenderSnapshot  -> material_bind  [0x18005a1e0]
//   MmeRebuildRenderPassPlan     -> pass_planner   [0x18005b9e0]
// Remaining stubs (later phases), still declared here:
//   MmeTickAnimatedTextures      [0x180001320] (anime_texture, Phase 2/4)
//   MmeInstallUiHooks            [0x180055890] (mme_ui, Phase 4)

// [0x180001320] animated-texture tick on ctx+0x240 (Phase 2 anime_texture).
void MmeTickAnimatedTextures(MmeContext* ctx);

// [0x180055b10/0x180055800/0x180055890 cluster] host UI install: menu install,
// main-window subclass, WH_CBT hook, GdiplusStartup (GdiplusStartup lives in
// this address cluster per subsystems.md). PHASE 2 seam (mme_ui). Returns
// true (success) so Initialize proceeds.
bool MmeInstallUiHooks(unsigned char arg);

} // namespace mme
