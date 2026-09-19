// mme_globals.cpp - see mme_globals.h
#include "mme_globals.h"

#include <mmsystem.h>  // timeGetTime
#pragma comment(lib, "winmm.lib")

#include "effect_engine.h"
#include "mme_context.h"
#include "mme_log.h"
#include "mmhack_api.h"  // GetCurrentFrameTime / IsEditMode
#include "anime_texture.h"  // [Phase 4] MmeAnimeTick
#include "mme_dlg.h"
#include "mme_ui.h"         // [Phase 4] MmeUiInstallHooks / MmeUiTickAutoReload

namespace mme {

// --- parameter names (strings_evidence.md section 4; PE 0x1800b2a00..0x1800b2d10) ---
const char* const g_paramNames[kParamCount] = {
    "matWorld",          // 0x1800b2b20
    "matWorldViewProj",  // 0x1800b2b08
    "matLightViewProj",  // 0x1800b2af0
    "matRotate",         // 0x1800b2ae0
    "EgColor",           // 0x1800b2ad8
    "ToonColor",         // 0x1800b2d10
    "LightDir",          // 0x1800b2ac8
    "SpcColor",          // 0x1800b2ab8
    "Place",             // 0x1800b2aac
    "DifColor",          // 0x1800b2aa0
    "parthf",            // 0x1800b2a94
    "spadd",             // 0x1800b2a8c
    "transp",            // 0x1800b2a84
    "TexCAdd",           // 0x1800b2a18
    "TexCMul",           // 0x1800b2a10
    "SphCAdd",           // 0x1800b2a08
    "SphCMul",           // 0x1800b2a00
};

// --- module / file paths ---
std::string g_exeDir;             // DAT_1800d7550
std::string g_iniPath;            // DAT_1800d7578

// --- ini-derived / host flags ---
unsigned char g_emmAutoSave = 0;      // DAT_1800d72e0 (Initialize sets 1 first)
unsigned char g_skipValidation = 0;   // DAT_1800d99d9
unsigned char g_debugMode = 0;        // DAT_1800d99de

// --- host UI state ---
HWND         g_mainWindow = nullptr;               // DAT_1800d9b00
LONG_PTR     g_mainOriginalWndProc = 0;            // DAT_1800d9b08
HWND         g_offscreenWindow = nullptr;          // DAT_1800d9b10
LONG_PTR     g_offscreenOriginalWndProc = 0;       // DAT_1800d9b18
HMENU        g_menuState = nullptr;                // DAT_1800d9b20
HHOOK        g_cbtHook = nullptr;                  // DAT_1800d9b28
// DAT_1800d72e1: NOT BSS - it lives in the file-backed .data image (RVA
// 0xD72E1 -> raw file offset 0xD56E1) and is initialized to 0x01 there, so
// the original ships with auto-reload ON and needs no startup write. The only
// 4 code xrefs are the CheckMenuItem read (0x180055a30), the menu-40001
// toggle writes (0x180055bba = 0 / 0x180055bc9 = 1) and the poll read
// (0x18000b935); nothing at Initialize/DllMain touches it.
unsigned int g_autoReload = 1;                     // DAT_1800d72e1 (auto-reload default ON)
ExpGetEnglishModeFn g_englishModeFn = nullptr;     // DAT_1800d9bf8
unsigned char g_uiJapaneseFlag = 1;                // DAT_1800d99dd (Japanese default)

// --- singletons ---
MmeContext*         g_context = nullptr;      // DAT_1800d9bb8
EffectOwnerManager* g_ownerManager = nullptr; // DAT_1800d9a40

// --- effect engine COM state ---
ID3DXEffectPool*   g_effectPool = nullptr;       // DAT_1800d9a30
IDirect3DSurface9* g_offscreenSurface = nullptr; // DAT_1800d9a38
unsigned char      g_engineMipFilterOk = 0;      // DAT_1800d99da

// --- current effect + parameter handles ---
ID3DXEffect* g_currentEffect = nullptr;                      // DAT_1800d9918
D3DXHANDLE   g_paramHandles[kParamCount] = { 0 };            // DAT_1800d9b30..DAT_1800d9bb0

// --- cached render state ---
D3DLIGHT9    g_cachedLight;         // DAT_1800d9890
D3DMATRIX    g_worldAtBegin;        // DAT_1800d9d30
D3DMATRIX    g_invWorldAtBegin;     // DAT_1800d9d70
D3DMATRIX    g_worldViewMatrix;     // DAT_1800d9db0
D3DMATRIX    g_projMatrix;          // DAT_1800d9df0
D3DMATRIX    g_viewProjMatrix;      // DAT_1800d9e30
D3DMATRIX    g_lightViewMatrix;     // DAT_1800d9e70
D3DMATRIX    g_lightProjMatrix;     // DAT_1800d9eb0
D3DMATRIX    g_lightViewProjMatrix; // DAT_1800d9ef0

// --- per-frame flags ---
unsigned char g_insideModelDraw = 0;   // DAT_1800d99df

// --- frame timing ---
DWORD         g_timeBase = 0;          // DAT_1800d990c
DWORD         g_lastTick = 0;          // DAT_1800d9908
float         g_frameDelta = 0.0f;     // DAT_1800d98f8
float         g_lastFrameTime = 0.0f;  // DAT_1800d98fc
float         g_deltaSeconds = 0.0f;   // DAT_1800d9900
float         g_frameTimeBase = 0.0f;  // DAT_1800d9904
unsigned char g_timeInitialized = 0;   // DAT_1800d9911
unsigned char g_wasEditMode = 0;       // DAT_1800d9910

// --- begin-scene viewport + mouse capture (the FUN_180055b10 mouse tail) ---
D3DVIEWPORT9 g_beginViewport = { 0, 0, 0, 0, 0.0f, 0.0f };  // DAT_1800d9878
float g_mouseX = 0.0f;                    // DAT_1800d9bc0
float g_mouseY = 0.0f;                    // DAT_1800d9bc4
float g_mouseClickPos[3][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 } };  // DAT_1800d9bc8..bec
float g_mouseClickZ[3] = { 0, 0, 0 };     // DAT_1800d9bd0/be0/bf0
float g_mouseClickTime[3] = { 0, 0, 0 };  // DAT_1800d9bd4/be4/bf4

bool MmeIsEnglishUiMode()
{
    // [0x180058a20 L78-82] the localized-message resolution order: when the
    // host export was resolved at Initialize it wins; otherwise the stored
    // flag (1 = Japanese default) decides. Returns true for English UI.
    if (g_englishModeFn != nullptr) {
        return g_englishModeFn() != 0;
    }
    return g_uiJapaneseFlag == 0;
}

void MmeUpdateFrameTime()
{
    // [0x180056f80] FUN_180056f80 - full port.
    DWORD now = timeGetTime();
    float frameTime = GetCurrentFrameTime();   // MMHack
    if (g_timeInitialized == 0) {
        g_timeInitialized = 1;
        g_timeBase = now;
        g_wasEditMode = IsEditMode() ? 1 : 0;
        g_frameTimeBase = frameTime;
        if (g_wasEditMode != 0) {
            g_frameTimeBase = 0.0f;            // [L70287-70289]
        }
        g_frameDelta = 0.0f;
        g_deltaSeconds = 0.033333335f;         // [L70291]
    } else {
        bool editMode = IsEditMode() != 0;     // [L70294]
        if (!editMode) {
            if (g_wasEditMode == 0) {
                g_deltaSeconds = frameTime - g_frameTimeBase;
                g_frameTimeBase = frameTime;
            } else {
                g_deltaSeconds = static_cast<float>(now - g_lastTick) / 1000.0f;
                g_frameTimeBase = frameTime;
            }
        } else {
            if (g_wasEditMode == 0) {
                g_timeBase = now;
            }
            g_deltaSeconds = static_cast<float>(now - g_lastTick) / 1000.0f;
            g_frameTimeBase = static_cast<float>(now - g_timeBase) / 1000.0f;
        }
        g_frameDelta = frameTime - g_lastFrameTime;   // [L70312]
    }
    g_lastFrameTime = frameTime;               // [L70314]
    g_wasEditMode = IsEditMode() ? 1 : 0;      // [L70315]
    g_lastTick = now;                          // [L70316]
}

void MmeInitEffectEngine(IDirect3DDevice9* device, bool mipFilterAvailable)
{
    // [0x18000a8e0] forwarded to effect_engine (pool + 16x16 target + mip
    // latch + cache teardown).
    MmeEngineInit(device, mipFilterAvailable);
}

// --- PHASE 2 SEAM REGISTRY --------------------------------------------------
// The former Phase 1 stub bodies were filled by the Phase 2 modules:
//   effect_engine.cpp  : MmeTeardownEffectEngine (as MmeEngineTerm)
//   pass_planner.cpp   : MmePassBookkeeping / MmeUpdatePassBookkeeping /
//                        MmeRunPostEffect / MmeAcquireBindingContext /
//                        MmeReportDrawError / MmeRebuildRenderPassPlan
//   emm_manager.cpp    : MmeAutoSaveEmmForPmm / MmeFindEffectFileForModel
//   material_bind.cpp  : MmeApplyModelRenderSnapshot (via render_snapshot.h)
// The remaining seams stay here.

void MmeTickAnimatedTextures(MmeContext* ctx)
{
    // [0x180001320] Phase 4: the ctx+0x240 animated-texture tick
    // (GDI+ frame decode + SetTexture; checks IsEditMode inside) plus the
    // mme_ui auto-reload poll (FUN_18000b880's 100 ms stamp check).
    MmeAnimeTick(ctx);
    MmeUiTickAutoReload();
    MmeDlgRefreshIfModelCountChanged();
}

bool MmeInstallUiHooks(unsigned char arg)
{
    // [0x180055890] Phase 4: menu install/English-mode swap, main-window
    // subclass with 0x180055b10, WH_CBT hook FUN_180055800, GdiplusStartup
    // (inside mme_ui.cpp), the log-mirror registration and the assignment
    // dialog bookkeeping.
    return MmeUiInstallHooks(arg != 0);
}

} // namespace mme
