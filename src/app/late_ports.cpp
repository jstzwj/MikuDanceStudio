// Application lifecycle helpers, renderer defaults and PMM migration dialog.
// Original addresses below identify the behavior reconstructed in each body.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>
#include <mmsystem.h> // timeEndPeriod

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <locale.h>

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// Full-port callees defined in other TUs (local declarations).
void TeardownDShowGraph(DShowRecorder* rec); // VA 0x409320 (shutdown_cleanup.cpp)
void RefreshAfterFrameApply(MMDApp* app);    // VA 0x432FA0 (ui_frame_step.cpp),

// ---------------------------------------------------------------------------
// VA 0x00464A00 - FinishAviRecord: recording-wait epilogue (called at 0x46F122
// after the frame-step wait finishes, from RecWndProc's IDYES and the AVI
// dump flow).  Full port:
//   timeEndPeriod(1); stop-playback restore (0x4341E0); the 0x9EDD4 flag
//   byte poke; TeardownDShowGraph (0x409320) on the recorder at +0xA06C0;
//   lazy GetBackBuffer refetch (vtable +0x48) and SetRenderTarget(0, bb)
//   when the wrapper byte +0x1D4F8 == 0 (vtable +0x94); the separate-window
//   branch (byte 0xA0B41): clear 0x9FE74, ApplyFullscreenWindowState fullscreen restore,
//   PostDeviceReset, clear 0xA0B41 - else the device-vs-window size check
//   (adopt window size + PostDeviceReset) and DestroyWindow of the wait
//   window 0xA0D24; enable control 408; viewport refresh (0x42C810) +
//   0x432FA0 + PostViewRefresh; Release() the two COM objects at
//   650120/650124; free the buffer at 652084; ShowWindow(SW_SHOW) the main
//   and separate windows when the 0xA0B41 dialog was not up.
// ---------------------------------------------------------------------------
void FinishAviRecord(MMDApp* app) {
    auto& s = *app;
    timeEndPeriod(1);                                  // 0x464A07
    s.state.playbackActive = 0;                        // 0x464A11
    StopPlayback(app);                                 // 0x464A17
    unsigned char* flag = s.RecordingCompletionFlag(); // 0x464A1C
    s.state.frameStepPlayback = 0;                     // 0x464A22
    if (flag != nullptr)
        *flag = 1;                    // 0x464A28
    TeardownDShowGraph(s.Recorder()); // 0x464A31
    D3DRenderer* r = s.Renderer();
    if (r != nullptr) {
        IDirect3DDevice9* device = r->device; // 0x1D4E0
        IDirect3DSurface9** backBuffer = &r->backbufferSurface;
        if (*backBuffer == nullptr) // 0x464A3C
            device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO,
                                  backBuffer); // 0x464A5A
        if (r->multisampleAvailable == 0)
            device->SetRenderTarget(0, *backBuffer); // 0x464A81
    }
    if (s.state.aviStereoOutput != 0) {  // 0xA0D61
        s.state.fullscreenMode = 0;      // 0x464B0B
        ApplyFullscreenWindowState(app); // 0x464B11
        PostDeviceReset(app);            // 0x464B18
        s.state.aviStereoOutput = 0;     // 0x464B1D
    } else {
        const std::int32_t winW = s.state.recRTW;
        const std::int32_t winH = s.state.recRTH;
        if (r != nullptr && (r->screenWidth > winW || r->screenHeight > winH)) {
            r->presentParameters.BackBufferWidth = winW;  // 0x1D4FC
            r->presentParameters.BackBufferHeight = winH; // 0x1D500
            r->screenWidth = winW;                        // 0x1D4E4
            r->screenHeight = winH;                       // 0x1D4E8
            PostDeviceReset(app);                         // 0x464AF5
        }
        DestroyWindow(s.RecordingWindow()); // 0x464B01
    }
    s.RecordingWindow() = nullptr;                                                // 0x464B31
    EnableWindow(GetDlgItem(static_cast<HWND>(s.Hwnd()), panel::kPlayButton), 1); // 0x464B3E
    RefreshMainWindowViewport(app);                                               // 0x464B46
    RefreshAfterFrameApply(app);                                                  // 0x464B4D
    PostViewRefresh(app);                                                         // 0x464B54
    if (IDirect3DSurface9* com = s.CaptureRenderTarget()) {                       // 0x464B69
        com->Release();
        s.CaptureRenderTarget() = nullptr;
    }
    if (IDirect3DSurface9* com = s.CaptureSystemSurface()) { // 0x464B81
        com->Release();
        s.CaptureSystemSurface() = nullptr;
    }
    if (void* buf = s.state.captureReadbackPixels) { // 0x464B94
        free(buf);
        s.state.captureReadbackPixels = nullptr;
    }
    if (s.state.aviStereoOutput == 0) {                   // 0x464BA2
        ShowWindow(static_cast<HWND>(s.Hwnd()), SW_SHOW); // 5
        HWND separate = s.FloatingWindow();
        if (separate != nullptr)
            ShowWindow(separate, SW_SHOW); // 0x464BC8
    }
}

// StopPlayback (0x4341E0 stop playback) and the 0x4C2760/4C34A0
// audio-timer helpers are defined further below (400-family). 0x463640
// (edit commit) is ported in src/window/ui_edit_commit.cpp; 0x4C2680/
// 0x4C2A00/0x4C2B80 live in ui_refresh.cpp / ui_timeline_gfx.cpp.

// VA 0x0042A110 - model count query behind ExpGetPmdNum.  Original walks the
// 100-slot model array at +0x780 in an unrolled 20x5 loop counting non-null
// entries (the loop body is the compiler's unroll of `for i in 0..100`).
// x64 twin: the ExpGetPmdNum export body itself (sub_7FF7CB4FB5F0) counts
// non-null entries in a 255-count do/while over app+0xBE8.
int GetPmdNum(MMDApp* app) {
    int count = 0;
    for (int slot = 0; slot < kModelSlotCount; ++slot) {
        if (app->ModelSlot(slot) != nullptr)
            ++count;
    }
    return count;
}

// ---- subsystem inits reached from InitMainWindowAndD3D (0x0047A5B0) ------
// 0x406D40 - render-subsystem construction/reset for the 0x1D574 object.
// The original defaults three device-state gates that the render passes
// key on: +120164 = 1 (stencil shadow passes), +120024 = 1 (MaxAnisotropy
// seed, replaced by InitD3D caps), +120176 = 1 (anisotropic sampler path).
// Leaving this a no-op silently disabled the stencil shadow compositing
// and the anisotropic filters, which the d3d9 call-trace A/B surfaced as
// the missing STENCILFUNC/REF/PASS trios on the port side.
void RendererInit(D3DRenderer* r) {
    r->d3d9 = nullptr;
    r->device = nullptr;
    r->lineVertexBuffer = nullptr;
    r->captureSurface = nullptr;
    r->backbufferSurface = nullptr;
    r->depthStencilSurface = nullptr;
    r->hdrTexture = nullptr;
    r->spriteTexture = nullptr;
    r->shadowSurface = nullptr;
    r->shadowDepthSurface = nullptr;
    r->effect = nullptr;
    r->screenWidth = 0;
    r->screenHeight = 0;
    r->multisampleAvailable = 0;
    r->postProcessEnabled = 0;
    // 0x406D94: the 10000-entry resource pool keeps two of every three
    // dwords zeroed (the tag is left untouched).
    for (auto& entry : r->resourcePool) {
        entry.heapBuffer = nullptr;
        entry.comObject = nullptr;
    }
    r->d3dInitialized = 1;
    r->shaderModelCaps = 1;
    r->runtimeToggle = 1;
    r->localeTable[0] = _create_locale(LC_ALL, "");
    r->localeTable[1] = _create_locale(LC_ALL, ".OCP");
    r->localeTable[2] = _create_locale(LC_ALL, ".ACP");
    r->localeTable[3] = _create_locale(LC_ALL, "JPN");
    // 0x406E18..0x406E42: nvapi stereo probe chain.  sub_4C6940 loads
    // nvapi.dll and runs NvAPI_Initialize; on success sub_4CAF10(1) queries
    // the stereo caps interface (0xBE7692EC) and sub_4CB210 the support
    // gate (0x239C4545).  stereoEnabled = 1 only when every step returns 0.
    // On drivers without 3D Vision support the caps call fails and the byte
    // stays 0 - matching the measured original on this machine - but on a
    // stereo-capable driver the port now follows the original instead of
    // hard-disabling.
    {
        int status = NvapiInitChain(); // 0x406E18
        if (status != 0)
            status = NvapiStereoCaps(1); // 0x406E28
        if (status != 0) {
            r->stereoEnabled = 0; // 0x406E2F
        } else {
            r->stereoEnabled = (NvapiStereoSupportGate() == 0) ? 1 : 0; // 0x406E42
        }
    }
}
// 0x4C46F0: the original is the identity ctor `return this;` (verified on
// the decompile) - callers ignore the return value, so an empty body is
// the faithful port, not a placeholder.
void IdentityCtor(void* obj) {
    (void)obj;
}
// ---------------------------------------------------------------------------
// VA 0x0040FF80 - DialogFunc: generic message-box-style dialog procedure
// used by the v2 loader's old-version migration dialogs (three DialogBox
// sites in 0x450000).  WM_INITDIALOG copies the app's prompt text at
// +0xA442D into static control 0x2AE; WM_COMMAND maps the child ids to
// DialogBox return codes (2AF->1, 2B0->3, 2B1->4, 2B2->5, IDCANCEL->2);
// everything else returns FALSE without chaining.
// ---------------------------------------------------------------------------
INT_PTR CALLBACK PmmMigrationDialogProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg == WM_INITDIALOG) { // 0x410014
        MMDApp* app = g_Block;
        SetWindowTextA(GetDlgItem(hDlg, panel::kScaleFromEdit), app->state.statusText);
        return 0;
    }
    if (msg == WM_COMMAND) { // 0x40FF98
        switch (LOWORD(wp)) {
        case 0x2AF:
            EndDialog(hDlg, IDOK);
            return 0; // 0x40FFA2
        case 0x2B0:
            EndDialog(hDlg, 3);
            return 0; // 0x40FFBA
        case 0x2B1:
            EndDialog(hDlg, 4);
            return 0; // 0x40FFD2
        case 0x2B2:
            EndDialog(hDlg, 5);
            return 0; // 0x40FFEA
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return 0; // 0x410002
        default:
            break;
        }
    }
    return 0; // 0x410038
}
} // namespace mikudancestudio
