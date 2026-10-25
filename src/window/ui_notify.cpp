// ===========================================================================
// VA 0x004398B0 - HandleNotify  (original: sub_4398B0, 0x3D5 bytes)
// ===========================================================================
// WM_NOTIFY / NM_CUSTOMDRAW handler for the themed control labels (sole
// caller: WndProc 0x004C3A10).  Original signature:
//   LRESULT __thiscall sub_4398B0(MMDApp* this /*ecx*/, HWND hWnd, UINT Msg,
//                                 WPARAM wParam, LPARAM lParam)
// `this` is the g_Block global (the ported signature has no app parameter).
//
// lParam is read as NMCUSTOMDRAW: hwndFrom +0, idFrom +4, code +8,
// dwDrawStage +12, hdc +16, rc +20 (verified against asm 0x4398B9..0x439AC1).
// Gate: code(lParam+8) == -12 (NM_CUSTOMDRAW) && dwDrawStage(lParam+12) == 1
// (CDDS_PREPAINT), otherwise DefWindowProcA.
//
// Prologue - runs UNCONDITIONALLY before the OR-chain (asm 0x4398D1..0x43993D;
// the decompiler's short-circuit rendering is wrong, both calls are eager):
//   v8      = SendMessageA(GetDlgItem(mainHwnd, 556), 0xF0 /*BM_GETCHECK*/, 0, 0)
//   v10     = (v8 == 1)
//   v16     = dword this+0xA0D30 (658736)   slot-state flag
//   v15     = dword this+0x914  (2324)      slot selection index
//   playbackActive = byte  this+0x330 (816)        playback-active flag
//   floatingShadowCheck  = SendMessageA(GetDlgItem(subWnd, 556), 0xF0 /*BM_GETCHECK*/, 0, 0), where
//             subWnd = this+0xA0D38 (658744) read as HWND
//
// Blue path: SetTextColor(hdc, 0xC86400); DrawControlText(0x41A550); ret 4.
// The 14-term condition (asm 0x439943..0x439A8B) is a fully unrolled bitwise
// OR - every term and every state load evaluates unconditionally:
//   (v9 == 556) && (v10 || floatingShadowCheck == BST_CHECKED)          ctrl 556 checked twice
//   (v9 == 446) && byte(this+0x31C) == 0         // 796
//   (v9 == 492) && v15 == 4      (v9 == 493) && v15 == 3
//   (v9 == 491) && v15 == 1      (v9 == 490) && v15 == 0
//   (v9 == 552) && byte(this+0xA4420)            // 672800
//   (v9 == 557) && byte(this+0x31D)              // 797
//   (v9 == 564) && v16 == 2      (v9 == 551) && byte(this+0x31E)  // 798
//   (v9 == 563) && v16 == 1      (v9 == 562) && v16 == 0
//   (v9 == 535) && byte(this+0x9ED98)            // 650648
//   (v9 == 408) && playbackActive                       // 816
// then the per-control-id chain (linear cmps, asm 0x439AD8..0x439C5F):
//   499 (0x1F3): IsDlgButtonChecked(mainHwnd, 499) == BST_CHECKED -> blue
//   440 (0x1B8): byte(this+0x2F8 /*760*/) != 0 -> DefWindowProcA; else
//       model = dword[this+0x780 + 4*byte(this+0x910)];   // 1920, 2320
//       byte(model+0x37C0 /*14272*/) != 0 -> blue, == 0 -> DefWindowProcA
//   441 (0x1B9): byte(this+760) != 0 -> DefWindowProcA; else
//       byte(model+0x31BE /*12734*/) != 0 -> blue, == 0 -> DefWindowProcA
//   477 (0x1DD): idx = byte(this+0x9E170 /*647536*/);
//       rec = dword[this+0x9DD70 + 4*idx];       // 646512
//       rec == 0 -> DefWindowProcA;
//       byte(rec+0x49E /*1182*/) != 0 -> blue, == 0 -> DefWindowProcA
//   543 (0x21F): SetTextColor(hdc, 0) black + DrawControlText + return 4
//                (no 0xC86400 step)
//   default, playbackActive == 0:
//       537|540 -> SetTextColor(0x96) red
//       538|541 -> SetTextColor(0x6400) green
//       539|542 -> SetTextColor(0x960000) blue
//       each followed by DrawControlText + return CDRF_SKIPDEFAULT; anything else ->
//       DefWindowProcA.
//
// Reference: ../translated/MikuMikuDance/fcn_004398b0.cpp (verified against
// the live IDB disassembly: both SendMessageA calls are unconditional, and
// controls 440/477 take the blue path when the sub-byte is NON-zero).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>

#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// VA 0x0041A550 - defined in ui_control_text.cpp; not yet declared in
// ported_funcs.hpp, so an external-linkage forward declaration lives here
// (NOT in an anonymous namespace - that would shadow the real definition
// and fail to link).  Original is __thiscall on `this` = g_Block, with
// hwnd/hdc/rc as stack args.
void DrawControlText(MMDApp* app, HWND hwnd, HDC hdc, RECT rc);

// lParam viewed as NMCUSTOMDRAW (x86, 4-byte fields, no padding; only the
// fields this handler touches are declared).
struct NotifyCd {
    HWND hwndFrom;        // +0
    std::uint32_t idFrom; // +4  NMHDR.idFrom
    std::int32_t code;    // +8  NMHDR.code
    std::uint32_t stage;  // +12 dwDrawStage
    HDC hdc;              // +16
    RECT rc;              // +20
};

LRESULT HandleNotify(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MMDApp* app = g_Block;
    const NotifyCd* customDraw = reinterpret_cast<const NotifyCd*>(lParam);
    const HWND mainHwnd = static_cast<HWND>(app->state.hwnd); // this+0xA06B8 (657080)

    // Gate: NM_CUSTOMDRAW (-12) on NMHDR.code and CDDS_PREPAINT (1) on
    // dwDrawStage, else default.  (asm 0x4398B9 / 0x4398C7)
    if (customDraw->code != static_cast<std::int32_t>(NM_CUSTOMDRAW) ||
        customDraw->stage != CDDS_PREPAINT)
        return DefWindowProcA(hwnd, message, wParam, lParam);

    // ---- Unconditional prologue (asm 0x4398D1..0x43993D) ----------------
    // First BM_GETCHECK probe: ctrl 556 on the main window.
    const LRESULT mainShadowCheck =
        SendMessageA(GetDlgItem(mainHwnd, panel::kSelfShadowCheckbox), BM_GETCHECK, 0, 0);
    const std::uint32_t shadowMode =
        static_cast<std::uint32_t>(app->state.selfShadowMode); // 658736
    const std::uint32_t editMode = static_cast<std::uint32_t>(app->EditMode());
    const std::uint8_t playbackActive = app->PlaybackActive();
    // Second BM_GETCHECK probe: ctrl 556 on the sub-window this+0xA0D38
    // (658744, read as HWND).  Runs eagerly like the first one.
    const HWND subWnd = app->FloatingWindow();
    const LRESULT floatingShadowCheck =
        SendMessageA(GetDlgItem(subWnd, panel::kSelfShadowCheckbox), BM_GETCHECK, 0, 0);

    const bool mainShadowChecked = (mainShadowCheck == BST_CHECKED);
    const std::uint32_t ctrlId = customDraw->idFrom; // control id (lParam+4)

    // ---- 14-term useActiveTextColor test (asm 0x439943..0x439A8B) ----------------------
    // Fully unrolled bitwise OR in the original - no short-circuit; every
    // term below must therefore be evaluated.  Mirrored with &/| on bools.
    const bool useActiveTextColor = static_cast<bool>(
        ((floatingShadowCheck == BST_CHECKED) | mainShadowChecked) &
            (ctrlId == panel::kSelfShadowCheckbox) // ctrl 556
        | (ctrlId == panel::kPerspectiveCheckbox) & (app->CameraPerspective() == 0) |
        (ctrlId == panel::kBoneMoveRadio) & (editMode == 4) |
        (ctrlId == panel::kBoneRotateRadio) & (editMode == 3) |
        (ctrlId == panel::kBoxSelectRadio) & (editMode == 1) |
        (ctrlId == panel::kBoneSelectRadio) & (editMode == 0) |
        (ctrlId == panel::kFrameVolumeCheckbox) & app->FrameVolumeControlEnabled() // 672800
        | (ctrlId == panel::kCoordAxisCheckbox) & app->state.groundGridEnabled     // 797
        | (ctrlId == panel::kEditMode2Checkbox) & (shadowMode == 2) |
        (ctrlId == panel::kInfoCheckbox) & app->state.fpsOverlayEnabled // 798
        | (ctrlId == panel::kEditMode1Checkbox) & (shadowMode == 1) |
        (ctrlId == panel::kEditOffCheckbox) & (shadowMode == 0) |
        (ctrlId == panel::kFollowCameraCheckbox) & app->state.followCameraEnabled // 650648
        | (ctrlId == panel::kPlayButton) & (playbackActive != 0));                // 816

    if (useActiveTextColor) {
        // Blue path (asm 0x439A8F..0x439AD5): SetTextColor then
        // DrawControlText(hwndFrom, hdc, rc), return 4.
        SetTextColor(customDraw->hdc, 0xC86400);
        DrawControlText(app, customDraw->hwndFrom, customDraw->hdc, customDraw->rc);
        return CDRF_SKIPDEFAULT;
    }

    // ---- Per-control-id chain (asm 0x439AD8..0x439C5F) ------------------
    if (ctrlId == panel::kPhysicsCheckbox) { // 0x1F3
        // Blue iff the 499 checkbox on the main window is checked.
        if (IsDlgButtonChecked(mainHwnd, panel::kPhysicsCheckbox) == BST_CHECKED) {
            SetTextColor(customDraw->hdc, 0xC86400);
            DrawControlText(app, customDraw->hwndFrom, customDraw->hdc, customDraw->rc);
            return CDRF_SKIPDEFAULT;
        }
        return DefWindowProcA(hwnd, message, wParam, lParam);
    }

    if (ctrlId == panel::kShadowCheckbox) { // 0x1B8
        if (app->CameraMode() != 0)         // this+0x2F8 (760)
            return DefWindowProcA(hwnd, message, wParam, lParam);
        const std::uint8_t idx = app->SelectedModelSlot();
        const mdl::ModelRecord& model = *app->ModelSlot(idx);
        // Blue iff the model's byte +0x37C0 (14272) is non-zero.
        if (model.toonFlag == 0)
            return DefWindowProcA(hwnd, message, wParam, lParam);
        SetTextColor(customDraw->hdc, 0xC86400);
        DrawControlText(app, customDraw->hwndFrom, customDraw->hdc, customDraw->rc);
        return CDRF_SKIPDEFAULT;
    }

    if (ctrlId == panel::kAddBlendCheckbox) { // 0x1B9
        if (app->CameraMode() != 0)           // this+0x2F8 (760)
            return DefWindowProcA(hwnd, message, wParam, lParam);
        const std::uint8_t idx = app->SelectedModelSlot();
        const mdl::ModelRecord& model = *app->ModelSlot(idx);
        // Blue iff the model's byte +0x31BE (12734) is non-zero.
        if (model.postLoadFlag2 == 0)
            return DefWindowProcA(hwnd, message, wParam, lParam);
        SetTextColor(customDraw->hdc, 0xC86400);
        DrawControlText(app, customDraw->hwndFrom, customDraw->hdc, customDraw->rc);
        return CDRF_SKIPDEFAULT;
    }

    if (ctrlId == panel::kAccessoryAddBlendCheckbox) { // 0x1DD
        const std::uint8_t idx = app->SelectedAccessorySlot();
        const mdl::AccessoryRecord* rec = app->AccessorySlot(idx);
        if (rec == nullptr)
            return DefWindowProcA(hwnd, message, wParam, lParam);
        if (rec->additiveBlend == 0)
            return DefWindowProcA(hwnd, message, wParam, lParam);
        SetTextColor(customDraw->hdc, 0xC86400);
        DrawControlText(app, customDraw->hwndFrom, customDraw->hdc, customDraw->rc);
        return CDRF_SKIPDEFAULT;
    }

    if (ctrlId == panel::kCameraDistanceButton) { // 0x21F
        // Black (no 0xC86400 step - jumps straight to the SetTextColor call
        // with color 0 pushed, asm 0x439BE2).
        SetTextColor(customDraw->hdc, 0);
        DrawControlText(app, customDraw->hwndFrom, customDraw->hdc, customDraw->rc);
        return CDRF_SKIPDEFAULT;
    }

    // Default: red/green/blue variants, all requiring playbackActive == 0
    // (this+0x330 / 816).  Each draws with DrawControlText and returns 4.
    if ((ctrlId == panel::kResetPositionXButton || ctrlId == panel::kResetRotationXButton) &&
        playbackActive == 0) {
        SetTextColor(customDraw->hdc, 0x96); // dark red (BGR)
        DrawControlText(app, customDraw->hwndFrom, customDraw->hdc, customDraw->rc);
        return CDRF_SKIPDEFAULT;
    }
    if ((ctrlId == panel::kResetPositionYButton || ctrlId == panel::kResetRotationYButton) &&
        playbackActive == 0) {
        SetTextColor(customDraw->hdc, 0x6400); // green (BGR)
        DrawControlText(app, customDraw->hwndFrom, customDraw->hdc, customDraw->rc);
        return CDRF_SKIPDEFAULT;
    }
    if ((ctrlId == panel::kResetPositionZButton || ctrlId == panel::kResetRotationZButton) &&
        playbackActive == 0) {
        SetTextColor(customDraw->hdc, 0x960000); // blue (BGR)
        DrawControlText(app, customDraw->hwndFrom, customDraw->hdc, customDraw->rc);
        return CDRF_SKIPDEFAULT;
    }

    return DefWindowProcA(hwnd, message, wParam, lParam);
}

} // namespace mikudancestudio
