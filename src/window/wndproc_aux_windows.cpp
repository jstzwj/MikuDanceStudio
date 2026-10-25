// ===========================================================================
// Window procedures (separate/recording windows)
// ===========================================================================
// The separate window WM_COMMAND child dispatch 0x004620A0 is a full port
// below (DispatchSeparateWindowCommand).  The main WndProc
// 0x004C3A10 is ported in src/window/wndproc.cpp; the separate-window WndProc
// 0x00466A10 and its mouse filter 0x00428FF0 are ported in
// src/window/mic_window.cpp.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>

#include <cstdio>
#include <cstdlib>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

namespace {
// The original deliberately uses a different coefficient for the surviving X
// rotation. x64 rounds after mulss and divss; x86 stores the x87 result once.
float ResetRotationRadians(float degrees, bool xAxis) {
#if defined(_M_X64)
    const float coefficient = xAxis ? 3.141594886779785f : -3.141592025756836f;
    return degrees * coefficient / 180.0f;
#else
    const double coefficient = xAxis ? 3.141594886779785 : -3.141592025756836;
    return static_cast<float>(degrees * coefficient / 180.0);
#endif
}
} // namespace

// 0x52EB80: "May recording be interrupted?" / 0x52EB9C: "Confirm recording interruption"
static const char kJpRecStopText[] =
    "\x98\x5e\x89\xe6\x82\xf0\x92\x86\x92\x66\x82\xb5\x82\xc4\x82\xe0"
    "\x82\xe6\x82\xa2\x82\xc5\x82\xb7\x82\xa9";
static const char kJpRecStopCaption[] = "\x98\x5e\x89\xe6\x92\x86\x92\x66\x8a\x6d\x94\x46";

// ---------------------------------------------------------------------------
// VA 0x00479DA0 - RecWndProc: the fullscreen-recording overlay window's
// procedure.  WM_PAINT = bare Begin/EndPaint; WM_CLOSE and WM_KEYDOWN
// (only VK_ESCAPE) ask for confirmation (MB_ICONWARNING|YESNO = 0x24, JP
// or EN text by the app English flag, global app at 0x54593C) and run the
// recording epilogue 0x464A00 on IDYES; everything else DefWindowProc.
// ---------------------------------------------------------------------------
LRESULT CALLBACK RecWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MMDApp* app = g_Block;
    if (msg == WM_PAINT) { // 0x479DBC
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg != WM_CLOSE && msg != WM_KEYDOWN) // 0x479DD0
        return DefWindowProcA(hwnd, msg, wp, lp);
    if (msg == WM_KEYDOWN && wp != 27 /*VK_ESCAPE*/)
        return 0;
    const bool english = app != nullptr && app->EnglishUI(); // 0xA0B4C
    const int choice =
        english
            ? MessageBoxA(hwnd, "Do you want to stop recording?", "recording", 0x24u)
            : MessageBoxA(hwnd, kJpRecStopText, kJpRecStopCaption, (MB_YESNO | MB_ICONQUESTION));
    if (choice == 6 /*IDYES*/) {
        FinishAviRecord(app); // 0x464A00
        return 0;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// VA 0x004620A0 - DispatchSeparateWindowCommand(app, id):
//   separate-window WM_COMMAND dispatch
// over child controls 0x218..0x22D (called from the separate WndProc at
// 0x466B9E with LOWORD(wParam)).  Cases verbatim:
//   0x218 camera/light/accessory combo 436 re-select (stored index 0xA046C
//         or 0; ApplyModelComboSelection; no-index+model-mode reposts WM_COMMAND 0x1B3)
//   0x219/0x21A/0x21B axis clear: model-mode branch zeroes bone position
//         x/y/z (&bones[idx+320/324/328], idx = model+0x2D90, guard <0)
//         after PushBoneEditUndo undo, marks the bone edit flag (model+0x2DA8
//         byte array + idx); camera branch zeroes 0x334/0x338/0x33C ->
//         820/824/828 + SelectTimelineTrack(GlobalTimelineTrack::Camera)
//   0x21C/0x21D/0x21E rotation clear (model branch): degree caches
//         0xA04C0/0xA04C4/0xA04C8 folded to radians in place (note the
//         TWO distinct pi constants 3.141592025756836/3.141594886779785),
//         R = RotZ * RotX * RotY via d3dx9_32.dll, quaternion written to
//         &bones[idx].rotQuat[0], edit flag set; camera branch zeroes
//         0x310/0x314/0x318 (784/788/792)
//   0x21F camera angle z (0xA08DC) zero
//   0x227 toggle byte 0x31E + menu 0xD3 (off path also zeroes 0x320/0x324)
//   0x228 menu 0x12B check toggle + byte 0xA4220
//   0x229 frame edit 554: atol, clamp >= 0, frame 0x980, 0x432FA0 +
//         PostViewRefresh + "%d" echo into edit 417
//   0x22B "%d" frame echo back into edit 554
//   0x22C checkbox 556: BM_GETCHECK -> 4096/2048 into wrapper
//         renderTargetWidth/renderTargetHeight (+0x1D558/+0x1D55C), then
//         Release() of the three COM objects hdrTexture/shadowDepthSurface/
//         shadowSurface (+0x1D548/+0x1D554/+0x1D550)
//   0x22D toggle byte 0x31D + menu 0xD7
// ---------------------------------------------------------------------------
void DispatchSeparateWindowCommand(MMDApp* app, unsigned short id) { // VA 0x004620A0
    auto& s = *app;
    HWND main = static_cast<HWND>(s.Hwnd());
    HWND separate = s.FloatingWindow();
    char text[0x100];
    switch (id) {
    case panel::kModelEditToggle: { // 0x4620D5
        if (s.CameraMode() != 0) {
            const std::int32_t sel = s.MainModelComboSelection();
            if (sel != 0) {
                SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_SETCURSEL,
                             static_cast<WPARAM>(sel), 0);
                ApplyModelComboSelection(app); // 0x462127
            } else {
                // The original re-dispatches command 0x1B3 here.
                SendMessageA(main, WM_COMMAND, panel::kLoadModelButton, 0); // 0x4620FB
            }
        } else {
            SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_SETCURSEL, 0, 0);
            ApplyModelComboSelection(app); // 0x462153
        }
        break;
    }
    case panel::kResetPositionXButton: // 0x46215D
    case panel::kResetPositionYButton:
    case panel::kResetPositionZButton: {
        const std::size_t axis = id - panel::kResetPositionXButton; // 0/1/2 -> +320/+324/+328
        if (s.CameraMode() != 0) {
            s.CameraPosition()[axis] = 0.0f;
            SelectTimelineTrack(GlobalTimelineTrack::Camera); // 0x440AC0
            PostViewRefresh(app);                             // 0x40D130
        } else {
            mdl::ModelRecord* model = s.SelectedModel();
            const std::int32_t bone = model->selectedBone;
            if (bone >= 0) {
                PushBoneEditUndo(app); // 0x42D6E0
                model->boneTable[bone].trans[axis] = 0.0f;
                model->bonePhysicsState[bone] = 1;
                PostViewRefresh(app);
            } else {
                PostViewRefresh(app); // 0x46252E
            }
        }
        break;
    }
    case panel::kResetRotationXButton: // 0x462314
    case panel::kResetRotationYButton:
    case panel::kResetRotationZButton: {
        const int axis = id - panel::kResetRotationXButton;
        if (s.CameraMode() == 0) {
            mdl::ModelRecord* model = s.SelectedModel();
            const std::int32_t bone = model->selectedBone;
            if (bone >= 0) {
                PushBoneEditUndo(app);
                // These caches hold radians temporarily, until PostViewRefresh
                // extracts degrees from the new quaternion below.
                float& radiansX = s.BoneRotationEditDegreesX();
                float& radiansY = s.BoneRotationEditDegreesY();
                float& radiansZ = s.BoneRotationEditDegreesZ();
                if (axis == 0) { // 0x462356
                    radiansX = 0.0f;
                    radiansY = ResetRotationRadians(radiansY, false);
                    radiansZ = ResetRotationRadians(radiansZ, false);
                } else if (axis == 1) { // 0x462463
                    radiansX = ResetRotationRadians(radiansX, true);
                    radiansY = 0.0f;
                    radiansZ = ResetRotationRadians(radiansZ, false);
                } else { // 0x46259C
                    radiansX = ResetRotationRadians(radiansX, true);
                    radiansY = ResetRotationRadians(radiansY, false);
                    radiansZ = 0.0f;
                }
                auto& api = d3dx::Get();
                d3dx::D3DXMATRIXF rz{}, rx{}, ry{}, rot{};
                api.rotZ(&rz, radiansZ);
                api.rotX(&rx, radiansX);
                api.multiply(&rot, &rz, &rx);
                api.rotY(&ry, radiansY);
                api.multiply(&rot, &rot, &ry);
                api.quatFromMatrix(model->boneTable[bone].rotQuat, &rot); // 0x462623
                model->bonePhysicsState[bone] = 1;
            }
            PostViewRefresh(app); // 0x46252E
        } else {
            s.CameraRotation()[axis] = 0.0f;
            SelectTimelineTrack(GlobalTimelineTrack::Camera);
            PostViewRefresh(app);
        }
        break;
    }
    case panel::kCameraDistanceButton: // 0x46266B
        if (s.CameraMode() != 0)
            s.CameraDistance() = 0.0f;
        PostViewRefresh(app);
        break;
    case panel::kInfoCheckbox: { // 0x4626DD
        if (s.FpsOverlayEnabled() != 0) {
            s.FpsOverlayEnabled() = 0;
            CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_INFORMATION, MF_UNCHECKED);
        } else {
            s.FpsOverlayElapsedSeconds() = 0.0f;
            s.FpsOverlayEnabled() = 1;
            s.FpsOverlayFrameCount() = 0;
            CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_INFORMATION, MF_CHECKED);
        }
        break;
    }
    case panel::kFrameVolumeCheckbox: { // 0x46275A
        const bool checked =
            (GetMenuState(GetMenu(main), MDS_CMD_TOGGLE_LOW_POWER, 0) & MF_CHECKED) != 0;
        CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_LOW_POWER, checked ? MF_UNCHECKED : MF_CHECKED);
        s.FrameVolumeControlEnabled() = checked ? 0 : 1;
        break;
    }
    case panel::kGoToFrameButton: { // 0x462810
        GetWindowTextA(GetDlgItem(separate, panel::kGotoFrameEdit), text, 10);
        std::int32_t frame = atol(text);
        if (frame < 0)
            frame = 0;
        s.CurrentFrame() = frame;    // 0x980
        RefreshAfterFrameApply(app); // 0x432FA0
        PostViewRefresh(app);
        sprintf_s(text, 0x100, "%d", s.CurrentFrame());
        SetWindowTextA(GetDlgItem(main, panel::kCurrentFrameEdit), text);
        break;
    }
    case panel::kSetGoToFrameButton: { // 0x4627C6
        sprintf_s(text, 0x100, "%d", s.CurrentFrame());
        SetWindowTextA(GetDlgItem(separate, panel::kGotoFrameEdit), text);
        break;
    }
    case panel::kSelfShadowCheckbox: { // 0x46289B
        const bool on =
            SendMessageA(GetDlgItem(separate, panel::kSelfShadowCheckbox), BM_GETCHECK, 0, 0) == 1;
        const std::int32_t value = on ? 4096 : 2048;
        D3DRenderer* sub = s.Renderer();
        sub->renderTargetWidth = value;  // 0x1D558
        sub->renderTargetHeight = value; // 0x1D55C
        // kRelease[]{0x1D548, 0x1D554, 0x1D550}: release in this order
        // (IUnknown::Release = vtable+8) and null each slot.
        IUnknown* released[] = {sub->hdrTexture,         // 0x1D548
                                sub->shadowDepthSurface, // 0x1D554
                                sub->shadowSurface};     // 0x1D550
        for (IUnknown* com : released) {
            if (com != nullptr) {
                com->Release();
            }
        }
        sub->hdrTexture = nullptr;
        sub->shadowDepthSurface = nullptr;
        sub->shadowSurface = nullptr;
        break;
    }
    case panel::kCoordAxisCheckbox: { // 0x46269C
        if (s.GroundGridEnabled() != 0) {
            s.GroundGridEnabled() = 0;
            CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_COORDINATE_AXIS, MF_UNCHECKED);
        } else {
            s.GroundGridEnabled() = 1;
            CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_COORDINATE_AXIS, MF_CHECKED);
        }
        break;
    }
    default:
        break;
    }
}

} // namespace mikudancestudio
