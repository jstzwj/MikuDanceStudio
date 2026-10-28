// Kinect collection and capture gate: x86 FrameDriver 0x46DCxx/0x46FBxx;
// x64 0x7FF7CB44A34C..0x7FF7CB44A3EC / 0x7FF7CB44C141..0x7FF7CB44C656.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>

#include <cstdint>
#include <new>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// The trace registrar is shared by x86 and x64 (src/model/pose_trace.cpp).
void PumpKinectSkeleton(MMDApp* app, unsigned char selActive) {
    auto& s = *app;

    // Three-condition gate at 0x7FF7CB44C106..0x7FF7CB44C128.
    if (selActive == 0 || s.CameraMode() != 0 || // x64 0x328 (camera mode)
        s.state.frameStepPlayback != 0)          // x64 0x9FC98（x86 0x9ED90）
        return;

    mdl::ModelRecord* model = s.SelectedModel(); // slot index at x64 0x13E0 / table at 0xBE8

    // 1. Fetch joints (0x7FF7CB44C141..0x7FF7CB44C4E7): OpenNI joint index -> slot
    // float index (byte offset = 14280 + 12*slot ordinal, sharing ModelVertexHistoryPush's channel table).
    // The first 18 are unconditional; version 1.40 adds 2 and version 1.50 adds 5.
    auto getJoint = s.OpenNi().jointPosition; // x64 [app+0xA1358]
    if (model == nullptr || getJoint == nullptr)
        return;
    mdl::CaptureSkeletonJoints(model->currentJoints, s.state.openniVersion, getJoint);

    // 2. Moving-average smoothing (0x7FF7CB44C4EF): window = framesPerSecond/10.
    ModelVertexHistoryPush(model, s.state.framesPerSecond / 10); // [app+0x360]/10

    // 3. At 12600 SM-Lip samples, uncheck automatic frame recording and reset the capture stage
    // （0x7FF7CB44C51C..0x7FF7CB44C558）。
    if (static_cast<std::uint32_t>(model->poseTraceSampleCount) >= mdl::kPoseTraceCapacity) {
        CheckMenuItem(GetMenu(static_cast<HWND>(s.Hwnd())), MDS_CMD_TOGGLE_MOTION_CAPTURE,
                      MF_UNCHECKED);
        s.state.autoRepeat = 0; // x64 0xA1E14 / x86 0xA0D68
    }

    // 4. Populate the standard pose and detect the recording edge (0x7FF7CB44C560..0x7FF7CB44C59B):
    // returning 0 (no recording ended this frame) goes directly to gate B.
    const char wasRecording =
        ModelStandardPoseSetup(model,
                               s.state.autoRepeat == 4 ? 1 : 0, // dl = (stage==4)
                               s.state.kinectMirrorEnabled,     // r8 = 0xA1370
                               s.state.kinectInitLostBone);     // r9 = 0xA1371
    if (wasRecording == 0)
        return;

    // 5. Disable Kinect and register the entire capture as keyframes (0x7FF7CB44C5A1..0x7FF7CB44C604).
    DisableKinect(app); // 0x42A020
    if (model != nullptr)
        RegisterKinectPoseCapture(model, static_cast<unsigned>(s.state.currentFrame)); // [0x1450]
    CurvePanelRepaint(app); // 0x7FF7CB482BB0
    if (model != nullptr)
        SeekModelFrame(*model, s.state.currentFrame,
                       s.PlaybackPhysicsMode()); // 0x7FF7CB4EBD90

    // 6. Finish (0x7FF7CB44C609..0x7FF7CB44C656).
    if (s.state.lastRegisteredFrame < model->maxFrame)
        s.state.lastRegisteredFrame = model->maxFrame;
    s.PhysicsResetPending() = 1;              // 0x9FCC1 (second set site)
    s.state.kinectCaptureActive = 0;          // 0xA1373 / x86 0xA03DF
    s.state.fpsLimit = s.state.fpsLimitSaved; // [0xA1904] = [0xA1374]
    PanelPaint(app);                          // 0x7FF7CB480EA0
}

// ---------------------------------------------------------------------------
// x86 0x46DCCF..0x46DD61 / x64 0x7FF7CB44A34C..0x7FF7CB44A3EC -
// ManageKinectRecordGate: immediately follows OpenNIIsTracking detection in the pump prologue (frame_driver.cpp's
// selActive block). The caller guarantees the gate conditions (Kinect enabled and not in frame-by-frame
// playback); this function has no return value.
//
//   1. Depth-map drawing request (0x46DCCF/0x7FF7CB44A34C):
//      OpenNIDrawDepthMap(bool) (export slot 2, x86 [app+0xA03C8] /
//      x64 [app+0xA1348]): pass 0 only when both kinectCaptureActive(0xA03DF) and the capture stage
//      (0xA0D68/0xA1E14) are 0 (fully idle); otherwise pass 1.
//   2. Menu 0x124 maintenance (0x46DCFE..0x46DD61/0x7FF7CB44A37C..0x7FF7CB44A3E4):
//      selActive && non-camera mode(app+0x2F8/0x328) -> EnableMenuItem(0x124,
//      MF_ENABLED(0)); otherwise EnableMenuItem(0x124, MF_GRAYED(1)) +
//      CheckMenuItem(0x124, MF_UNCHECKED) + clear the capture stage (losing tracking
//      disables automatic frame recording and resets its state machine, as in DisableKinect 0x42A044).
// ---------------------------------------------------------------------------
void ManageKinectRecordGate(MMDApp* app, unsigned char selActive) {
    auto& s = *app;

    // 1. Depth map: do not draw while idle (neither file-driven capture nor any recording
    // state-machine stage is active).
    auto drawDepthMap = s.OpenNi().drawDepthMap; // x64 [app+0xA1348]
    if (drawDepthMap != nullptr)
        drawDepthMap((s.state.kinectCaptureActive == 0 && // x86 0xA03DF
                      s.state.autoRepeat == 0)            // x86 0xA0D68
                         ? 0
                         : 1);

    // 2. Menu 0x124: enabled only while tracking in non-camera mode.
    HMENU menu = GetMenu(static_cast<HWND>(s.Hwnd()));
    if (selActive != 0 && s.CameraMode() == 0) {
        EnableMenuItem(menu, MDS_CMD_TOGGLE_MOTION_CAPTURE, 0); // MF_ENABLED
        return;
    }
    EnableMenuItem(menu, MDS_CMD_TOGGLE_MOTION_CAPTURE, 1); // MF_GRAYED
    CheckMenuItem(menu, MDS_CMD_TOGGLE_MOTION_CAPTURE, MF_UNCHECKED);
    s.state.autoRepeat = 0; // x64 0xA1E14 / x86 0xA0D68
}

} // namespace mikudancestudio
