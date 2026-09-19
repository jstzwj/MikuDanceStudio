// ===========================================================================
// VA 0x0046B090 - FrameDriver  (original: sub_46B090, 0xED01 = 60KB)
// ===========================================================================
// The verified frame flow skeleton with the interaction-mode dispatch
// table; the per-mode manipulation bodies (modes 1..0x12) live in
// frame_modes_bone.cpp and are field-exact against the decompilation.
//
// Flow (verified):
//   1. mouse-delta snapshot reset
//   2. interaction mode state machine, this+836:
//        1..3   rotation X/Y/Z (object slot+0x220 / camera; UI edit 481..)
//        4..6   translation X/Z/Y
//        7      scale
//        8..9   bone rotation (matrix path via 0x507AA0/0x507970)
//        10..12 physics body pos/rot X/Y/Z (ctrl 0x2C5+ / 0x2E8+)
//        0xD..F camera adjustment
//        0x10..0x12 angle adjustment
//   3. mouse position update (this+0xA03E8)
//   4. timeline: 0x9EDD0 -> fcn_460130 + clear 0xA03B7;  0xA03B7 sets 0x9EDD0
//   5. reload path: 0xA04B8 -> clear 0xA0478/0x308/0x30C, fcn_42E640,
//      fcn_41A650, clear 0xA04B8
//   6. D3D cooperative-level / device reset dance (device =
//      *(this+0xA06C4)+120032), reset params from 0xA0D24/0xA0D38/
//      renderW/H; D3DERR_DEVICELOST wait loop with Sleep(1) + message pump
//      when 0xA0274 (windowed?) set; then fcn_440DB0
//   7. animation frame section (0x479CC6): byte 0x9EDD8 -> copy 0x9EDDC ->
//      this+0x980, 0x9ED90=1, unsigned frame ints 0xA0B00/0xA0B04 (fild
//      +2^32 wrap, const 0x52B9F0) / 30.0 (dbl_52BA68) -> 0x9E654/0x9E658,
//      int copy 0x9E648, cursor 0x9E64C, 0x9EDB6 equality flag, 0x330=1,
//      fcn_433A40, 0x9ED94=0, EnableWindow(GetDlgItem(0xA06B8, 0x198),
//      FALSE), 0x9EDD8=0
//   7b. selection-active local var_14A1 (0x46DCA6..0x46DCCD), threaded
//      into the playback catch-up below
//   8. playback catch-up (0x46EEE0..0x46F575, playback_catchup.cpp) and
//      the physics simulation blocks (0x46F7FF..0x46FEB8, physics_frame.cpp)
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "frame_state_dump.hpp"

// key_ladder.cpp (registered in CMakeLists next to frame_modes_bone.cpp):
// main-pump letter hotkey consumption ladder (x86 0x46FF35..0x4739E2).
// pump_navigation.cpp / pump_edit_keys.cpp: the non-letter pump segments
// that surround the ladder in the original message pump.
namespace mikudancestudio {

void ConsumeLetterHotkeys(MMDApp* app);
void ConsumeRightButtonDrag(MMDApp* app);   // pump_navigation.cpp (G10)
void ConsumeMiddleButtonPan(MMDApp* app);   // pump_navigation.cpp (G11)
void ConsumeEditKeys(MMDApp* app);          // pump_edit_keys.cpp

namespace {

void AdvanceFrameRenderGate(MMDApp* app) {
    // 0x46B0B1..0x46B11D: transient render/present state machine.
    // The three gates independently restart the cycle before its 1->2->3->0
    // transition; the normal main-window path therefore enters rendering at 2.
    auto& state = app->state.messageSeen;
    if (app->PlaybackActive() != 0)
        state = 1;
    if (app->state.frameVolumeControlEnabled == 0)
        state = 1;
    if (app->FrameRangeDialog() != nullptr)
        state = 1;

    if (state == 1)
        state = 2;
    else if (state == 2)
        state = 3;
    else if (state == 3)
        state = 0;
}

#ifdef MIKUDANCESTUDIO_DIAG
// ---- A/B capture tooling (see frame_state_dump.hpp for the gate) ---------

void TraceOperationInput(MMDApp* app, const char* phase) {
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_AB_TRACE_INPUT", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;
    const int state = app->LeftMouseButtonState();
    const auto mode = app->InteractionDragMode();
    if (state == 0 && mode == ViewportDragMode::None)
        return;
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\input_trace.jsonl", directory);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, "ab") != 0 || stream == nullptr)
        return;
    std::uint32_t quaternion[4]{};
    const unsigned slot = app->state.slotIdx;
    auto* model = app->ModelSlot(slot);
    if (model != nullptr) {
        const int selected = mikudancestudio::mdl::Mdl(model)->selectedBone;
        auto* bones = mikudancestudio::mdl::Bones(model);
        const int count = static_cast<int>(mikudancestudio::mdl::Mdl(model)->boneCount);
        if (bones != nullptr && selected >= 0 && selected < count)
            std::memcpy(quaternion, &bones[selected].rotQuat[0],
                        sizeof(quaternion));
    }
    std::fprintf(stream,
        "{\"phase\":\"%s\",\"state\":%d,\"mode\":%u,"
        "\"mouse\":[%d,%d,%d,%d],\"quaternion\":["
        "\"%08X\",\"%08X\",\"%08X\",\"%08X\"]}\n",
        phase, state, mode,
        app->MouseX(), app->MouseY(),
        app->PreviousMouseX(), app->PreviousMouseY(),
        quaternion[0], quaternion[1], quaternion[2], quaternion[3]);
    std::fclose(stream);
}

bool LineCaptureSwitchReady() {
    char stableCapture[2]{};
    if (GetEnvironmentVariableA("MIKUDANCESTUDIO_AB_STABLE_CAPTURE", stableCapture,
                                sizeof(stableCapture)) != 1 ||
        stableCapture[0] != '1') {
        return true;
    }

    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_VB_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return false;

    char readyPath[MAX_PATH]{};
    std::snprintf(readyPath, sizeof(readyPath),
                  "%s\\vb.capture.ready", directory);
    const DWORD attributes = GetFileAttributesA(readyPath);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool IsRegularFile(const char* path) {
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool WriteCaptureMarker(const char* path, const char* contents) {
    HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const DWORD size = static_cast<DWORD>(std::strlen(contents));
    const bool ok = WriteFile(file, contents, size, &written, nullptr) != FALSE &&
                    written == size && FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    return ok;
}

void ActivateRefreshedVbCapture() {
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_VB_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    char requestPath[MAX_PATH]{};
    char activePath[MAX_PATH]{};
    std::snprintf(requestPath, sizeof(requestPath),
                  "%s\\vb.capture.ready", directory);
    std::snprintf(activePath, sizeof(activePath),
                  "%s\\vb.capture.active", directory);
    if (IsRegularFile(requestPath) && !IsRegularFile(activePath))
        WriteCaptureMarker(activePath, "active after successful Present\n");
}

void HoldPresentCaptureFence() {
    char stableCapture[2]{};
    if (GetEnvironmentVariableA("MIKUDANCESTUDIO_AB_STABLE_CAPTURE", stableCapture,
                                sizeof(stableCapture)) != 1 ||
        stableCapture[0] != '1') {
        return;
    }

    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_VB_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    char requestPath[MAX_PATH]{};
    char readyPath[MAX_PATH]{};
    char releasePath[MAX_PATH]{};
    std::snprintf(requestPath, sizeof(requestPath),
                  "%s\\screen.present.request", directory);
    if (!IsRegularFile(requestPath))
        return;
    std::snprintf(readyPath, sizeof(readyPath),
                  "%s\\screen.present.ready", directory);
    std::snprintf(releasePath, sizeof(releasePath),
                  "%s\\screen.present.release", directory);
    if (!WriteCaptureMarker(readyPath, "presented\n"))
        return;

    // Test-only handshake: keep the UI thread immediately after a successful
    // Present until the external capturer has copied that exact desktop frame.
    const ULONGLONG deadline = GetTickCount64() + 30000;
    while (!IsRegularFile(releasePath) && GetTickCount64() < deadline)
        Sleep(1);
}

#endif  // MIKUDANCESTUDIO_DIAG

}  // namespace

// ---------------------------------------------------------------------------
// VA 0x00460130 - TimelineAdvance (misleading ledger name: this is the
// frame-capture / screenshot tail invoked from the timeline flag handoff at
// 0x479864 when byte 0x9EDD0 is set).  Render wrapper = app+0xA06C4, device
// at wrapper+0x1D4E0, back buffer at wrapper+0x1D538, back-buffer format at
// wrapper+0x1D540; capture size app+0xA08D4/A08D8; wait window app+0xA0D24;
// save path buffer app+0x9F134 (wchar); window size app+0xA02AC/A02B0.
//
// Flow (vtable slots empirically anchored by the A/B traces: 0x48 =
// GetBackBuffer, 0x70 = CreateRenderTarget, 0x88 = StretchRect, 0x94 =
// SetRenderTarget):
//   1. CreateRenderTarget(w, h, backBufferFormat, 0, 0, 0, &surf, 0);
//      FAILED -> destroy wait window, viewport refresh, return.
//   2. back buffer lazily re-fetched (GetBackBuffer(0, 0, MONO, &bb)).
//   3. StretchRect(bb, rect{0,0,w,h}, surf, same rect, D3DTEXF_LINEAR);
//      if that FAILED, retried with filter NONE.  If both succeeded ->
//      D3DXSaveSurfaceToFileW(path, extFormat(path), surf, 0, &rect) (the
//      extension ladder ".bmp"0/".jpg"1/".png"3/".dds"4/".dib"6/".pfm"8/
//      ".hdr"7; an unrecognized extension passes the surface pointer as the
//      format - a latent original quirk, D3DX rejects it harmlessly).
//   4. save tail: Release(surf); re-fetch back buffer if null;
//      SetRenderTarget(0, bb) when wrapper byte 0x1D4F8 == 0; destroy the
//      wait window; when the window size (0xA02AC/A02B0) exceeds the device
//      size (wrapper 0x1D4E4/0x1D4E8), copy it into 0x1D4E4/0x1D4E8 and
//      0x1D4FC/0x1D500 and run PostDeviceReset (0x440DB0).
//   5. always RefreshMainWindowViewport (viewport refresh) at the end (save path); the
//      blit-failed and create-failed paths only destroy + refresh.
// ---------------------------------------------------------------------------
void TimelineAdvance(MMDApp* app) {
    D3DRenderer* r = app->Renderer();
    if (r == nullptr)
        return;  // wrapper absent: nothing the original could act on either
    IDirect3DDevice9* device = r->device;  // +0x1D4E0
    if (device == nullptr)
        return;

    IDirect3DSurface9* surf = nullptr;                            // 0x46016A
    if (FAILED(device->CreateRenderTarget(
            app->RenderWidth(),
            app->RenderHeight(),
            r->backbufferFormat,  // wrapper+0x1D540
            D3DMULTISAMPLE_NONE, 0, FALSE, &surf, nullptr))) {
        DestroyWindow(app->RecordingWindow());                    // 0x460177
        app->RecordingWindow() = nullptr;
        RefreshMainWindowViewport(app);                                           // 0x460185
        return;
    }

    IDirect3DSurface9** backBuffer = &r->backbufferSurface;  // +0x1D538
    if (*backBuffer == nullptr)                                   // 0x460197
        device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, backBuffer);

    RECT rect;                                                    // 0x4601B7
    rect.left = 0;
    rect.top = 0;
    rect.right = app->RenderWidth();
    rect.bottom = app->RenderHeight();

    HRESULT hr = device->StretchRect(*backBuffer, &rect, surf, &rect,
                                     D3DTEXF_LINEAR);             // 0x460200
    if (FAILED(hr))
        hr = device->StretchRect(*backBuffer, &rect, surf, &rect,
                                 D3DTEXF_NONE);                   // 0x46022E
    if (FAILED(hr)) {
        // both blits rejected: no file is written, just clean up.
        DestroyWindow(app->RecordingWindow());                    // 0x46023B
        app->RecordingWindow() = nullptr;
        RefreshMainWindowViewport(app);                                           // 0x460249
        if (surf != nullptr)
            surf->Release();                                      // 0x460260
        return;
    }

    // ---- save path (0x460269): extension ladder -------------------------
    const wchar_t* path = app->CaptureSavePath();
    std::intptr_t format;  // passed straight to D3DX as the original does
    if (wcsstr(path, L".bmp") != nullptr)
        format = 0;                                               // D3DXIFF_BMP
    else if (wcsstr(path, L".jpg") != nullptr)
        format = 1;                                               // D3DXIFF_JPG
    else if (wcsstr(path, L".png") != nullptr)
        format = 3;                                               // D3DXIFF_PNG
    else if (wcsstr(path, L".dds") != nullptr)
        format = 4;                                               // D3DXIFF_DDS
    else if (wcsstr(path, L".dib") != nullptr)
        format = 6;                                               // D3DXIFF_DIB
    else if (wcsstr(path, L".pfm") != nullptr)
        format = 8;                                               // D3DXIFF_PFM
    else if (wcsstr(path, L".hdr") != nullptr)
        format = 7;                                               // D3DXIFF_HDR
    else
        format = reinterpret_cast<std::intptr_t>(surf);           // 0x46031C
    auto& d3dxApi = d3dx::Get();
    if (d3dxApi.Load() && d3dxApi.saveSurfaceToFileW != nullptr)
        d3dxApi.saveSurfaceToFileW(path, static_cast<int>(format),
                                   surf, nullptr, &rect);         // 0x46032D
    if (surf != nullptr) {
        surf->Release();                                          // 0x460340
        surf = nullptr;
    }

    if (*backBuffer == nullptr)                                   // 0x46034C
        device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, backBuffer);
    if (r->multisampleAvailable == 0)  // wrapper+0x1D4F8
        device->SetRenderTarget(0, *backBuffer);                  // 0x460391
    DestroyWindow(app->RecordingWindow());                        // 0x46039A
    app->RecordingWindow() = nullptr;

    // window-vs-device size check (0x4603B2..0x460410): when either device
    // dimension is larger than the window's, adopt the window size and reset.
    const std::int32_t winW = app->state.recRTW;
    const std::int32_t winH = app->state.recRTH;
    const bool tooWide = r->screenHeight > winH;   // wrapper+0x1D4E8
    const bool tooHigh = r->screenWidth > winW;    // wrapper+0x1D4E4
    if (tooWide || tooHigh) {
        r->presentParameters.BackBufferWidth = winW;   // 0x1D4FC
        r->presentParameters.BackBufferHeight = winH;  // 0x1D500
        r->screenWidth = winW;   // 0x1D4E4
        r->screenHeight = winH;  // 0x1D4E8
        PostDeviceReset(app);                                     // 0x460410
    }
    RefreshMainWindowViewport(app);                                               // 0x460417
}

void FrameDriver(MMDApp* app) {
#ifdef MIKUDANCESTUDIO_DIAG
    // DIAGNOSTIC ONLY: append a tick per pass to prove liveness of the
    // frame driver in real-time runs (stderr is unavailable in the GUI
    // subsystem).
    static char tickPath[MAX_PATH]{};
    static bool tickInit = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_TRACE_TICKS", tickPath, sizeof tickPath) != 0;
    if (tickInit) {
        static LONG tickCount = 0;
        if (InterlockedIncrement(&tickCount) % 60 == 0) {
            if (FILE* f = fopen(tickPath, "a"))
                std::fprintf(f, "tick %ld\n", tickCount), std::fclose(f);
        }
    }
#endif
    AdvanceFrameRenderGate(app);
#ifdef MIKUDANCESTUDIO_DIAG
    char freezePhysics[2]{};
    if (GetEnvironmentVariableA("MIKUDANCESTUDIO_AB_FREEZE_PHYSICS", freezePhysics,
                                sizeof(freezePhysics)) == 1 &&
        freezePhysics[0] == '1') {
        app->state.accessoryEditDialogOpen = 1;
    }
    char requireLine[2]{};
    const bool requireLineEnabled =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_AB_REQUIRE_LINE", requireLine,
                                sizeof(requireLine)) == 1 &&
        requireLine[0] == '1';
    char keepPhysics[2]{};
    const bool keepPhysicsEnabled =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_AB_KEEP_PHYSICS", keepPhysics,
                                sizeof(keepPhysics)) == 1 &&
        keepPhysics[0] == '1';
    static LONG lineModeRequested = 0;
#endif
    DumpFrameEntryState(app);
#ifdef MIKUDANCESTUDIO_DIAG
    if (requireLineEnabled && lineModeRequested != 0)
        DumpFrameEntryState(app, "frame_state_line.json");
#endif
    auto& s = *app;

    // 0x46B118..0x46B17B: one-second render-frame counter.  The original
    // snaps the two common cap-adjacent values to their nominal rates.
    float& fpsElapsed = s.state.fpsOverlayElapsedSeconds;
    std::uint32_t& fpsFrames = s.state.fpsOverlayFrameCount;
    fpsElapsed += s.DeltaTime();
    // 0x46B132: fld1/fcompp + test $0x05/jp - reset only when elapsed is
    // STRICTLY greater than 1.0 (equal or unordered skips).
    if (fpsElapsed > 1.0f) {
        s.state.framesPerSecond = fpsFrames - 1;
        fpsElapsed = 0.0f;
        fpsFrames = 0;
    }
    ++fpsFrames;
    if (s.state.framesPerSecond == 29)
        s.state.framesPerSecond = 30;
    if (s.state.framesPerSecond == 59)
        s.state.framesPerSecond = 60;

    // ---- 1. mouse-delta snapshot reset ------------------------------------
    MouseInteractionBegin(app);                                  // per-frame
    // 0x46FF02 poll done.  The original pump then walks, in this order:
    // the right/middle-button camera drags (x86 0x470BF5..0x47133D), the
    // non-letter edit-key chain whose panel focus sweep opens the ladder
    // region (0x471342..0x473127), and the letter ladder itself
    // (0x46FF35..0x4739E2) - the arrow-key and numpad blocks are
    // interleaved inside the ladder at their binary positions.
    mikudancestudio::ConsumeRightButtonDrag(app);  // pump_navigation.cpp
    mikudancestudio::ConsumeMiddleButtonPan(app);  // pump_navigation.cpp
    mikudancestudio::ConsumeEditKeys(app);         // pump_edit_keys.cpp
    mikudancestudio::ConsumeLetterHotkeys(app);    // key_ladder.cpp
#ifdef MIKUDANCESTUDIO_DIAG
    TraceOperationInput(app, "before");
#endif

    // ---- 2. interaction-mode dispatch --------------------------------------
    const ViewportDragMode mode = s.InteractionDragMode();
    if (mode != ViewportDragMode::None && s.LeftMouseButtonHeld()) {
        switch (mode) {
        case ViewportDragMode::ViewAxisRotateX:
        case ViewportDragMode::ViewAxisRotateY:
        case ViewportDragMode::ViewAxisRotateZ:
            ModeRotate(app, static_cast<int>(mode) - 1);
            break;
        case ViewportDragMode::LocalAxisTranslateX:
        case ViewportDragMode::LocalAxisTranslateY:
        case ViewportDragMode::LocalAxisTranslateZ:
            ModeTranslate(app, static_cast<int>(mode) - 4);
            break;
        case ViewportDragMode::BoneScale:
            ModeScale(app);
            break;
        case ViewportDragMode::BoneMoveVertical:
        case ViewportDragMode::BoneMoveScreenPlane:
            ModeBoneRotate(app, static_cast<int>(mode) - 8);
            break;
        case ViewportDragMode::PhysicsAxisX:
        case ViewportDragMode::PhysicsAxisY:
        case ViewportDragMode::PhysicsAxisZ:
            ModePhysicsBody(app, static_cast<int>(mode) - 10);
            break;
        case ViewportDragMode::CameraAdjustX:
        case ViewportDragMode::CameraAdjustY:
        case ViewportDragMode::CameraAdjustZ:
            ModeCameraAdjust(app, static_cast<int>(mode) - 13);
            break;
        case ViewportDragMode::AngleAdjustX:
        case ViewportDragMode::AngleAdjustY:
        case ViewportDragMode::AngleAdjustZ:
            ModeAngleAdjust(app, static_cast<int>(mode) - 16);
            break;
        default:
            break;
        }
    }
#ifdef MIKUDANCESTUDIO_DIAG
    TraceOperationInput(app, "after");
#endif

    // ---- 3. mouse position update ------------------------------------------
    MouseInteractionEnd(app);

    // ---- 4. selection callback (0x46DCA4..0x46DCCD) ---------------------
    unsigned char selActive = 0;
    if (s.state.frameStepPlayback == 0 &&                  // 0x9ED90
        s.state.depthDeviceEnabled != 0) {                  // 0xA03B8
        // 0xA03D4 = the OpenNI is-tracking callback slot (literal was a
        // +0x80 decimal slip that landed mid-cameraAttachmentBasis)
        // ?OpenNIIsTracking@@YGXPA_N@Z - void __stdcall OpenNIIsTracking(bool*)
        using OpenNIIsTrackingFn = void(__stdcall*)(unsigned char*);
        auto cb = reinterpret_cast<OpenNIIsTrackingFn>(
            s.OpenniTrackingCallback());                         // 0xA03D4
        if (cb != nullptr)
            cb(&selActive);
        // 0x46DCCF..0x46DD61（x64 0x7FF7CB44A34C..0x7FF7CB44A3EC）：原版
        // 泵在探测之后立即做的深度图请求与菜单 0x124 卫生（oni_skeleton_
        // pump.cpp 含地址锚点）。
        ManageKinectRecordGate(app, selActive);
    }

    // ---- 5. dynamic overlay producer + D3D scene envelope ---------------
    RenderFrameScene(app);

    // ida_capture_original_vb.py samples operation state at 0x46FF07: the
    // current overlay has been produced, while 0x4757C3 has not yet reset
    // the transient line counter for the next frame.
    DumpRequestedFrameState(app);

#ifdef MIKUDANCESTUDIO_DIAG
    // Match ida_capture_original_vb.py: its one-shot breakpoint changes
    // these fields at the first camera sprite Unlock, after transforms and
    // sprite production but before the following FrameDriver cycle.
    if (requireLineEnabled && LineCaptureSwitchReady() &&
        InterlockedCompareExchange(&lineModeRequested, 1, 0) == 0) {
        const bool alreadyModelMode =
            app->state.optflag[0] == 0 &&
            app->EditMode() == ViewportEditMode::Bone;
        if (!alreadyModelMode) {
            RebuildModelModePanel(app);
            app->state.optflag[0] = 0;
            app->EditMode() = ViewportEditMode::Bone;
            PostLanguageSweep(app);
            PostModelReload2(app);
            HandleWindowSize(app);
            InvalidateRect(static_cast<HWND>(app->Hwnd()), nullptr, FALSE);
        }
        char stableCapture[2]{};
        if (GetEnvironmentVariableA(
                "MIKUDANCESTUDIO_AB_STABLE_CAPTURE", stableCapture,
                sizeof(stableCapture)) == 1 && stableCapture[0] == '1') {
            app->state.fpsOverlayElapsedSeconds = 0.0f;
            app->state.fpsOverlayFrameCount = 0;
            app->state.framesPerSecond = 0;
        }
        if (!keepPhysicsEnabled)
            app->state.accessoryEditDialogOpen = 1;
    }
#endif

    // ---- 5.5 frame-step recording readback (0x46E787..0x46EFB7) ---------
    // The original splits right after the scene render's EndScene: with
    // the frame-step byte 0x9ED90 set (AVI recording / manual stepping)
    // the readback+push pass runs before the catch-up; a failure inside
    // it runs the recording epilogue and returns from the whole driver.
    if (s.state.frameStepPlayback != 0) {                   // 0x9ED90
        if (!RecordingReadbackPass(app))
            return;
    }

    // ---- 6. playback catch-up + physics (0x46EEE0..0x46FEB8) ------------
    PlaybackCatchup(app, selActive);
    // ---- 6.5 Kinect 骨架驱动泵块（x64 0x7FF7CB44C12E..0x7FF7CB44C656）----
    // 原版位于物理帧内部：settle 上升沿请求（0x7FF7CB44C12E）之后、gate B
    // （0x7FF7CB44C65B）之前。settle 请求已移植在 physics_frame.cpp，该文件
    // 本轮冻结，故此处按同一三重门先行调用（门输入互不影响；块尾与
    // DisableKinect 都会补置 settle，先后次序无观测差异）。
    // oni_skeleton_pump.cpp 含完整地址锚点。
    PumpKinectSkeleton(app, selActive);
    PhysicsFrame(app, selActive);
    // x64 0x7FF7CB456F21（泵尾，紧随 previousMouse 回写 0x7FF7CB456F08..F17）:
    // 把本趟 selActive 滞留进 app+0xA137C；下一趟物理帧的 settle 请求
    // （0x7FF7CB44C12E）读它做上升沿判定。物理帧已消费完上一趟的值，
    // 此处回写不影响本趟。
    s.state.selectionActiveLatch = selActive;

    // The original produces text at 0x46FECF and resets/rebuilds the
    // selection line batch at 0x4757C3. Both are consumed on the next frame.
    if (s.RecordingWindow() == nullptr)
        PrepareFrameTextOverlay(app);
    PrepareFrameLineOverlay(app);

    // ---- 7. timeline flag handoff (0x479864..0x479896) -------------------
    if (s.state.timelineAdvanceDue != 0) {
        TimelineAdvance(app);                                     // 0x460130
        s.state.timelineAdvanceDue = 0;
        s.state.timelineAdvanceRequested = 0;
    }
    if (s.state.timelineAdvanceRequested != 0) {
        s.state.timelineAdvanceDue = 1;
        s.state.timelineAdvanceRequested = 0;
    }

    // ---- 8. reload path (0x47989D..0x4798CB) ----------------------------
    if (s.state.modelReloadPending != 0) {
#ifdef MIKUDANCESTUDIO_DIAG
        if (getenv("MIKUDANCESTUDIO_TRACE_REC")) {
            FILE* tf = fopen(getenv("MIKUDANCESTUDIO_TRACE_REC"), "a");
            if (tf) { fputs("frame section8 reload path\n", tf); fclose(tf); }
        }
#endif
        s.state.cameraAttachmentTransformSuppressed = 0;
        s.ViewOffsetX() = 0.0f;
        s.ViewOffsetY() = 0.0f;
        ReloadModels(app);                                        // 0x42E640
        PostModelReload(app);                                     // 0x41A650
        s.state.modelReloadPending = 0;
    }

    // ---- 9. Present + device-lost recovery (0x479B23..0x479CB9) ----------
    // NOT a Reset dance (phase-A misread): the vtable call is slot +0x44 =
    // 17 = Present (Reset is slot 16 - adjacent, hence the confusion).
    // Gate: A0D6C != 0 && A0D61 == 0 (the skip path reuses a stale local
    // hr in the original; the port keeps last frame's value).
    //   A0274 != 0                 -> Present(NULL, NULL, NULL, NULL)
    //   else A0D24 != 0            -> Present(&r{0,0,A08D4,A08D8}, same,
    //                                   hwnd A0D24, NULL)
    //   else A0D38 != 0            -> Present(&RECT@A0D40, same, A0D38, NULL)
    //   else                       -> Present(&RECT@A0D40, same, A06B8, NULL)
    // On D3DERR_DEVICELOST: while (TestCooperativeLevel() != NOTRESET)
    //   { Sleep(1); if (A0274) pump one message; } then 0x440DB0.
    IDirect3DDevice9* device = nullptr;
    if (s.Renderer() != nullptr)
        device = s.Renderer()->device;                     // +0x1D4E0
    static HRESULT hr = D3D_OK;         // stale-local equivalent
    bool presented = false;
    if (device != nullptr) {
        if (s.state.messageSeen != 0 &&
            s.AviStereoOutput() == 0) {
            presented = true;
            if (s.FullscreenMode() != 0) {
                hr = device->Present(nullptr, nullptr, nullptr, nullptr);
            } else if (s.RecordingWindow() != nullptr) {          // 0xA0D24
                RECT r;
                r.left = 0;
                r.top = 0;
                // 0xA08D4/0xA08D8 = render size (literals were decimal
                // slips landing inside the dirModel path buffer)
                r.right = static_cast<LONG>(s.RenderWidth());
                r.bottom = static_cast<LONG>(s.RenderHeight());
                hr = device->Present(&r, &r,
                                     s.RecordingWindow(), nullptr);
            } else if (s.FloatingWindow() != nullptr) {
                RECT r = s.ViewportRect();                        // 0xA0D40
                hr = device->Present(&r, &r,
                                     s.state.floatingWindow,
                                     nullptr);
            } else {
                RECT r = s.ViewportRect();                        // 0xA0D40
                hr = device->Present(&r, &r,
                                     static_cast<HWND>(s.Hwnd()),
                                     nullptr);
            }
            if (hr == D3DERR_DEVICELOST) {
                // Original loop (x64 0x7FF7CB4570CD): while (TCL !=
                // NOTRESET) { Sleep(1); pump ONLY in fullscreen; }.
                // Port hardening (documented deviation, stuck-forever
                // rescue only): (1) pump in windowed mode too - the
                // original leaves the app visibly hung (AppHangB1 ghost)
                // for as long as the wait lasts, and a pending WM_SIZE can
                // itself be required for the driver to report NOTRESET;
                // (2) break out when TestCooperativeLevel() == D3D_OK - a
                // transient Present() DEVICELOST (RDP/DWM hiccup, driver
                // TDR recovery) otherwise waits forever for a NOTRESET
                // that never comes, freezing the viewport black with the
                // message pump starved; retrying the next Present is the
                // correct recovery when the device is not resettable-pending.
                for (;;) {
                    HRESULT cooperative = device->TestCooperativeLevel();
                    if (cooperative == D3DERR_DEVICENOTRESET ||
                        cooperative == D3D_OK)
                        break;
                    Sleep(1);
                    MSG recovery;
                    if (PeekMessageA(&recovery, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&recovery);
                        DispatchMessageA(&recovery);
                    }
                }
                if (device->TestCooperativeLevel() != D3D_OK)
                    PostDeviceReset(app);                          // 0x440DB0
            }
        }
    }
#ifdef MIKUDANCESTUDIO_DIAG
    if (presented && SUCCEEDED(hr)) {
        if (requireLineEnabled && lineModeRequested != 0)
            ActivateRefreshedVbCapture();
        HoldPresentCaptureFence();
    }
#endif

    // ---- 10. animation frame section (0x479CC6..0x479D75) ----------------
    // Byte compare at 0x479CB9; 0xA0B00/0xA0B04 are INTEGER frame fields
    // (fild + 2^32 fixup on negative, i.e. unsigned load) divided by the
    // constant 30.0 (dbl_52BA68); EnableWindow pushes 0 (0x479D58).
    if (s.state.recordPlaybackStartPending != 0) {       // 0x9EDD8
        s.CurrentFrame() =
            s.state.recordSavedFrame;        // 0x9EDDC
        s.state.frameStepPlayback = 1;          // 0x9ED90
        // (was a raw 650128 literal = 0x9EB90, one of the +/-0x100
        // decimal slips; 0x9ED90 = 650640)

        const std::int32_t fa = s.state.aviRecordStartFrame;
        s.state.playbackStartSeconds =
            static_cast<float>((fa < 0
                                    ? static_cast<double>(fa) + g_Wrap32
                                    : static_cast<double>(fa)) /
                               g_FrameScale);

        const std::int32_t fb = s.state.aviRecordEndFrame;
        s.state.playbackEndSeconds =
            static_cast<float>((fb < 0
                                    ? static_cast<double>(fb) + g_Wrap32
                                    : static_cast<double>(fb)) /
                               g_FrameScale);

        s.state.aviBackgroundSample = s.state.aviRecordStartFrame;
        s.PlaybackCursorSeconds() = s.PlaybackStartSeconds();

        if (s.state.aviRecordStartFrame ==
            s.CurrentFrame())
            s.state.playbackFrameChanged = 1;

        s.state.playbackActive = 1;
        UpdateBoneFrames(app);                                    // 0x433A40
        s.state.recordedFrameCount = 0;
        EnableWindow(GetDlgItem(static_cast<HWND>(s.Hwnd()), panel::kPlayButton),
                     FALSE);
        s.state.recordPlaybackStartPending = 0;
    }

}

}  // namespace mikudancestudio
