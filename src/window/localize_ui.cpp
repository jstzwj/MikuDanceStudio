// ===========================================================================
// VA 0x00441AD0 - LocalizeUI  (original: sub_441AD0, 0x13D3 bytes)
// ===========================================================================
// UI language pass + selector population, called from 0x0047A5B0 right
// after CreateWindowExA (controls were created by 0x00466D20 during
// WM_CREATE).  NOT the D3D init - reclassified from the earlier stub name.
//
//   1. combobox 433 (bone-mode): reset + add entries
//      "x/y/z axis move", "rotation" (+ "distance","view angle" in camera
//      mode, this+760) + "all"; select index 3.
//   2. per-control texts for ids 400..567 (English literals below; the JP
//      branch re-sets the same strings the controls were created with -
//      restored from ui_controls.inc by id, single source of truth).
//   3. model-slot language flag sweep: for each of the 100 slots at
//      this+1920, physicsFlags(slot) = english (x86 12740 / x64 0x3560 =
//      13664, x64 store at 0x7FF7CB43A4F5); subobject(this+204)+596 too.
//   4. InvalidateRect + sub_42F1E0 + sub_40D070 [stubs]
//   5. camera-mode branch (this+760): combobox 434 entries
//      gravity/s shadow/light/camera (EN) else sub_49C850(slot).
//   6. selector comboboxes 436/474/449: save cur selection, reset, add
//      "camera/light/accessory" / "ground" / JP/EN third label, add every
//      loaded model's name (nameEn/name: x86 8826/8776, x64 8946/8896 -
//      x64 fills at 0x7FF7CB43A9AE/0x7FF7CB43AA21), restore selection.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/panel_controls.hpp"
#include <Windows.h>

#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "ui_controls.inc"

namespace mikudancestudio {
namespace {

struct TextEntry {
    int id;
    const char* en;
};

// English texts in original call order (0x441AD0 EN branch).
constexpr TextEntry kEnTexts[] = {
    {panel::kUndoButton, "undo"},
    {panel::kRedoButton, "redo"},
    {panel::kPreviousFrameButton, "<"},
    {panel::kNextFrameButton, ">"},
    {panel::kShowCurrentFrameButton, "current"},
    {panel::kSelectColumnButton, "v-sel"},
    {panel::kDeleteFramesButton, "delete"},
    {panel::kCopyFramesButton, "copy"},
    {panel::kPasteButton, "paste"},
    {panel::kReversePasteButton, "revers"},
    {panel::kSelectRangeButton, "range-sel"},
    {panel::kExpandShrinkButton, "expand"},
    {panel::kCurveLinearButton, "liner"},
    {panel::kCurvePasteButton, "paste"},
    {panel::kCurveCopyButton, "copy"},
    {panel::kLoadModelButton, "load"},
    {panel::kDeleteModelButton, "delete"},
    {panel::kShadowCheckbox, "shadow"},
    {panel::kAddBlendCheckbox, "Add-syn"},
    {panel::kEditExternalParentButton, "OP"},
    {panel::kRegisterModelDisplayButton, "register"},
    {panel::kResetCameraButton, "reset"},
    {panel::kPerspectiveCheckbox, "perspect"},
    {panel::kRegisterCameraButton, "register"},
    {panel::kExpandCameraPanelButton, "+"},
    {panel::kCollapseCameraPanelButton, "-"},
    {panel::kResetLightButton, "reset"},
    {panel::kRegisterLightButton, "register"},
    {panel::kEditOffCheckbox, "off"},
    {panel::kEditMode1Checkbox, "mode1"},
    {panel::kEditMode2Checkbox, "mode2"},
    {panel::kRegisterSelfShadowButton, "register"},
    {panel::kExpandLightPanelButton, "+"},
    {panel::kCollapseLightPanelButton, "-"},
    {panel::kExpandAccessoryPanelButton, "+"},
    {panel::kCollapseAccessoryPanelButton, "-"},
    {panel::kLoadAccessoryButton, "load"},
    {panel::kDeleteAccessoryButton, "delete"},
    {panel::kAccessoryAddBlendCheckbox, "Add-syn"},
    {panel::kRegisterAccessoryButton, "register"},
    {panel::kViewFrontButton, "front"},
    {panel::kViewBackButton, "back"},
    {panel::kViewTopButton, "top"},
    {panel::kViewLeftButton, "left"},
    {panel::kViewRightButton, "right"},
    {panel::kViewBottomButton, "btm"},
    {panel::kPlayButton, "play"},
    {panel::kFollowCameraCheckbox, "track"},
    {panel::kBoneSelectRadio, "select"},
    {panel::kBoneRotateRadio, "rotate"},
    {panel::kBoneMoveRadio, "move"},
    {panel::kBoxSelectRadio, "BOX-sel"},
    {panel::kSelectAllBonesButton, "select all"},
    {panel::kSelectUnregisteredBonesButton, "unregisted"},
    {panel::kCopyBonePoseButton, "copy"},
    {panel::kBonePasteButton, "paste"},
    {panel::kBoneReversePasteButton, "revers"},
    {panel::kRegisterFrameButton, "register"},
    {panel::kPhysicsCheckbox, "physics"},
    {panel::kResetBonePoseButton, "reset"},
    {panel::kExpandBonePanelButton, "+"},
    {panel::kCollapseBonePanelButton, "-"},
    {panel::kExpandMorphPanelButton, "+"},
    {panel::kCollapseMorphPanelButton, "-"},
    {panel::kExpandSelfShadowPanelButton, "+"},
    {panel::kCollapseSelfShadowPanelButton, "-"},
    {panel::kRegisterMorphButton1, "register"},
    {panel::kRegisterMorphButton0, "register"},
    {panel::kRegisterMorphButton2, "register"},
    {panel::kRegisterMorphButton3, "register"},
    {panel::kCameraDistanceButton, "dist"},
    {panel::kInfoCheckbox, "info"},
    {panel::kFrameVolumeCheckbox, "low pow"},
    {panel::kCoordAxisCheckbox, "axis"},
    {panel::kSetGoToFrameButton, "set"},
    {panel::kSelfShadowCheckbox, "Fshadow"},
};

HWND Dlg(MMDApp* app, int id) {
    return GetDlgItem(static_cast<HWND>(app->Hwnd()), id);
}

const wchar_t* JpControlText(int id) {
    // JP branch re-sets the creation strings; single source = ui_controls.inc
    // (regenerated dual-encoding table: wide-variant captions come back
    // verbatim, ANSI-variant captions are ASCII and widened in place, which
    // matches the original's SetWindowTextW re-set of the same strings).
    static wchar_t buf[64];
    for (const ui::ControlSpec& c : ui::kControls) {
        if (c.id != id)
            continue;
        if (c.wtext != nullptr)
            return c.wtext;
        int i = 0;
        for (; i < 63 && c.atext != nullptr && c.atext[i] != '\0'; ++i)
            buf[i] = static_cast<unsigned char>(c.atext[i]);
        buf[i] = L'\0';
        return buf;
    }
    return L"";
}

} // namespace

void LocalizeUI(MMDApp* app) {
    auto& s = *app;
    const bool english = s.EnglishUI() != 0;     // 658252
    const bool cameraMode = s.CameraMode() != 0; // 760

    HWND interpolationChannelCombo = Dlg(app, panel::kInterpCurveCombo);
    SendMessageA(interpolationChannelCombo, CB_RESETCONTENT, 0, 0);

    if (english) {
        for (const TextEntry& t : kEnTexts)
            SetWindowTextA(Dlg(app, t.id), t.en);
        SetWindowTextA(Dlg(app, panel::kModelEditToggle), cameraMode ? "To model" : "camera");
        SendMessageA(interpolationChannelCombo, CB_ADDSTRING, 0, (LPARAM) "x axis move");
        SendMessageA(interpolationChannelCombo, CB_ADDSTRING, 0, (LPARAM) "y axis move");
        SendMessageA(interpolationChannelCombo, CB_ADDSTRING, 0, (LPARAM) "z axis move");
        SendMessageA(interpolationChannelCombo, CB_ADDSTRING, 0, (LPARAM) "rotation");
        if (cameraMode) {
            SendMessageA(interpolationChannelCombo, CB_ADDSTRING, 0, (LPARAM) "distance");
            SendMessageA(interpolationChannelCombo, CB_ADDSTRING, 0, (LPARAM) "view angle");
        }
        SendMessageA(interpolationChannelCombo, CB_ADDSTRING, 0, (LPARAM) "all");
    } else {
        for (const TextEntry& t : kEnTexts)
            SetWindowTextW(Dlg(app, t.id), JpControlText(t.id));
        SetWindowTextW(Dlg(app, panel::kModelEditToggle), JpControlText(panel::kModelEditToggle));
        // JP combo strings verified byte-exact against the push sequence
        // 0x442909..0x442991 of the original: X translation(0x52D3D4) Y translation(0x52D3CC)
        // Z translation(0x52D3C4) rotation(0x52D470, full-width space) [distance(0x52D3BC)
        // view angle(0x52D3B4) in camera mode] all(0x52D3AC).
        SendMessageW(interpolationChannelCombo, CB_ADDSTRING, 0,
                     (LPARAM)L"\xff38\x79fb\x52d5"); // 0x52D3D4
        SendMessageW(interpolationChannelCombo, CB_ADDSTRING, 0,
                     (LPARAM)L"\xff39\x79fb\x52d5"); // 0x52D3CC
        SendMessageW(interpolationChannelCombo, CB_ADDSTRING, 0,
                     (LPARAM)L"\xff3a\x79fb\x52d5"); // 0x52D3C4
        SendMessageW(interpolationChannelCombo, CB_ADDSTRING, 0,
                     (LPARAM)L"\x56de\x3000\x8ee2"); // 0x52D470
        if (cameraMode) {
            SendMessageW(interpolationChannelCombo, CB_ADDSTRING, 0,
                         (LPARAM)L"\x8ddd\x3000\x96e2"); // 0x52D3BC
            SendMessageW(interpolationChannelCombo, CB_ADDSTRING, 0,
                         (LPARAM)L"\x8996\x91ce\x89d2"); // 0x52D3B4
        }
        SendMessageW(interpolationChannelCombo, CB_ADDSTRING, 0,
                     (LPARAM)L"\x3059\x3079\x3066"); // 0x52D3AC
    }
    SendMessageA(interpolationChannelCombo, CB_SETCURSEL, 3, 0);

    // model-slot language flag sweep (20 x 5 slots at this+1920); the x64
    // twin sub_7FF7CB438E70 sweeps all 255 slots (mov r8d, 0FFh ... dec r8
    // at 0x7FF7CB43A4E0), storing the app language byte into model+0x3560
    // (= physicsFlags, x64 store at 0x7FF7CB43A4F5; x86 twin 12740 - the
    // same typed field on both ABIs, never a raw offset).
    for (int i = 0; i < kModelSlotCount; ++i) {
        unsigned char* slot = s.ModelSlot(i);
        if (slot != nullptr)
            mdl::Mdl(slot)->physicsFlags = static_cast<unsigned char>(english);
    }
    if (s.Audio() != nullptr)
        s.Audio()->englishUI = static_cast<unsigned char>(english);

    InvalidateRect(static_cast<HWND>(s.Hwnd()), nullptr, FALSE);
    RebuildTimelineTrackPanel(app);   // 0x42F1E0
    InvalidateSelectionReadouts(app); // 0x40D070

    if (cameraMode) {
        HWND combo434 = Dlg(app, panel::kRegisterScopeCombo);
        SendMessageA(combo434, CB_DELETESTRING, 0, 0);
        SendMessageA(combo434, CB_DELETESTRING, 0, 0);
        SendMessageA(combo434, CB_DELETESTRING, 0, 0);
        if (english) {
            SendMessageA(combo434, CB_INSERTSTRING, 0, (LPARAM) "gravity");
            SendMessageA(combo434, CB_INSERTSTRING, 0, (LPARAM) "s shadow");
            SendMessageA(combo434, CB_INSERTSTRING, 0, (LPARAM) "light");
            SendMessageA(combo434, CB_INSERTSTRING, 0, (LPARAM) "camera");
        } else {
            // JP strings verified byte-exact: gravity(0x52D3A4) self-shadow(0x52D398)
            // lighting(0x52D390) camera(0x52B784).
            SendMessageW(combo434, CB_INSERTSTRING, 0,
                         (LPARAM)L"\x91cd\x529b"); // 0x52D3A4
            SendMessageW(combo434, CB_INSERTSTRING, 0,
                         (LPARAM)L"\x30bb\x30eb\x30d5\x5f71"); // 0x52D398
            SendMessageW(combo434, CB_INSERTSTRING, 0,
                         (LPARAM)L"\x7167\x660e"); // 0x52D390
            SendMessageW(combo434, CB_INSERTSTRING, 0,
                         (LPARAM)L"\x30ab\x30e1\x30e9"); // 0x52B784
        }
        SendMessageA(combo434, CB_SETCURSEL, 0, 0);
    } else {
        PostLoadInit(s.SelectedModel()); // 0x49C850
    }

    // selector comboboxes 436/474/449: preserve selection across refill
    HWND combo436 = Dlg(app, panel::kMainComboModel), combo474 = Dlg(app, panel::kMainComboGround),
         combo449 = Dlg(app, panel::kMainComboNormal);
    LRESULT sel436 = SendMessageA(combo436, CB_GETCURSEL, 0, 0);
    LRESULT sel474 = SendMessageA(combo474, CB_GETCURSEL, 0, 0);
    LRESULT sel449 = SendMessageA(combo449, CB_GETCURSEL, 0, 0);
    SendMessageA(combo436, CB_RESETCONTENT, 0, 0);
    SendMessageA(combo474, CB_RESETCONTENT, 0, 0);
    SendMessageA(combo449, CB_RESETCONTENT, 0, 0);
    if (english) {
        SendMessageA(combo436, CB_ADDSTRING, 0, (LPARAM) "camera/light/accessory"); // 0x52D2AC
        SendMessageA(combo474, CB_ADDSTRING, 0, (LPARAM) "ground");                 // 0x52D2A4
        SendMessageA(combo449, CB_ADDSTRING, 0, (LPARAM) "non");                    // 0x52D38C
    } else {
        // Verified byte-exact: 0x52D370 camera/lighting/accessory / 0x52D368 ground /
        // 0x52D360 なし (push sequence 0x442CE0..0x442D31).
        SendMessageW(combo436, CB_ADDSTRING, 0,
                     (LPARAM)L"\xff76\xff92\xff97\xff65\x7167\x660e"
                             L"\xff65\xff71\xff78\xff7e\xff7b\xff98"); // 0x52D370
        SendMessageW(combo474, CB_ADDSTRING, 0,
                     (LPARAM)L"\x5730\x9762"); // 0x52D368
        SendMessageW(combo449, CB_ADDSTRING, 0,
                     (LPARAM)L"\x306a\x3057"); // 0x52D360
    }

    // add every loaded model's name to all three selectors; x64 twin
    // sub_7FF7CB438E70 runs the comboSelIndex (model+0x3108) slot scan and
    // the order walk to 255 (cmp edx, 0FFh at 0x7FF7CB43A96C / cmp ebp,
    // 0FFh at 0x7FF7CB43AA8D) - same family as english_name_dialog.cpp.
    for (int order = 1; order < kModelSlotCount; ++order) {
        int slotIdx = -1;
        for (int i = 0; i < kModelSlotCount; ++i) {
            unsigned char* slot = s.ModelSlot(i);
            if (slot != nullptr && mikudancestudio::mdl::Mdl(slot)->comboSelIndex == order) {
                slotIdx = i;
                break;
            }
        }
        if (slotIdx < 0)
            continue;
        unsigned char* slot = s.ModelSlot(slotIdx);
        mdl::ModelRecord* model = mdl::Mdl(slot);
        // nameEn/name: x86 8826/8776, x64 8946/8896 (x64 English fills
        // use model+8946 at 0x7FF7CB43A9AE, Japanese model+8896 at
        // 0x7FF7CB43AA21) - always through the typed buffers.
        LPARAM name = reinterpret_cast<LPARAM>(english ? model->nameEn : model->name);
        SendMessageA(combo436, CB_ADDSTRING, 0, name);
        SendMessageA(combo474, CB_ADDSTRING, 0, name);
        SendMessageA(combo449, CB_ADDSTRING, 0, name);
    }

    SendMessageA(combo436, CB_SETCURSEL, sel436, 0);
    SendMessageA(combo474, CB_SETCURSEL, sel474, 0);
    SendMessageA(combo449, CB_SETCURSEL, sel449, 0);
}

} // namespace mikudancestudio
