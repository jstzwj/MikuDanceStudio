// ===========================================================================
// VA 0x00443300 - HandleWindowSize  (original: sub_443300, ~0x19B1 bytes)
// ===========================================================================
// WM_SIZE layout engine.  Called from MainWndProc (0x4C3A10 WM_SIZE), the
// command dispatch (0x47E8A0) and the scene loaders (0x458F80 / 0x460430 /
// 0x460B30).  After a resize the 168 child controls of the main window are
// fully repositioned:
//
//   1. 0x44331B  clamp: GetWindowRect; if width  > 2560 ->
//      SetWindowPos(cx=2560, SWP_NOZORDER); if height > 1920 ->
//      SetWindowPos(cy=1920, SWP_NOZORDER).  Position is preserved.
//   2. 0x4433A4  sidebar width (this+0xA06C8): GetClientRect; when the
//      accessory-column gate (this+0xA0D38) is set the sidebar is client
//      width - 3 and the label sweep RebuildTimelineTrackPanel (0x42F1E0) runs;
//      otherwise sidebar = max(250, (int)(ratio @0xA4428 * client width))
//      and PanelPaint (0x414610) runs when the sidebar grew.
//   3. 0x4433F5  control layout, three coordinate families:
//      - left tool row (ids 400..559): X = sidebar/2 + k (or sidebar - k,
//        fixed 15 for 558), constant Y/W/H.
//      - main grid (ids 427..453): Y = clientBottom - k (stretched to the
//        window bottom), X = sidebar-derived or fixed; several controls get
//        W/H = clientBottom - k (e.g. 427 H, 428 W, 434/436/443 big panels).
//      - option bands (ids 455..535): X = const - bandXBase / const -
//        bandXBase2 where
//        the two bases accumulate conditional deltas from the UI option
//        flag bytes this+0x2F8..0x2FE (kByteOptflag0..6), see the body.
//   4. 0x444B3B  visibility re-toggle: five id ranges (455..469, 471..488,
//      504..528, 402..414, 531..535) get ShowWindow(SW_HIDE) followed by
//      ShowWindow(SW_SHOW) when IsWindowVisible.
//   5. 0x444C9D  refresh tail: RefreshMainWindowViewport (0x42C810) then 0x40CAC0
//      (0x40CAC0).
//
// All MoveWindow calls pass bRepaint = TRUE; every GetDlgItem handle is
// consumed by the immediately following MoveWindow, so the per-control
// sequence is rendered through one file-static helper (control order and
// parameters are unchanged).
//
// Reference: ../translated/MikuMikuDance/fcn_00443300.cpp is a partial
// auto-generated dump (coordinates/IDs mislabelled past 0x44365E); this
// port follows the live IDA disassembly/pseudocode of VA 0x00443300.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/panel_controls.hpp"
#include <Windows.h>

#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

namespace {

// GetDlgItem + MoveWindow pair as in the original (0x44341E..0x443427 etc.).
void MoveCtrl(HWND parent, int id, int x, int y, int w, int h) {
    MoveWindow(GetDlgItem(parent, id), x, y, w, h, TRUE);
}

// Visibility re-toggle loop body (0x444B4A..0x444B6F and the four siblings).
void ToggleVisible(HWND parent, int firstId, int lastId) {
    for (int id = firstId; id <= lastId; ++id) {
        HWND ctrl = GetDlgItem(parent, id);
        if (IsWindowVisible(ctrl)) {
            ShowWindow(ctrl, SW_HIDE);
            ShowWindow(ctrl, SW_SHOW);
        }
    }
}

} // namespace

void PanelPaint(MMDApp* app);           // VA 0x00414610 (defined in ui_panel_paint.cpp)
void LayoutViewportPanels(MMDApp* app); // VA 0x0040CAC0
                                        // (ui_viewport_layout.cpp)

void HandleWindowSize(MMDApp* app) {
    auto& s = *app;
    const HWND hwnd = static_cast<HWND>(app->Hwnd()); // 657080 (0xA06B8)

    // --- clamp the window to 2560 x 1920 (0x44331B..0x443391) --------------
    RECT wr;
    GetWindowRect(hwnd, &wr);
    if (wr.right - wr.left > 2560)
        SetWindowPos(hwnd, nullptr, wr.left, wr.top, 2560, wr.bottom - wr.top, SWP_NOZORDER);
    GetWindowRect(hwnd, &wr);
    if (wr.bottom - wr.top > 1920)
        SetWindowPos(hwnd, nullptr, wr.left, wr.top, wr.right - wr.left, 1920, SWP_NOZORDER);

    // --- sidebar width (0x4433A4..0x4433F2) --------------------------------
    RECT cr1;
    GetClientRect(hwnd, &cr1); // var_10 - only .right feeds the sidebar
    std::int32_t& sidebar = s.SidebarWidth();
    if (s.FloatingWindow() != nullptr) {
        sidebar = cr1.right - 3;        // 0x4433B8
        RebuildTimelineTrackPanel(app); // 0x4433BE
    } else {
        std::int32_t newSidebar = static_cast<std::int32_t>(static_cast<double>(s.SidebarRatio()) *
                                                            cr1.right); // 0x4433CF
        if (newSidebar < 250)
            newSidebar = 250;                   // 0x4433D9
        const bool grew = sidebar < newSidebar; // 0x4433E0
        sidebar = newSidebar;                   // 0x4433E6
        if (grew)
            PanelPaint(app); // 0x4433EC
    }
    const std::int32_t side = sidebar;
    RECT cb;                  // var_30
    GetClientRect(hwnd, &cb); // 0x443563
    const std::int32_t bottom = cb.bottom;

    // --- left tool row (0x4433F5..0x443555) --------------------------------
    MoveCtrl(hwnd, panel::kUndoButton, side / 2 - 80, 21, 70, 24);
    MoveCtrl(hwnd, panel::kRedoButton, side / 2 + 10, 21, 70, 24);
    MoveCtrl(hwnd, panel::kPreviousFrameButton, side / 2 - 49, 67, 20, 24);
    MoveCtrl(hwnd, panel::kNextFrameButton, side / 2 + 29, 67, 20, 24);
    MoveCtrl(hwnd, panel::kPreviousKeyframeButton, side / 2 - 79, 69, 20, 22);
    MoveCtrl(hwnd, panel::kNextKeyframeButton, side / 2 + 59, 69, 20, 22);
    MoveCtrl(hwnd, panel::kCurrentFrameEdit, side / 2 - 27, 66, 54, 24);
    MoveCtrl(hwnd, panel::kLastFrameButton, side - 30, 69, 20, 22);
    MoveCtrl(hwnd, panel::kFirstFrameButton, 15, 69, 20, 22);

    // --- main grid, anchored to the client bottom (0x443563..0x443B00) -----
    MoveCtrl(hwnd, panel::kTimelineVScroll, side - 19, 162, 16, bottom - 410);
    MoveCtrl(hwnd, panel::kTimelineHScroll, 97, bottom - 247, side - 116, 16);
    MoveCtrl(hwnd, panel::kShowCurrentFrameButton, 8, bottom - 247, 80, 18);
    MoveCtrl(hwnd, panel::kSelectColumnButton, side / 2 + 15, bottom - 224, 50, 18);
    MoveCtrl(hwnd, panel::kDeleteFramesButton, side / 2 + 70, bottom - 224, 50, 18);
    MoveCtrl(hwnd, panel::kCopyFramesButton, side / 2 - 119, bottom - 224, 40, 18);
    MoveCtrl(hwnd, panel::kPasteButton, side / 2 - 76, bottom - 224, 40, 18);
    MoveCtrl(hwnd, panel::kReversePasteButton, side / 2 - 33, bottom - 224, 40, 18);
    MoveCtrl(hwnd, panel::kRangeStartEdit, side / 2 - 3, bottom - 201, 50, 14);
    MoveCtrl(hwnd, panel::kRangeStopEdit, side / 2 + 69, bottom - 201, 50, 14);
    MoveCtrl(hwnd, panel::kRegisterScopeCombo, side / 2 - 118, bottom - 193, 100, 400);
    MoveCtrl(hwnd, panel::kSelectRangeButton, side / 2 - 4, bottom - 181, 60, 18);
    MoveCtrl(hwnd, panel::kExpandShrinkButton, side / 2 + 60, bottom - 181, 60, 18);
    MoveCtrl(hwnd, panel::kCurveLinearButton, 144, bottom - 26, 63, 18);
    MoveCtrl(hwnd, panel::kCurvePasteButton, 144, bottom - 52, 63, 18);
    MoveCtrl(hwnd, panel::kCurveCopyButton, 144, bottom - 78, 63, 18);
    MoveCtrl(hwnd, panel::kInterpCurveCombo, 144, bottom - 135, 63, 120);
    MoveCtrl(hwnd, panel::kPhysicsFrameCheckbox, 144, bottom - 105, 13, 13);
    MoveCtrl(hwnd, panel::kMainComboModel, 225, bottom - 138, 125, 200);
    MoveCtrl(hwnd, panel::kLoadModelButton, 224, bottom - 112, 60, 21);
    MoveCtrl(hwnd, panel::kDeleteModelButton, 291, bottom - 112, 60, 21);
    MoveCtrl(hwnd, panel::kRegisterModelDisplayButton, 291, bottom - 35, 60, 28);
    MoveCtrl(hwnd, panel::kModelVisibleCheckbox, 225, bottom - 84, 13, 13);
    MoveCtrl(hwnd, panel::kShadowCheckbox, 267, bottom - 86, 40, 18);
    MoveCtrl(hwnd, panel::kAddBlendCheckbox, 310, bottom - 86, 41, 18);
    MoveCtrl(hwnd, panel::kEditExternalParentButton, 328, bottom - 62, 22, 22);
    MoveCtrl(hwnd, panel::kIkChainCombo, 225, bottom - 61, 100, 100);
    MoveCtrl(hwnd, panel::kIkOnRadio, 227, bottom - 36, 13, 13);
    MoveCtrl(hwnd, panel::kIkOffRadio, 227, bottom - 19, 13, 13);
    MoveCtrl(hwnd, panel::kResetCameraButton, 368, bottom - 139, 70, 23);
    MoveCtrl(hwnd, panel::kPerspectiveCheckbox, 443, bottom - 139, 65, 23);
    MoveCtrl(hwnd, panel::kFovEdit, 477, bottom - 111, 30, 14);
    MoveCtrl(hwnd, panel::kFovSlider, 363, bottom - 95, 151, 20);
    MoveCtrl(hwnd, panel::kRegisterCameraButton, 463, bottom - 40, 45, 33);
    MoveCtrl(hwnd, panel::kMainComboNormal, 368, bottom - 53, 90, 100);
    MoveCtrl(hwnd, panel::kBoneRegisterCombo, 368, bottom - 28, 90, 300);
    MoveCtrl(hwnd, panel::kExpandCameraPanelButton, 363, bottom - 155, 14, 14);
    MoveCtrl(hwnd, panel::kCollapseCameraPanelButton, 363, bottom - 155, 14, 14);

    // --- option band 1: base bandXBase (original v52; 0x443B02..0x443D9E) --
    // bandXBase = -18, or 117 while bone mode is off (optflag[0] set) and the
    // expand toggle optflag[1] is clear.
    const std::uint8_t cameraMode = s.CameraMode();                                    // 760
    const std::uint8_t cameraPanelExpanded = s.PanelExpanded(UiPanel::Camera);         // 761
    const std::uint8_t lightPanelExpanded = s.PanelExpanded(UiPanel::Light);           // 762
    const std::uint8_t accessoryPanelExpanded = s.PanelExpanded(UiPanel::Accessory);   // 763
    const std::uint8_t bonePanelExpanded = s.PanelExpanded(UiPanel::Bone);             // 764
    const std::uint8_t morphPanelExpanded = s.PanelExpanded(UiPanel::Morph);           // 765
    const std::uint8_t selfShadowPanelExpanded = s.PanelExpanded(UiPanel::SelfShadow); // 766

    std::int32_t bandXBase = -18;
    if (cameraMode != 0 && cameraPanelExpanded == 0)
        bandXBase = 117; // 0x443B1A
    MoveCtrl(hwnd, panel::kLightColorSliderR, 514 - bandXBase, bottom - 140, 136, 18);
    MoveCtrl(hwnd, panel::kLightColorEditR, 650 - bandXBase, bottom - 140, 23, 14);
    MoveCtrl(hwnd, panel::kLightColorSliderG, 514 - bandXBase, bottom - 122, 136, 18);
    MoveCtrl(hwnd, panel::kLightColorEditG, 650 - bandXBase, bottom - 122, 23, 14);
    MoveCtrl(hwnd, panel::kLightColorSliderB, 514 - bandXBase, bottom - 104, 136, 18);
    MoveCtrl(hwnd, panel::kLightColorEditB, 650 - bandXBase, bottom - 104, 23, 14);
    MoveCtrl(hwnd, panel::kLightDirSliderX, 514 - bandXBase, bottom - 84, 132, 20);
    MoveCtrl(hwnd, panel::kLightDirEditX, 646 - bandXBase, bottom - 84, 27, 14);
    MoveCtrl(hwnd, panel::kLightDirSliderY, 514 - bandXBase, bottom - 66, 132, 20);
    MoveCtrl(hwnd, panel::kLightDirEditY, 646 - bandXBase, bottom - 66, 27, 14);
    MoveCtrl(hwnd, panel::kLightDirSliderZ, 514 - bandXBase, bottom - 48, 132, 20);
    MoveCtrl(hwnd, panel::kLightDirEditZ, 646 - bandXBase, bottom - 48, 27, 14);
    MoveCtrl(hwnd, panel::kRegisterLightButton, 590 - bandXBase, bottom - 26, 75, 20);
    MoveCtrl(hwnd, panel::kResetLightButton, 524 - bandXBase, bottom - 26, 50, 20);
    MoveCtrl(hwnd, panel::kExpandLightPanelButton, 501 - bandXBase, bottom - 155, 14, 14);
    MoveCtrl(hwnd, panel::kCollapseLightPanelButton, 501 - bandXBase, bottom - 155, 14, 14);

    // --- option band 2: bandXBase += 162 with expand toggle optflag[2] (0x443DB3)
    if (cameraMode != 0 && lightPanelExpanded == 0)
        bandXBase += 162;
    MoveCtrl(hwnd, panel::kExpandSelfShadowPanelButton, 684 - bandXBase, bottom - 155, 14, 14);
    MoveCtrl(hwnd, panel::kCollapseSelfShadowPanelButton, 684 - bandXBase, bottom - 155, 14, 14);
    MoveCtrl(hwnd, panel::kEditOffCheckbox, 688 - bandXBase, bottom - 135, 54, 22);
    MoveCtrl(hwnd, panel::kEditMode1Checkbox, 745 - bandXBase, bottom - 135, 54, 22);
    MoveCtrl(hwnd, panel::kEditMode2Checkbox, 802 - bandXBase, bottom - 135, 54, 22);
    MoveCtrl(hwnd, panel::kSelfShadowRangeSlider, 683 - bandXBase, bottom - 80, 176, 25);
    MoveCtrl(hwnd, panel::kSelfShadowRangeEdit, 820 - bandXBase, bottom - 100, 30, 14);
    MoveCtrl(hwnd, panel::kRegisterSelfShadowButton, 745 - bandXBase, bottom - 36, 54, 30);

    // --- option band 3: bandXBase += 160 with expand toggle optflag[6] (0x443F19)
    if (cameraMode != 0 && selfShadowPanelExpanded == 0)
        bandXBase += 160;
    MoveCtrl(hwnd, panel::kExpandAccessoryPanelButton, 864 - bandXBase, bottom - 155, 14, 14);
    MoveCtrl(hwnd, panel::kCollapseAccessoryPanelButton, 864 - bandXBase, bottom - 155, 14, 14);
    MoveCtrl(hwnd, panel::kAccessoryCombo, 868 - bandXBase, bottom - 140, 125, 150);
    MoveCtrl(hwnd, panel::kLoadAccessoryButton, 867 - bandXBase, bottom - 116, 60, 18);
    MoveCtrl(hwnd, panel::kDeleteAccessoryButton, 934 - bandXBase, bottom - 116, 60, 18);
    MoveCtrl(hwnd, panel::kAccessoryVisibleCheckbox, 1000 - bandXBase, bottom - 142, 13, 13);
    MoveCtrl(hwnd, panel::kAccessoryShadowCheckbox, 1000 - bandXBase, bottom - 127, 13, 13);
    MoveCtrl(hwnd, panel::kAccessoryAddBlendCheckbox, 999 - bandXBase, bottom - 113, 53, 17);
    MoveCtrl(hwnd, panel::kMainComboGround, 868 - bandXBase, bottom - 95, 90, 100);
    MoveCtrl(hwnd, panel::kAttachBoneCombo, 962 - bandXBase, bottom - 95, 90, 300);
    MoveCtrl(hwnd, panel::kAccPosXEdit, 877 - bandXBase, bottom - 70, 48, 14);
    MoveCtrl(hwnd, panel::kAccPosYEdit, 940 - bandXBase, bottom - 70, 48, 14);
    MoveCtrl(hwnd, panel::kAccPosZEdit, 1003 - bandXBase, bottom - 70, 48, 14);
    MoveCtrl(hwnd, panel::kAccRotXEdit, 877 - bandXBase, bottom - 50, 48, 14);
    MoveCtrl(hwnd, panel::kAccRotYEdit, 940 - bandXBase, bottom - 50, 48, 14);
    MoveCtrl(hwnd, panel::kAccRotZEdit, 1003 - bandXBase, bottom - 50, 48, 14);
    MoveCtrl(hwnd, panel::kAccScaleEdit, 877 - bandXBase, bottom - 30, 48, 14);
    MoveCtrl(hwnd, panel::kAccOpacityEdit, 940 - bandXBase, bottom - 30, 48, 14);
    MoveCtrl(hwnd, panel::kRegisterAccessoryButton, 995 - bandXBase, bottom - 32, 58, 26);

    // --- option band 4: bandXBase += 178 with expand toggle optflag[3] (0x44422B);
    // band 4 controls use fixed X -------------------------------------------
    if (cameraMode != 0 && accessoryPanelExpanded == 0)
        bandXBase += 178;
    MoveCtrl(hwnd, panel::kExpandBonePanelButton, 363, bottom - 155, 14, 14);
    MoveCtrl(hwnd, panel::kCollapseBonePanelButton, 363, bottom - 155, 14, 14);
    MoveCtrl(hwnd, panel::kBoneSelectRadio, 366, bottom - 140, 60, 28);
    MoveCtrl(hwnd, panel::kBoneRotateRadio, 430, bottom - 140, 60, 28);
    MoveCtrl(hwnd, panel::kBoneMoveRadio, 494, bottom - 140, 60, 28);
    MoveCtrl(hwnd, panel::kBoxSelectRadio, 366, bottom - 105, 60, 24);
    MoveCtrl(hwnd, panel::kSelectAllBonesButton, 430, bottom - 105, 60, 24);
    MoveCtrl(hwnd, panel::kSelectUnregisteredBonesButton, 494, bottom - 105, 60, 24);
    MoveCtrl(hwnd, panel::kCopyBonePoseButton, 366, bottom - 70, 60, 24);
    MoveCtrl(hwnd, panel::kBonePasteButton, 430, bottom - 70, 60, 24);
    MoveCtrl(hwnd, panel::kBoneReversePasteButton, 494, bottom - 70, 60, 24);
    MoveCtrl(hwnd, panel::kPhysicsCheckbox, 500, bottom - 36, 48, 28);
    MoveCtrl(hwnd, panel::kResetBonePoseButton, 430, bottom - 32, 60, 24);
    MoveCtrl(hwnd, panel::kRegisterFrameButton, 366, bottom - 36, 60, 28);

    // --- option band 5: second base bandXBase2 (original v110;
    // 0x44444C..0x4448BE) ----------------------------------------------------
    // optflag[4]/optflag[0] both clear -> bandXBase += 179; then bandXBase2 = bandXBase - 180
    // (bone mode on) or bandXBase + 53; optflag[5]/optflag[0] both clear -> bandXBase2 +=
    // 251; optflag[0] clear -> bandXBase2 += 13.
    if (bonePanelExpanded == 0 && cameraMode == 0)
        bandXBase += 179;                                                         // 0x444453
    std::int32_t bandXBase2 = cameraMode != 0 ? bandXBase - 180 : bandXBase + 53; // 0x44445D
    MoveCtrl(hwnd, panel::kExpandMorphPanelButton, 598 - bandXBase2, bottom - 155, 14, 14);
    MoveCtrl(hwnd, panel::kCollapseMorphPanelButton, 598 - bandXBase2, bottom - 155, 14, 14);
    MoveCtrl(hwnd, panel::kMorphCombo1, 618 - bandXBase2, bottom - 120, 92, 500);
    MoveCtrl(hwnd, panel::kMorphPreviousButton1, 602 - bandXBase2, bottom - 121, 16, 22);
    MoveCtrl(hwnd, panel::kMorphNextButton1, 710 - bandXBase2, bottom - 121, 16, 22);
    MoveCtrl(hwnd, panel::kMorphSlider1, 597 - bandXBase2, bottom - 100, 135, 25);
    MoveCtrl(hwnd, panel::kMorphEdit1, 642 - bandXBase2, bottom - 138, 28, 14);
    MoveCtrl(hwnd, panel::kMorphCombo0, 618 - bandXBase2, bottom - 50, 92, 500);
    MoveCtrl(hwnd, panel::kMorphPreviousButton0, 602 - bandXBase2, bottom - 51, 16, 22);
    MoveCtrl(hwnd, panel::kMorphNextButton0, 710 - bandXBase2, bottom - 51, 16, 22);
    MoveCtrl(hwnd, panel::kMorphSlider0, 597 - bandXBase2, bottom - 30, 135, 25);
    MoveCtrl(hwnd, panel::kMorphEdit0, 642 - bandXBase2, bottom - 68, 28, 14);
    MoveCtrl(hwnd, panel::kMorphCombo2, 749 - bandXBase2, bottom - 120, 92, 500);
    MoveCtrl(hwnd, panel::kMorphPreviousButton2, 733 - bandXBase2, bottom - 121, 16, 22);
    MoveCtrl(hwnd, panel::kMorphNextButton2, 841 - bandXBase2, bottom - 121, 16, 22);
    MoveCtrl(hwnd, panel::kMorphSlider2, 729 - bandXBase2, bottom - 100, 135, 25);
    MoveCtrl(hwnd, panel::kMorphEdit2, 773 - bandXBase2, bottom - 138, 28, 14);
    MoveCtrl(hwnd, panel::kMorphCombo3, 749 - bandXBase2, bottom - 50, 92, 500);
    MoveCtrl(hwnd, panel::kMorphPreviousButton3, 733 - bandXBase2, bottom - 51, 16, 22);
    MoveCtrl(hwnd, panel::kMorphNextButton3, 841 - bandXBase2, bottom - 51, 16, 22);
    MoveCtrl(hwnd, panel::kMorphSlider3, 729 - bandXBase2, bottom - 30, 135, 25);
    MoveCtrl(hwnd, panel::kMorphEdit3, 773 - bandXBase2, bottom - 68, 28, 14);
    MoveCtrl(hwnd, panel::kRegisterMorphButton1, 675 - bandXBase2, bottom - 141, 50, 19);
    MoveCtrl(hwnd, panel::kRegisterMorphButton0, 675 - bandXBase2, bottom - 71, 50, 19);
    MoveCtrl(hwnd, panel::kRegisterMorphButton3, 807 - bandXBase2, bottom - 71, 50, 19);
    MoveCtrl(hwnd, panel::kRegisterMorphButton2, 807 - bandXBase2, bottom - 141, 50, 19);

    // The original updates bandXBase2 only after the complete facial group above;
    // the adjusted base belongs exclusively to the following view/play band.
    if (morphPanelExpanded == 0 && cameraMode == 0)
        bandXBase2 += 251; // 0x4448A8
    if (cameraMode == 0)
        bandXBase2 += 13; // 0x444895
    MoveCtrl(hwnd, panel::kViewFrontButton, 886 - bandXBase2, bottom - 141, 38, 17);
    MoveCtrl(hwnd, panel::kViewBackButton, 925 - bandXBase2, bottom - 141, 37, 17);
    MoveCtrl(hwnd, panel::kViewTopButton, 963 - bandXBase2, bottom - 141, 37, 17);
    MoveCtrl(hwnd, panel::kViewLeftButton, 886 - bandXBase2, bottom - 121, 38, 17);
    MoveCtrl(hwnd, panel::kViewRightButton, 925 - bandXBase2, bottom - 121, 37, 17);
    MoveCtrl(hwnd, panel::kViewBottomButton, 963 - bandXBase2, bottom - 121, 37, 17);
    MoveCtrl(hwnd, panel::kPlayButton, 887 - bandXBase2, bottom - 63, 50, 24);
    MoveCtrl(hwnd, panel::kPlayStartFrameEdit, 888 - bandXBase2, bottom - 36, 50, 14);
    MoveCtrl(hwnd, panel::kPlayStopFrameEdit, 952 - bandXBase2, bottom - 36, 50, 14);
    MoveCtrl(hwnd, panel::kPlaybackLoopCheckbox, 952 - bandXBase2, bottom - 58, 13, 13);
    MoveCtrl(hwnd, panel::kCameraRefModelCheckbox, 902 - bandXBase2, bottom - 99, 13, 13);
    MoveCtrl(hwnd, panel::kCameraRefBoneCheckbox, 971 - bandXBase2, bottom - 99, 13, 13);
    MoveCtrl(hwnd, panel::kPlaybackReturnCheckbox, 952 - bandXBase2, bottom - 19, 13, 13);
    MoveCtrl(hwnd, panel::kPlaybackFromCurrentCheckbox, 888 - bandXBase2, bottom - 19, 13, 13);
    MoveCtrl(hwnd, panel::kFrameVolumeSlider, 1017 - bandXBase2, bottom - 63, 22, 57);
    MoveCtrl(hwnd, panel::kFollowCameraCheckbox, 1002 - bandXBase2, bottom - 141, 35, 37);

    // --- visibility re-toggle, five id ranges (0x444B3B..0x444C99) ---------
    ToggleVisible(hwnd, panel::kLightColorSliderR, panel::kCollapseLightPanelButton);
    ToggleVisible(hwnd, panel::kAccessoryCombo, panel::kCollapseAccessoryPanelButton);
    ToggleVisible(hwnd, panel::kMorphCombo0, panel::kCollapseMorphPanelButton);
    ToggleVisible(hwnd, panel::kViewFrontButton, panel::kPlaybackFromCurrentCheckbox);
    ToggleVisible(hwnd, panel::kCameraRefBoneCheckbox, panel::kFollowCameraCheckbox);

    // --- refresh tail (0x444C9D..0x444CB0) ---------------------------------
    RefreshMainWindowViewport(app);
    LayoutViewportPanels(app); // original tail call: `return sub_40CAC0(this);`
}

// VA 0x00442EB0 - live relayout of the sidebar-anchored
// controls (top tool row 400..417/532/533/558/559 plus the bottom frame
// grid 415..434) at the current sidebar width and client height; the
// visibility re-toggle refreshes those bands without the full WM_SIZE
// option-band pass.  Called by the sidebar-resize drag (HandleMouseMove
// 0x444CC0), the scene loaders and the mic window procs.
void RelayoutSidebarControls(MMDApp* app) {
    const HWND hwnd = static_cast<HWND>(app->Hwnd());
    const int side = app->SidebarWidth();
    RECT client{};
    GetClientRect(hwnd, &client);

    MoveCtrl(hwnd, panel::kUndoButton, side / 2 - 80, 21, 70, 24);
    MoveCtrl(hwnd, panel::kRedoButton, side / 2 + 10, 21, 70, 24);
    MoveCtrl(hwnd, panel::kPreviousFrameButton, side / 2 - 49, 67, 20, 24);
    MoveCtrl(hwnd, panel::kNextFrameButton, side / 2 + 29, 67, 20, 24);
    MoveCtrl(hwnd, panel::kPreviousKeyframeButton, side / 2 - 79, 69, 20, 22);
    MoveCtrl(hwnd, panel::kNextKeyframeButton, side / 2 + 59, 69, 20, 22);
    MoveCtrl(hwnd, panel::kCurrentFrameEdit, side / 2 - 27, 66, 54, 24);
    MoveCtrl(hwnd, panel::kLastFrameButton, side - 30, 69, 20, 22);
    MoveCtrl(hwnd, panel::kFirstFrameButton, 15, 69, 20, 22);

    const int bottom = client.bottom;
    MoveCtrl(hwnd, panel::kTimelineVScroll, side - 19, 162, 16, bottom - 410);
    MoveCtrl(hwnd, panel::kTimelineHScroll, 97, bottom - 247, side - 116, 16);
    MoveCtrl(hwnd, panel::kShowCurrentFrameButton, 8, bottom - 247, 80, 18);
    MoveCtrl(hwnd, panel::kSelectColumnButton, side / 2 + 15, bottom - 224, 50, 18);
    MoveCtrl(hwnd, panel::kDeleteFramesButton, side / 2 + 70, bottom - 224, 50, 18);
    MoveCtrl(hwnd, panel::kCopyFramesButton, side / 2 - 119, bottom - 224, 40, 18);
    MoveCtrl(hwnd, panel::kPasteButton, side / 2 - 76, bottom - 224, 40, 18);
    MoveCtrl(hwnd, panel::kReversePasteButton, side / 2 - 33, bottom - 224, 40, 18);
    MoveCtrl(hwnd, panel::kRangeStartEdit, side / 2 - 3, bottom - 201, 50, 14);
    MoveCtrl(hwnd, panel::kRangeStopEdit, side / 2 + 69, bottom - 201, 50, 14);
    MoveCtrl(hwnd, panel::kRegisterScopeCombo, side / 2 - 118, bottom - 193, 100, 400);
    MoveCtrl(hwnd, panel::kSelectRangeButton, side / 2 - 4, bottom - 181, 60, 18);
    MoveCtrl(hwnd, panel::kExpandShrinkButton, side / 2 + 60, bottom - 181, 60, 18);

    ToggleVisible(hwnd, panel::kUndoButton, panel::kRedoButton);
    ToggleVisible(hwnd, panel::kSelectRangeButton, panel::kRangeStopEdit);
    ToggleVisible(hwnd, panel::kFirstFrameButton, panel::kLastFrameButton);
    ToggleVisible(hwnd, panel::kRegisterScopeCombo, panel::kRegisterScopeCombo);
    RefreshMainWindowViewport(app);
    LayoutViewportPanels(app);
}

} // namespace mikudancestudio
