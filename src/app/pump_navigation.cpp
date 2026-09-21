// ===========================================================================
// Pump viewport interaction: arrow keys, RMB drag, MMB pan, numpad presets
// ===========================================================================
// The main pump (x86 0x46B090 / x64 sub_7FF7CB4474F0) consumes the arrow
// keys, the right/middle mouse drags and the numpad view presets inline,
// using the 0/1/2/3 edge cells that MouseInteractionBegin polls
// (frame_modes.cpp kKeySlots / model_query_helpers.cpp PollKeyboardStates; values:
// 0 idle, 1 pressed, 2 released, 3 held).  This file ports the four
// segments as self-contained free functions so the frame driver can call
// them in pump VA order next to ConsumeLetterHotkeys (key_ladder.cpp):
//
//   ConsumeArrowKeyNavigation   x86 0x47283F..0x472A49
//                               x64 0x7FF7CB44F7A6..0x44F9C1
//   ConsumeRightButtonDrag      x86 0x470BF5..0x470DD5 (hold + release)
//                               x64 0x7FF7CB44D6DF..0x44D93D
//   ConsumeMiddleButtonPan      x86 0x470DD5..0x47133D (hold + release)
//                               x64 0x7FF7CB44D93D..0x44DE6E
//   ConsumeNumpadViewPresets    x86 0x473127..0x473848 (gate + 7 blocks)
//                               x64 0x7FF7CB4501B5..0x45098A
//
// Key state cells (x86 = MMDAppState offsets; the x64 original's poll
// table sub_7FF7CB446AD0 writes the same cells at x86+4):
//   up +0x14 / down +0x18 / left +0x1C / right +0x20
//   shift +0x24 / ctrl +0xC0 / RBUTTON +0x88 / MBUTTON +0x8C
//   NUMPAD0..9 +0x90..+0xB8        (x64: +4 each)
//
// Segment gates, recomputed inside each function like the original:
//   arrows:  (GetFocus() == main window || ViewportInputActive) &&
//            !playing && !editFocus                    (x86 0x47283F)
//   RMB/MMB: ViewportInputActive && cell == 3 (held) / == 2 (released)
//   numpad:  ViewportInputActive && !playing && !editFocus
//            (x86 0x473127; note: no GetFocus alternative here)
//   editFocus: the same panel-edit probe as key_ladder.cpp (x86 var_14D0),
//            duplicated below to keep this TU self-contained.
//
// Bone-edit drag path shared by RMB/MMB/numpad (x86 0x470C15/0x470D78/
// 0x47318F): slotIdx(0x910) == cameraParentModel(0xA0430) &&
// cameraParentModel >= 0 && !playing(0x330) && followCameraEnabled != 0 (the pump reads
// the BYTE 0x9ED98, unlike frame_modes.cpp ViewRefreshGate's dword read).
// Hit: view-dirty viewDirty (0xA05D1) = 1 on hold / 0 on release, then the
// view-lock windowLayoutReady (0xA442C) 0 -> PostLanguageSweep2 (0x40D070)
// -> 1.
//
// Static constants (read from the x64 image; the x86 0x52xxxx slots are
// runtime-initialized zeros in the file image, see frame_modes.cpp):
//   arrow hold threshold   0.4    x64 .rdata 0x7FF7CB552D0C (x86 0x52E908)
//   RMB pan/zoom scale     0.1    x64 0x7FF7CB552B38 (x86 0x52BEA8)
//   RMB rotate scale       0.01   x64 0x7FF7CB552C28 (x86 0x52E9C8)
//   MMB pan scale          0.5 / 0.005 / 0.05  (x86 0x52B8F0/0x52E9C0/
//                                          0x52D738; shift/ctrl/plain)
//   MMB key-pixel scale    2e-4 / 2e-6 / 2e-5  x64 0x7FF7CB552D18/D14/D10
//   key-pan depth floor    0.3 / 10.0 / 1.0    x64 0x7FF7CB552C54/2980/2984
//   numpad angle presets   PI/2 = 0x3FC90FF9, PI = 0x40490FD8 (as stored)
//   numpad distance        -45.0 (0xC2340000); NUMPAD1 model path -30.0
//                           (0xC1F00000)
//   numpad target Y        10.0 (0x41200000) unless the camera reference
//                           mode is SelectedBone (== 2, x86 0x340)
//
// Not ported here: the ESC/frame-step block before the numpad gate and the
// 'R' block after it (both letter-ladder territory, key_ladder.cpp), and
// the letter blocks themselves.  The older coarse RMB/MMB handling inside
// MouseInteractionBegin (frame_modes.cpp) covers the same pump region
// without the bone-edit path details; when this file is wired into the
// frame driver that block should be retired to avoid double-applying
// drags.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include "pump_input.hpp"

namespace mikudancestudio {

// Sibling-TU bodies, declared locally the key_ladder.cpp way (kept out of
// ported_funcs.hpp).
void StepFrame(MMDApp* app, bool forward);  // VA 0x00430F20 (frame +1) / 0x004312E0
                                            // (frame -1)/0x4312E0
                                            // (ui_frame_step.cpp)
void SelectPreviousDisplayBone(MMDApp* app);  // VA 0x00438D60 (model_query_helpers.cpp)
void SelectNextDisplayBone(MMDApp* app);      // VA 0x00438F50 (model_query_helpers.cpp)
void SelectPrevEditTarget(MMDApp* app);       // VA 0x004391D0 (model_query_helpers.cpp)
void SelectNextEditTarget(MMDApp* app);       // VA 0x00439520 (model_query_helpers.cpp)
void JumpNextKeyframe(MMDApp* app);   // VA 0x00441070 (app_utilities.cpp)
void JumpPrevKeyframe(MMDApp* app);   // VA 0x004414C0 (app_utilities.cpp)

namespace {

// Angle presets bit-exact as stored by the original - deliberately NOT the
// correctly rounded PI constants (float(PI) is 0x40490FDB, float(PI/2) is
// 0x3FC90FDB; the image stores 3 ulps / 30 ulps off respectively).
constexpr float kNumpadHalfPi = 1.5707999467849731f;  // 0x3FC90FF9
constexpr float kNumpadPi = 3.1415920257568359f;      // 0x40490FD8
constexpr float kNumpadDistance = -45.0f;         // 0xC2340000
constexpr float kNumpadDistance1 = -30.0f;        // 0xC1F00000 (NUMPAD1)
constexpr float kNumpadTargetY = 10.0f;           // 0x41200000

// Bone-edit drag path gate (see header): the active model slot is also the
// camera's parent model and the select-state byte is set.  Callers inside
// the model-mode branch; the RMB/MMB release sequences additionally check
// optflag[0] == 0 themselves (implied here by the caller's branch).
bool BoneEditPathActive(MMDApp* app) {
    auto& state = app->state;
    return state.slotIdx == state.cameraParentModel &&  // 0x910 == 0xA0430
           state.cameraParentModel >= 0 &&
           state.playbackActive == 0 &&                 // 0x330
           state.followCameraEnabled != 0;                           // byte 0x9ED98
}

// The view-lock sequence around PostLanguageSweep2 (0x40D070):
// 0xA442C = windowLayoutReady, 0xA05D1 = viewDirty view-dirty.
void EnterBoneEditRefresh(MMDApp* app, bool hold) {
    app->state.viewDirty = hold ? 1 : 0;                 // 0xA05D1
    app->state.windowLayoutReady = 0;                   // 0xA442C
    PostLanguageSweep2(app);                            // 0x40D070
    app->state.windowLayoutReady = 1;
}

// Camera/accessory-mode branch shared by every preset key (x64 sites
// 0x4501F4 / 0x45034F / 0x4504B8 / 0x450631 / 0x45079A / 0x4508F2 /
// 0x4509A4): the rotation preset + fixed distance, then the target-panel
// refresh.
void ApplyNumpadCameraPreset(MMDApp* app, float pitch, float yaw) {
    app->CameraPitch() = pitch;                         // 0x310 / x64 0x348
    app->CameraYaw() = yaw;                             // 0x314 / x64 0x34C
    app->CameraRoll() = 0.0f;                           // 0x318 / x64 0x350
    app->CameraDistance() = kNumpadDistance;            // 0xA08DC / 0xA1900
    RefreshRequest(-1);                                 // 0x440AC0
}

// Model-mode branch shared by NUMPAD 2/4/6/8/5/1 (x64 0x450224..): reset
// the accessory transform block and the camera target/angles.  The skip
// gate (bone-edit path) is checked by the caller.
void ApplyNumpadModelPreset(MMDApp* app, float pitch, float yaw,
                            float distance, bool targetFollowsReference) {
    auto& state = app->state;
    if (state.cameraParentModel >= 0 && state.followCameraEnabled != 0)
        app->CameraAttachmentTransformSuppressed() = 1; // 0xA0478
    app->ViewOffsetX() = 0.0f;                          // 0x308 / x64 0x340
    app->CameraDistance() = distance;
    app->ViewOffsetY() =                                // 0x30C / x64 0x344
        targetFollowsReference &&
        app->CameraReferenceMode() !=
            CameraAttachmentReference::SelectedBone     // x86 0x340 != 2
            ? kNumpadTargetY
            : 0.0f;
    app->CameraPitch() = pitch;
    app->CameraYaw() = yaw;
    app->CameraRoll() = 0.0f;
    // 0xA0438..0xA0478 (x64 0xA140C..0xA144C): 4x4 identity basis.
    D3DMATRIX& basis = app->CameraAttachmentBasis();
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            basis.m[row][col] = row == col ? 1.0f : 0.0f;
}

// x64 0x44DCAC..0x44DE66 (x86 0x470DD5.. tail): free-camera MMB pan.  The
// screen delta is rotated through RotZ(-roll) * (RotX(pitch) * RotY(yaw))
// into the camera target (0x334/0x338/0x33C; x64 0x36C/0x370/0x374).
// Twin of frame_modes.cpp's internal PanCameraPosition (file-local there).
void PanCameraPosition(MMDApp* app, int dx, int dy) {
    const float scale = app->ShiftModifierActive() ? 0.5f        // 0x52B8F0
        : app->CtrlModifierActive() ? 0.005f                     // 0x52E9C0
        : 0.05f;                                                 // 0x52D738
    auto& api = d3dx::Get();
    d3dx::D3DXMATRIXF pitch{}, yaw{}, roll{}, rotation{};
    api.rotY(&yaw, app->CameraYaw());
    api.rotX(&pitch, app->CameraPitch());
    api.multiply(&rotation, &pitch, &yaw);
    api.rotZ(&roll, -app->CameraRoll());
    api.multiply(&rotation, &roll, &rotation);

    // x86 0x470EB8+: rx = prevX - curX, ry = curY - prevY; each product is
    // scaled after the matrix multiply (x64 0x44DD73..0x44DD8E order).
    const float rx = -static_cast<float>(dx);
    const float ry = static_cast<float>(dy);
    app->CameraPositionX() +=
        rx * rotation.m[0][0] * scale + ry * rotation.m[1][0] * scale +
        rotation.m[3][0];
    app->CameraPositionY() +=
        rx * rotation.m[0][1] * scale + ry * rotation.m[1][1] * scale +
        rotation.m[3][1];
    app->CameraPositionZ() -=
        rx * rotation.m[0][2] * scale + ry * rotation.m[1][2] * scale +
        rotation.m[3][2];
}

// x64 0x44DAFF..0x44DCA7 (x86 0x4711xx): MMB pan while the camera key is
// parented to a model - the delta is scaled by pixel size, view depth and
// FOV.  Twin of frame_modes.cpp's internal PanSelectedCameraKey.
void PanSelectedCameraKey(MMDApp* app, int dx, int dy) {
    const float pixelScale = app->ShiftModifierActive() ? 2e-4f   // 0x52D18
        : app->CtrlModifierActive() ? 2e-6f                       // 0x52D14
        : 2e-5f;                                                  // 0x52D10
    const float depth = std::fabs(app->CameraPositionZ());
    const float depthFactor = depth <= 0.3f          // 0x7FF7CB552C54
        ? 10.0f                                      // 0x7FF7CB552980
        : std::max(depth, 1.0f);                     // 0x7FF7CB552984
    const float fov = app->CameraFov();              // 0x9E1E8 / 0x9F0B4
    app->CameraPositionX() -=
        static_cast<float>(dx) * pixelScale * depthFactor * fov;
    app->CameraPositionY() +=
        static_cast<float>(dy) * pixelScale * depthFactor * fov;
}

}  // namespace

// ---------------------------------------------------------------------------
// Arrow-key navigation + hold auto-repeat (G1)
//   x86 0x47283F..0x472A49 / x64 0x7FF7CB44F7A6..0x44F9C1
// ---------------------------------------------------------------------------
// One shared hold timer (x86 0x9E650 = state.keyRepeatTimer) accumulates the frame
// delta (0xA06BC = state.deltaTime) for whichever arrow is held; a press
// re-arms it to zero and a fire does NOT re-arm it, so after the 0.4 s
// threshold the action repeats every frame until release.  RIGHT's repeat
// also drops the view lock (0xA442C) before re-stepping; the release edge
// restores it.
// ---------------------------------------------------------------------------
void ConsumeArrowKeyNavigation(MMDApp* app, const KeyboardInputContext& input) {
    auto& state = app->state;
    const HWND main = static_cast<HWND>(app->Hwnd());
    const bool focusOK =
        input.focus == main || app->ViewportInputActive() != 0;  // 0x9EDD1
    const bool focusInEdit = !input.focusNotInPanelEdit;             // !ecx
    const bool playing = app->PlaybackActive() != 0;            // 0x330
    if (!(focusOK && !playing && !focusInEdit))                 // 0x472860
        return;

    const bool ctrl = state.ctrlModifierState == 3;             // 0xC0/0xC4
    const bool cameraPanel = state.optflag[0] != 0;             // 0x2F8/0x328
    float& repeat = state.keyRepeatTimer;                               // 0x9E650
    constexpr float kRepeatThreshold = 0.4f;  // x64 0x7FF7CB552D0C / 0x52E908
    const auto repeatDue = [&]() {
        repeat += state.deltaTime;                              // 0xA06BC
        return repeat > kRepeatThreshold;
    };

    // ---- RIGHT (x86 0x472868): next frame / Ctrl = next keyframe --------
    if (state.rightKeyState == 1) {                             // 0x20/0x24
        if (ctrl) {
            JumpNextKeyframe(app);                              // 0x441070
        } else {
            StepFrame(app, true);                               // 0x430F20
            repeat = 0.0f;
        }
    }
    if (state.rightKeyState == 3 && !ctrl) {                    // 0x472884
        if (repeatDue()) {
            state.windowLayoutReady = 0;                        // 0xA442C
            StepFrame(app, true);     // timer deliberately not re-armed
        }
    }
    if (state.rightKeyState == 2)                               // 0x4728CF
        state.windowLayoutReady = 1;

    // ---- LEFT (x86 0x4728DC): previous frame / Ctrl = previous keyframe -
    if (state.leftKeyState == 1) {                              // 0x1C/0x20
        if (ctrl) {
            JumpPrevKeyframe(app);                              // 0x4414C0
        } else {
            StepFrame(app, false);                              // 0x4312E0
            repeat = 0.0f;
        }
    }
    if (state.leftKeyState == 3 && !ctrl) {                     // 0x472917
        if (repeatDue())
            StepFrame(app, false);  // no flag writes, no re-arm
    }

    // ---- UP (x86 0x47295B): previous edit target / display bone ---------
    if (state.upKeyState == 1) {                                // 0x14/0x18
        if (cameraPanel)
            SelectPrevEditTarget(app);       // 0x4391D0
        else
            SelectPreviousDisplayBone(app);  // 0x438D60
        repeat = 0.0f;
    }
    if (state.upKeyState == 3) {                                // 0x472982
        if (repeatDue()) {
            if (cameraPanel)
                SelectPrevEditTarget(app);
            else
                SelectPreviousDisplayBone(app);
        }
    }

    // ---- DOWN (x86 0x4729D3): next edit target / display bone -----------
    if (state.downKeyState == 1) {                              // 0x18/0x1C
        if (cameraPanel)
            SelectNextEditTarget(app);       // 0x439520
        else
            SelectNextDisplayBone(app);      // 0x438F50
        repeat = 0.0f;
    }
    if (state.downKeyState == 3) {                              // 0x4729FA
        if (repeatDue()) {
            if (cameraPanel)
                SelectNextEditTarget(app);
            else
                SelectNextDisplayBone(app);
        }
    }
}

// ---------------------------------------------------------------------------
// Right-button viewport drag (G10)
//   hold    x86 0x470BF5..0x470D5E / x64 0x7FF7CB44D6DF..0x44D8B5
//   release x86 0x470D62..0x470DD5 / x64 0x7FF7CB44D8B5..0x44D93D
// ---------------------------------------------------------------------------
// Model mode + bone-edit path consumes the hold entirely (no view refresh);
// otherwise Shift pans the view offsets, Ctrl dollies the camera distance,
// plain drag orbits (yaw -= dx*k, pitch -= dy*k).  Camera/accessory mode
// additionally refreshes the target panel unless the transform selector
// (0x9ED9C) sits on the accessory slot.
// ---------------------------------------------------------------------------
void ConsumeRightButtonDrag(MMDApp* app) {
    auto& state = app->state;
    const bool inputActive = app->ViewportInputActive() != 0;   // 0x9EDD1
    const bool modelMode = state.optflag[0] == 0;               // 0x2F8/0x328
    const int dx = app->MouseX() - app->PreviousMouseX();       // +8/+4 diff
    const int dy = app->MouseY() - app->PreviousMouseY();
    constexpr float kPanScale = 0.1f;     // x64 0x7FF7CB552B38 / x86 0x52BEA8
    constexpr float kRotateScale = 0.01f; // x64 0x7FF7CB552C28 / x86 0x52E9C8

    if (state.rightMouseButtonState == 3 && inputActive) {      // 0x470BF5
        if (modelMode && BoneEditPathActive(app)) {             // 0x470C15..
            EnterBoneEditRefresh(app, /*hold=*/true);           // 0x470C57..
            return;                 // bone path skips PostViewRefresh
        }
        if (modelMode && app->ShiftModifierActive()) {          // 0x470C7A
            app->ViewOffsetY() = app->ViewOffsetY() +           // 0x30C
                static_cast<float>(dy) * kPanScale;
            app->ViewOffsetX() = app->ViewOffsetX() -           // 0x308
                static_cast<float>(dx) * kPanScale;
        } else if (app->CtrlModifierActive()) {                 // 0x470CD4
            app->CameraDistance() = app->CameraDistance() -     // 0xA08DC
                static_cast<float>(dy) * kPanScale;
        } else {                                                // 0x470D02
            app->CameraYaw() = app->CameraYaw() -               // 0x314
                static_cast<float>(dx) * kRotateScale;
            app->CameraPitch() = app->CameraPitch() -           // 0x310
                static_cast<float>(dy) * kRotateScale;
        }
        if (!modelMode && state.coordinateSystem != 2)                    // 0x470D3C
            RefreshRequest(-1);                                 // 0x440AC0
        PostViewRefresh(app);                                   // 0x40D130
        return;
    }

    if (state.rightMouseButtonState == 2 && inputActive) {      // 0x470D62
        if (modelMode && BoneEditPathActive(app))
            EnterBoneEditRefresh(app, /*hold=*/false);          // 0x470DB9..
    }
}

// ---------------------------------------------------------------------------
// Middle-button camera pan (G11)
//   hold    x86 0x470DD5..0x47133C / x64 0x7FF7CB44D93D..0x44DE6E
//   release x64 0x7FF7CB44DE6E..                (x86 0x47133D..)
// ---------------------------------------------------------------------------
// Model mode pans the view offsets (and runs the bone-edit refresh path
// under the shared gate); camera/accessory mode pans the camera target -
// through the rotation matrix when the camera is free, or scaled by pixel
// size / depth / FOV when the camera key is parented to a model.  Always
// ends in PostViewRefresh (0x40D130).
// ---------------------------------------------------------------------------
void ConsumeMiddleButtonPan(MMDApp* app) {
    auto& state = app->state;
    const bool inputActive = app->ViewportInputActive() != 0;   // 0x9EDD1
    const bool modelMode = state.optflag[0] == 0;               // 0x2F8/0x328
    const int dx = app->MouseX() - app->PreviousMouseX();
    const int dy = app->MouseY() - app->PreviousMouseY();

    if (state.middleMouseButtonState == 3 && inputActive) {     // 0x470DD5
        if (modelMode) {                                        // 0x44D995
            const float scale = app->ShiftModifierActive() ? 0.5f    //0x52B8F0
                : app->CtrlModifierActive() ? 0.005f                 //0x52E9C0
                : 0.05f;                                             //0x52D738
            app->ViewOffsetY() = app->ViewOffsetY() +
                static_cast<float>(dy) * scale;                 // 0x30C
            app->ViewOffsetX() = app->ViewOffsetX() -
                static_cast<float>(dx) * scale;                 // 0x308
            if (BoneEditPathActive(app))                        // 0x44DA78..
                EnterBoneEditRefresh(app, /*hold=*/true);       // 0x44DAC8..
        } else if (app->CameraParentModel() >= 0) {             // 0x44DAF0
            PanSelectedCameraKey(app, dx, dy);                  // 0x44DAFF..
        } else {
            PanCameraPosition(app, dx, dy);                     // 0x44DCAC..
        }
        PostViewRefresh(app);                                   // 0x44DE66
        return;
    }

    if (state.middleMouseButtonState == 2 && inputActive) {     // 0x44DE6E
        if (modelMode && BoneEditPathActive(app))
            EnterBoneEditRefresh(app, /*hold=*/false);          // 0x44DED3..
    }
}

// ---------------------------------------------------------------------------
// Numpad view presets (G9) - x86 0x473127..0x473848 / x64 0x7FF7CB4501B5..
// ---------------------------------------------------------------------------
// One outer gate (0x473127/0x4501B5) covers all seven blocks; NUMPAD3/7/9
// have no block in either binary.  Camera/accessory mode presets the
// rotation + distance and refreshes the target panel; model mode resets
// the accessory transform block (identity basis at 0xA0438..0xA0478, and
// the suppressed byte 0xA0478) unless the bone-edit gate skips it.
// NUMPAD0's model path is the full reload reset instead.
// ---------------------------------------------------------------------------
void ConsumeNumpadViewPresets(MMDApp* app, const KeyboardInputContext& input) {
    auto& state = app->state;
    const bool gate = app->ViewportInputActive() != 0 &&        // 0x9EDD1
                      app->PlaybackActive() == 0 &&             // 0x330
                      input.focusNotInPanelEdit;
    if (!gate)                                                  // 0x473145
        return;

    const bool cameraMode = state.optflag[0] != 0;              // 0x2F8/0x328

    // ---- NUMPAD2 (x86 0x47314B / x64 0x4501DA): front view --------------
    if (state.numpadKeyState[2] == 1) {                         // 0x98/0x9C
        if (cameraMode)
            ApplyNumpadCameraPreset(app, 0.0f, 0.0f);
        else if (!BoneEditPathActive(app))                      // 0x47318F..
            ApplyNumpadModelPreset(app, 0.0f, 0.0f, kNumpadDistance,
                                    /*targetFollowsReference=*/true);
        PostViewRefresh(app);                                   // 0x40D130
    }

    // ---- NUMPAD4 (x86 0x473282 / x64 0x450335): yaw +90 degrees ---------
    if (state.numpadKeyState[4] == 1) {                         // 0xA0/0xA4
        if (cameraMode)
            ApplyNumpadCameraPreset(app, 0.0f, kNumpadHalfPi);
        else if (!BoneEditPathActive(app))
            ApplyNumpadModelPreset(app, 0.0f, kNumpadHalfPi, kNumpadDistance,
                                    /*targetFollowsReference=*/true);
        PostViewRefresh(app);
    }

    // ---- NUMPAD6 (x86 0x4733CC / x64 0x45049E): yaw -90 degrees ---------
    if (state.numpadKeyState[6] == 1) {                         // 0xA8/0xAC
        if (cameraMode)
            ApplyNumpadCameraPreset(app, 0.0f, -kNumpadHalfPi);
        else if (!BoneEditPathActive(app))
            ApplyNumpadModelPreset(app, 0.0f, -kNumpadHalfPi, kNumpadDistance,
                                    /*targetFollowsReference=*/true);
        PostViewRefresh(app);
    }

    // ---- NUMPAD8 (x86 0x473520 / x64 0x450617): yaw 180 degrees ---------
    if (state.numpadKeyState[8] == 1) {                         // 0xB0/0xB4
        if (cameraMode)
            ApplyNumpadCameraPreset(app, 0.0f, kNumpadPi);
        else if (!BoneEditPathActive(app))
            ApplyNumpadModelPreset(app, 0.0f, kNumpadPi, kNumpadDistance,
                                    /*targetFollowsReference=*/true);
        PostViewRefresh(app);
    }

    // ---- NUMPAD5 (x86 0x473670 / x64 0x450780): top view ----------------
    if (state.numpadKeyState[5] == 1) {                         // 0xA4/0xA8
        if (cameraMode)
            ApplyNumpadCameraPreset(app, -kNumpadHalfPi, 0.0f);
        else if (!BoneEditPathActive(app))
            ApplyNumpadModelPreset(app, -kNumpadHalfPi, 0.0f, kNumpadDistance,
                                    /*targetFollowsReference=*/false);
        PostViewRefresh(app);
    }

    // ---- NUMPAD0 (x86 0x473799 / x64 0x4508D8): transform reload reset --
    if (state.numpadKeyState[0] == 1) {                         // 0x90/0x94
        if (cameraMode) {
            ApplyNumpadCameraPreset(app, kNumpadHalfPi, 0.0f);
        } else if (!BoneEditPathActive(app)) {                  // 0x4737DE..
            app->CameraAttachmentTransformSuppressed() = 0;     // 0xA0478
            app->ViewOffsetX() = 0.0f;                          // 0x308
            app->ViewOffsetY() = 0.0f;                          // 0x30C
            ReloadModels(app);                                  // 0x42E640
            PostModelReload(app);                               // 0x41A650
        }
        PostViewRefresh(app);
    }

    // ---- NUMPAD1 (x86 0x473848 / x64 0x45098A): near-ground view --------
    if (state.numpadKeyState[1] == 1) {                         // 0x94/0x98
        if (cameraMode)
            ApplyNumpadCameraPreset(app, kNumpadHalfPi, 0.0f);
        else if (!BoneEditPathActive(app))
            ApplyNumpadModelPreset(app, kNumpadHalfPi, 0.0f, kNumpadDistance1,
                                    /*targetFollowsReference=*/false);
        PostViewRefresh(app);
    }
}

}  // namespace mikudancestudio
