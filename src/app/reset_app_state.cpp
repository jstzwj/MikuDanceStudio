// ===========================================================================
// VA 0x0044E540 - ResetAppState  (original: sub_44E540, 0x2AA0 bytes)
// ===========================================================================
// "New scene" (menu 0xCD): restores every default cluster, tears down the
// models/accessories/AVI handles, rebuilds the four global keyframe track
// arrays (camera 0x374 / light 0x378 / shadow 0x37C / physics 0x380) and
// the 255 accessory key-track blocks, resets the D3D light (D3DLIGHT9 at
// 0x9E180) and the menu/combo/edit state, then runs the standard refresh
// tail.  Field order verbatim from the decompilation; offsets decimal.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>
#include <commctrl.h> // TBM_SETPOS
#include <d3d9.h>
#include <vfw.h>

#include <btBulletDynamicsCommon.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/scene_ownership.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
void ReloadModels(MMDApp* app); // VA 0x0042E640 (timeline_advance.cpp)
} // namespace mikudancestudio

namespace mikudancestudio {

// JP wide literals (byte-verified against the original .rdata):
// 0x52B784 カメラ / 0x52D390 照明 / 0x52D398 セルフ影 / 0x52D3A4 重力
static const wchar_t kJpCamera[] = L"\x30ab\x30e1\x30e9";
static const wchar_t kJpLight[] = L"\x7167\x660e";
static const wchar_t kJpSelfShadow[] = L"\x30bb\x30eb\x30d5\x5f71";
static const wchar_t kJpGravity[] = L"\x91cd\x529b";
// 0x52D370 ｶﾒﾗ／照明＋ｱｸｾｻﾘ
static const wchar_t kJpCamLightAcc[] = L"\xff76\xff92\xff97\xff65\x7167\x660e"
                                        L"\xff65\xff71\xff78\xff7e\xff7b\xff98";
// 0x52D368 地面 / 0x52D360 なし
static const wchar_t kJpGround[] = L"\x5730\x9762";
static const wchar_t kJpNone[] = L"\x306a\x3057";
// combo 433 channel names (0x52D3D4..0x52D3AC)
static const wchar_t kJpMoveX[] = L"\xff38\x79fb\x52d5";
static const wchar_t kJpMoveY[] = L"\xff39\x79fb\x52d5";
static const wchar_t kJpMoveZ[] = L"\xff3a\x79fb\x52d5";
static const wchar_t kJpRotation[] = L"\x56de\x3000\x8ee2";
static const wchar_t kJpDistance[] = L"\x8ddd\x3000\x96e2";
static const wchar_t kJpViewAngle[] = L"\x8996\x91ce\x89d2";
static const wchar_t kJpAll[] = L"\x3059\x3079\x3066";

// Replace the edit text like the original: select all, then REPLACESEL.
static void ReplaceEditText(HWND parent, int id, const char* text) {
    HWND edit = GetDlgItem(parent, id);
    SendMessageA(edit, EM_SETSEL, 0,
                 GetWindowTextLengthA(edit)); // 0xB1
    SendMessageA(edit, EM_REPLACESEL, 0,
                 reinterpret_cast<LPARAM>(text)); // 0xC2
}

void ResetAppState(MMDApp* app) {
    HWND hwnd = app->MainWindow();
    app->SceneModified() = 0;

    // ---- menu checks + physics defaults -----------------------------------
    CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_ON_OFF, 8u); // 0x44E583
    CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_ALWAYS, 0u);
    CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_TRACE, 0u);
    CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_DISABLED, 0u);
    app->GravityMagnitude() = 9.8000002f;
    PhysicsScene* scene = app->Physics(); // +650672 (kPtrSub048)
    app->PlaybackPhysicsMode() = 2;
    app->state.gravityNoiseTimer = 0.0f;
    app->GravityNoiseEnabled() = 0;
    app->GravityDirection()[0] = 0.0f;
    app->GravityNoise() = 10;
    app->state.rigidBodyDisplayEnabled = 0;
    app->GravityDirection()[1] = -1.0f;
    app->GravityDirection()[2] = 0.0f;
    {
        const float gravity[4] = {0.0f, -98.0f, 0.0f, 0.0f}; // 0x44E65F
        auto* world = scene->world;                          // slot 64 / 0x40
        world->setGravity(*reinterpret_cast<const btVector3*>(gravity));
    }
    app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Camera);
    app->CameraParentModel() = -1;
    app->CameraParentBone() = 0;
    for (float& v : app->state.cameraAttachmentBasis)
        v = 0.0f;
    app->state.cameraAttachmentBasis[15] = 1.0f;
    app->state.cameraAttachmentBasis[10] = 1.0f;
    app->state.cameraAttachmentBasis[5] = 1.0f;
    app->state.cameraAttachmentBasis[0] = 1.0f;
    app->ClearPanelRowFlags();

    // ---- sub-window teardown ----------------------------------------------
    if (app->GravitySettingDialog() != nullptr)
        DestroyWindow(app->GravitySettingDialog()); // 0x44E719
    HWND w292 = app->FrameCopyOrPhysicsDialog();
    app->GravitySettingDialog() = nullptr;
    if (w292 != nullptr)
        DestroyWindow(w292);
    app->FrameCopyOrPhysicsDialog() = nullptr;
    HWND w256 = app->EnglishNameDialog();
    if (w256 != nullptr)
        DestroyWindow(w256);
    app->EnglishNameDialog() = nullptr;
    HWND w244 = app->EdgeThicknessDialog();
    if (w244 != nullptr)
        DestroyWindow(w244);
    app->EdgeThicknessDialog() = nullptr;
    app->EnhancedModelDirty() = 0;
    app->state.mainModelComboSelection = 0;
    HWND parent = app->FloatingWindow();
    if (parent == nullptr)
        parent = hwnd;
    SetWindowTextA(GetDlgItem(parent, panel::kGotoFrameEdit), "0"); // 0x44E78A

    // ---- AVI background teardown ------------------------------------------
    PGETFRAME getFrame = static_cast<PGETFRAME>(app->AviFrameReader());
    app->ShadowDistance() = 0.01125f;
    app->ShadowMode() = 1;
    app->state.modelOutlineColorRed = 0;
    app->state.modelOutlineColorGreen = 0;
    app->state.modelOutlineColorBlue = 0;
    if (getFrame != nullptr) {
        AVIStreamGetFrameClose(getFrame); // 0x44E7C3
        app->AviFrameReader() = nullptr;
    }
    if (app->AviStream() != nullptr) {
        AVIStreamRelease(static_cast<PAVISTREAM>(app->AviStream()));
        app->AviStream() = nullptr;
    }
    if (app->AviFile() != nullptr) {
        AVIFileRelease(static_cast<PAVIFILE>(app->AviFile()));
        app->AviFile() = nullptr;
    }
    app->state.aviBackgroundEnabled = 0;
    swprintf_s(app->state.aviBackgroundPath, 0x100, L"");
    if (app->AviBackgroundTexture() != nullptr) {
        app->AviBackgroundTexture()->Release();
        app->AviBackgroundTexture() = nullptr;
    }

    if (app->CameraMode() == 0) {
        SendMessageA(GetDlgItem(hwnd, panel::kIkChainCombo), CB_RESETCONTENT, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_RESETCONTENT, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kModelVisibleCheckbox), BM_CLICK, 0, 0);
        RebuildCameraModePanel(app); // 0x44E88F
        app->CameraMode() = 1;
        PostModelReload2(app); // 0x40D940
        HandleWindowSize(app); // 0x443300
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    // ---- camera/accessory edit state --------------------------------------
    app->ViewOffsetX() = 0.0f;
    app->ViewOffsetY() = 0.0f;
    app->state.accessoryRenderSplitOrder = 1;
    app->CurrentFrame() = 0;
    app->CameraDistance() = -45.0f;
    app->ViewToolDragOperation() = ViewportToolAction::None;
    app->ViewportToolOperation() = ViewportToolAction::None;
    app->InteractionDragMode() = ViewportDragMode::None;
    app->CameraRotation()[0] = 0.0f;
    app->BoneBoxSelectionActive() = 0;
    app->CameraRotation()[1] = 0.0f;
    app->ReleaseClipboardBuffers();
    app->CameraRotation()[2] = 0.0f;
    app->state.timelineStartFrame = 0;
    app->LastRegisteredFrame() = 0;
    app->CaptureMode() = ScreenCaptureMode::Disabled;
    app->state.projectedShadowBlendEnabled = 1;

    // ---- models + global/accessory track arrays ---------------------------
    ReleaseSceneModels(*app);          // 0x44E95A
    ReleaseGlobalTimelineTracks(*app); // 0x44E99C
    ReleaseAccessoriesAndTracks(*app); // 0x44EA10

    // ---- combo refills ------------------------------------------------------
    SendMessageA(GetDlgItem(hwnd, panel::kIkChainCombo), CB_RESETCONTENT, 0, 0);
    SendMessageA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_RESETCONTENT, 0, 0);
    if (app->state.englishUI != 0) {
        SendMessageA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("camera"));
        SendMessageA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("light"));
        SendMessageA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("s shadow"));
        SendMessageA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("gravity"));
    } else {
        SendMessageW(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kJpCamera));
        SendMessageW(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kJpLight));
        SendMessageW(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kJpSelfShadow));
        SendMessageW(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kJpGravity));
    }
    SendMessageA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_SETCURSEL, 0, 0);
    SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_RESETCONTENT, 0, 0);
    if (app->state.englishUI != 0)
        SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("camera/light/accessory"));
    else
        SendMessageW(GetDlgItem(hwnd, panel::kMainComboModel), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kJpCamLightAcc));
    SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_SETCURSEL, 0, 0);
    SendMessageA(GetDlgItem(hwnd, panel::kMainComboGround), CB_RESETCONTENT, 0, 0);
    SendMessageA(GetDlgItem(hwnd, panel::kMainComboNormal), CB_RESETCONTENT, 0, 0);
    if (app->state.englishUI != 0) {
        SendMessageA(GetDlgItem(hwnd, panel::kMainComboGround), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("ground"));
        SendMessageA(GetDlgItem(hwnd, panel::kMainComboNormal), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("non"));
    } else {
        SendMessageW(GetDlgItem(hwnd, panel::kMainComboGround), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kJpGround));
        SendMessageW(GetDlgItem(hwnd, panel::kMainComboNormal), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kJpNone));
    }
    static const int kResetCombos[] = {panel::kAttachBoneCombo, panel::kBoneRegisterCombo,
                                       panel::kAccessoryCombo,  panel::kMorphCombo0,
                                       panel::kMorphCombo1,     panel::kMorphCombo2,
                                       panel::kMorphCombo3,     panel::kInterpCurveCombo};
    for (int id : kResetCombos)
        SendMessageA(GetDlgItem(hwnd, id), CB_RESETCONTENT, 0, 0);
    if (app->state.englishUI != 0) {
        static const char* kChannels[] = {"x axis move", "y axis move", "z axis move", "rotation",
                                          "distance",    "view angle",  "all"};
        for (const char* name : kChannels)
            SendMessageA(GetDlgItem(hwnd, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(name));
    } else {
        static const wchar_t* kChannels[] = {kJpMoveX,    kJpMoveY,     kJpMoveZ, kJpRotation,
                                             kJpDistance, kJpViewAngle, kJpAll};
        for (const wchar_t* name : kChannels)
            SendMessageW(GetDlgItem(hwnd, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(name));
    }
    SendMessageA(GetDlgItem(hwnd, panel::kInterpCurveCombo), CB_SETCURSEL, 3, 0);
    SendMessageA(GetDlgItem(hwnd, panel::kShadowCheckbox), BM_CLICK, 0, 0);
    SendMessageA(GetDlgItem(hwnd, panel::kAddBlendCheckbox), BM_CLICK, 0, 0);
    SendMessageA(GetDlgItem(hwnd, panel::kAccessoryAddBlendCheckbox), BM_CLICK, 0, 0);
    ReplaceEditText(hwnd, panel::kCurrentFrameEdit, "0"); // 0x44F01D
    static const int kDisable[] = {panel::kCurveCopyButton,
                                   panel::kUndoButton,
                                   panel::kRedoButton,
                                   panel::kBonePasteButton,
                                   panel::kBoneReversePasteButton,
                                   panel::kPasteButton,
                                   panel::kReversePasteButton};
    for (int id : kDisable)
        EnableWindow(GetDlgItem(hwnd, id), FALSE);
    {
        char locale[0x100];
        sprintf_s(locale, 0x100, "%%"); // 0x529679
        static const int kPercentEdits[] = {
            panel::kRangeStartEdit,    panel::kRangeStopEdit, panel::kPlayStartFrameEdit,
            panel::kPlayStopFrameEdit, panel::kAccPosXEdit,   panel::kAccPosYEdit,
            panel::kAccPosZEdit,       panel::kAccRotXEdit,   panel::kAccRotYEdit,
            panel::kAccRotZEdit,       panel::kAccScaleEdit};
        for (int id : kPercentEdits)
            ReplaceEditText(hwnd, id, locale);
    }
    ClearTimelineAndCurveDCs(app); // 0x44F29F

    // ---- menu state ---------------------------------------------------------
    static const std::uint32_t kChecks[] = {MDS_CMD_TOGGLE_COORDINATE_AXIS | (8u << 12),
                                            MDS_CMD_TOGGLE_GROUND_SHADOW | (8u << 12),
                                            MDS_CMD_FPS_60 | (8u << 12),
                                            MDS_CMD_FPS_30,
                                            MDS_CMD_FPS_UNLIMITED,
                                            MDS_CMD_SCREEN_CAPTURE_OFF | (8u << 12),
                                            MDS_CMD_SCREEN_CAPTURE_MODE_1,
                                            MDS_CMD_SCREEN_CAPTURE_MODE_2,
                                            MDS_CMD_SCREEN_CAPTURE_MODE_3,
                                            MDS_CMD_TOGGLE_GROUND_SHADOW_TRANSPARENCY | (8u << 12)};
    for (std::uint32_t entry : kChecks)
        CheckMenuItem(GetMenu(hwnd), entry & 0xFFFu, entry >> 12);
    app->EditMode() = ViewportEditMode::None;
    static const int kClicks[] = {panel::kBoneSelectRadio, panel::kBoxSelectRadio,
                                  panel::kBoneMoveRadio, panel::kBoneRotateRadio};
    for (int id : kClicks)
        SendMessageA(GetDlgItem(hwnd, id), BM_CLICK, 0, 0);
    static const std::uint32_t kGray[] = {
        MDS_CMD_ACCESSORY_SETTINGS, MDS_CMD_PASTE_AT_DIFFERENT_FRAME, MDS_CMD_MODEL_DRAW_ORDER,
        MDS_CMD_MODEL_CALCULATION_ORDER};
    for (std::uint32_t id : kGray)
        EnableMenuItem(GetMenu(hwnd), id, 1u); // MF_GRAYED

    // ---- second AVI sweep + light/shadow menu defaults --------------------
    getFrame = static_cast<PGETFRAME>(app->AviFrameReader());
    app->state.waveEnabled = 0;
    if (getFrame != nullptr) {
        AVIStreamGetFrameClose(getFrame); // 0x44F4A2
        app->AviFrameReader() = nullptr;
    }
    if (app->AviStream() != nullptr) {
        AVIStreamRelease(static_cast<PAVISTREAM>(app->AviStream()));
        app->AviStream() = nullptr;
    }
    if (app->AviFile() != nullptr) {
        AVIFileRelease(static_cast<PAVIFILE>(app->AviFile()));
        app->AviFile() = nullptr;
    }
    app->state.fpsOverlayEnabled = 0;
    CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_INFORMATION, 0u);
    SendMessageA(GetDlgItem(app->FloatingWindow() != nullptr ? app->FloatingWindow() : hwnd,
                            panel::kInfoCheckbox),
                 BM_CLICK, 0, 0);
    app->state.groundGridEnabled = 1;
    CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_COORDINATE_AXIS, 8u);
    SendMessageA(GetDlgItem(app->FloatingWindow() != nullptr ? app->FloatingWindow() : hwnd,
                            panel::kCoordAxisCheckbox),
                 BM_CLICK, 1, 0);
    app->state.groundShadowEnabled = 1;
    CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_GROUND_SHADOW, 8u);

    // ---- rebuild the global track arrays ----------------------------------
    auto* camArr = static_cast<mdl::CameraKey*>(operator new(sizeof(mdl::CameraKey) *
                                                             mdl::kTimelineKeyCapacity));
    app->CameraKeys() = camArr;
    std::memset(camArr, 0, sizeof(mdl::CameraKey) * mdl::kTimelineKeyCapacity);
    auto* lightArr = static_cast<mdl::LightKey*>(operator new(sizeof(mdl::LightKey) *
                                                              mdl::kTimelineKeyCapacity));
    app->LightKeys() = lightArr;
    std::memset(lightArr, 0, sizeof(mdl::LightKey) * mdl::kTimelineKeyCapacity);
    auto* shadowArr = static_cast<mdl::SelfShadowKey*>(operator new(sizeof(mdl::SelfShadowKey) *
                                                                    mdl::kTimelineKeyCapacity));
    app->ShadowKeys() = shadowArr;
    std::memset(shadowArr, 0, sizeof(mdl::SelfShadowKey) * mdl::kTimelineKeyCapacity);
    auto* physArr = static_cast<mdl::GravityKey*>(operator new(sizeof(mdl::GravityKey) *
                                                               mdl::kTimelineKeyCapacity));
    app->GravityKeys() = physArr;
    std::memset(physArr, 0, sizeof(mdl::GravityKey) * mdl::kTimelineKeyCapacity);
    for (int i = 0; i < 255; ++i) { // 0x44F627
        auto* track = static_cast<mdl::AccessoryKey*>(operator new(sizeof(mdl::AccessoryKey) *
                                                                   mdl::kTimelineKeyCapacity));
        app->AccessoryKeys(i) = track;
        std::memset(track, 0, sizeof(mdl::AccessoryKey) * mdl::kTimelineKeyCapacity);
        track[0].visible = 1;
        track[0].parentModel = -1;
        track[0].opacity = 1.0f;
        track[0].scale = 1.0f;
        app->AccessorySlot(i) = nullptr;
    }
    camArr[0].distance = -45.0f;
    camArr[0].eye[1] = 10.0f;
    camArr[0].perspective = 0;
    camArr[0].fov = 30;
    camArr[0].parentModel = -1;
    camArr[0].parentBone = 0;
    for (std::size_t rec = 0; rec < mdl::kTimelineKeyCapacity; ++rec) { // 0x44F6D0
        camArr[rec].parentModel = -1;
        for (int c = 0; c < 6; ++c) {
            camArr[rec].interpolation[0][c] = 20;
            camArr[rec].interpolation[1][c] = 20;
            camArr[rec].interpolation[2][c] = 107;
            camArr[rec].interpolation[3][c] = 107;
        }
    }

    // ---- D3DLIGHT9 default (cluster at 0x9E180) ---------------------------
    app->CameraFov() = 30.0f;
    app->ResetSceneLight();
    D3DLIGHT9& light = app->SceneLight();
    {
        D3DRenderer* r = app->Renderer();
        IDirect3DDevice9* device = r->device; // +120032
        device->SetLight(0, &light);          // 0x44F804
        device->LightEnable(0, TRUE);         // 0x44F81F
    }
    lightArr[0].color[0] = 0.602f;
    lightArr[0].color[1] = 0.602f;
    lightArr[0].color[2] = 0.602f;
    lightArr[0].direction[0] = -0.5f;
    lightArr[0].direction[1] = -1.0f;
    lightArr[0].direction[2] = 0.5f;
    shadowArr[0].mode =
        static_cast<std::uint8_t>(app->Renderer()->postProcessEnabled != 0); // wrapper+120132
    shadowArr[0].distance = 0.01125f;
    physArr[0].noiseEnabled = 0;
    physArr[0].noise = 10;
    physArr[0].acceleration = 9.8000002f;
    physArr[0].direction[1] = -1.0f;
    DrawMenuBar(hwnd);

    // ---- frame/title state --------------------------------------------------
    app->state.aviBackgroundEnabled = 0;
    app->state.pictureBackgroundEnabled = 0;
    app->state.characterTransparentMode = 0;
    CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_MODEL_TRANSPARENCY, 0u);
    CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_BACKGROUND_VIDEO, 0u);
    CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_BACKGROUND_IMAGE, 0u);
    {
        char title[0x100];
        sprintf_s(title, 0x100, "MikuDanceStudio");
        SetWindowTextA(hwnd, title);
    }
    app->state.envFileName[0] = 0;
    app->PanelExpanded(UiPanel::Camera) = 1;
    app->PanelExpanded(UiPanel::Light) = 1;
    app->PanelExpanded(UiPanel::Accessory) = 1;
    app->PanelExpanded(UiPanel::Bone) = 1;
    app->PanelExpanded(UiPanel::Morph) = 1;
    app->PanelExpanded(UiPanel::SelfShadow) = 1;
    app->CameraPerspective() = 0;
    SendMessageA(GetDlgItem(hwnd, panel::kPerspectiveCheckbox), BM_CLICK, 1, 0);
    app->CameraPosition()[0] = 0.0f;
    app->CameraPosition()[1] = 10.0f;
    app->CameraPosition()[2] = 0.0f;
    app->state.v32c = 0;
    app->CameraReferenceMode() = CameraAttachmentReference::None;
    SendMessageA(GetDlgItem(hwnd, panel::kCameraRefModelCheckbox), BM_CLICK, 0, 0);
    SendMessageA(GetDlgItem(hwnd, panel::kCameraRefBoneCheckbox), BM_CLICK, 0, 0);
    app->PlaybackLoopEnabled() = 0;
    SendMessageA(GetDlgItem(hwnd, panel::kPlaybackLoopCheckbox), BM_CLICK, 0, 0);
    app->state.playbackReturnsToStartFrame = 0;
    SendMessageA(GetDlgItem(hwnd, panel::kPlaybackReturnCheckbox), BM_CLICK, 0, 0);
    app->state.viewportToolHovered = 0;
    app->CameraFov() = 45.0f;
    SendMessageA(GetDlgItem(hwnd, panel::kAccessoryShadowCheckbox), BM_CLICK, 0, 0);
    SendMessageA(GetDlgItem(hwnd, panel::kAccessoryVisibleCheckbox), BM_CLICK, 0, 0);
    app->FpsLimit() = 60.0f;

    // ---- light-accessory edit echoes ---------------------------------------
    char text[0x100];
    sprintf_s(text, 0x100, "%3d", static_cast<int>(app->LightColor()[0] * 256.0));
    SendMessageA(GetDlgItem(hwnd, panel::kLightColorEditR), WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(text));
    sprintf_s(text, 0x100, "%3d", static_cast<int>(app->LightColor()[1] * 256.0));
    SendMessageA(GetDlgItem(hwnd, panel::kLightColorEditG), WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(text));
    sprintf_s(text, 0x100, "%3d", static_cast<int>(app->LightColor()[2] * 256.0));
    SendMessageA(GetDlgItem(hwnd, panel::kLightColorEditB), WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(text));
    sprintf_s(text, 0x100, "%+3.1f", app->LightDirection()[0]);
    SendMessageA(GetDlgItem(hwnd, panel::kLightDirEditX), WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(text));
    sprintf_s(text, 0x100, "%+3.1f", app->LightDirection()[1]);
    SendMessageA(GetDlgItem(hwnd, panel::kLightDirEditY), WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(text));
    sprintf_s(text, 0x100, "%+3.1f", app->LightDirection()[2]);
    SendMessageA(GetDlgItem(hwnd, panel::kLightDirEditZ), WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(text));
    sprintf_s(text, 0x100, "%3d", static_cast<int>(app->CameraFov()));
    SendMessageA(GetDlgItem(hwnd, panel::kFovEdit), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text));
    SendMessageA(GetDlgItem(hwnd, panel::kFovSlider), TBM_SETPOS, 1,
                 static_cast<LPARAM>(static_cast<int>(app->CameraFov())));
    SendMessageA(GetDlgItem(hwnd, panel::kLightColorSliderR), TBM_SETPOS, 1,
                 static_cast<LPARAM>(static_cast<int>(app->LightColor()[0] * 256.0)));
    SendMessageA(GetDlgItem(hwnd, panel::kLightColorSliderG), TBM_SETPOS, 1,
                 static_cast<LPARAM>(static_cast<int>(app->LightColor()[1] * 256.0)));
    SendMessageA(GetDlgItem(hwnd, panel::kLightColorSliderB), TBM_SETPOS, 1,
                 static_cast<LPARAM>(static_cast<int>(app->LightColor()[2] * 256.0)));
    SendMessageA(GetDlgItem(hwnd, panel::kLightDirSliderX), TBM_SETPOS, 1,
                 static_cast<LPARAM>(static_cast<int>(app->LightDirection()[0] * 100.0)));
    SendMessageA(GetDlgItem(hwnd, panel::kLightDirSliderY), TBM_SETPOS, 1,
                 static_cast<LPARAM>(static_cast<int>(app->LightDirection()[1] * 100.0)));
    SendMessageA(GetDlgItem(hwnd, panel::kLightDirSliderZ), TBM_SETPOS, 1,
                 static_cast<LPARAM>(static_cast<int>(app->LightDirection()[2] * 100.0)));

    // ---- menu enable state --------------------------------------------------
    static const std::uint32_t kEnable[] = {
        MDS_CMD_SELECT_ALL_CAMERA_FRAMES,      MDS_CMD_SELECT_ALL_LIGHT_FRAMES,
        MDS_CMD_SELECT_ALL_SELF_SHADOW_FRAMES, MDS_CMD_SELECT_ALL_GRAVITY_FRAMES,
        MDS_CMD_SELECT_ALL_ACCESSORY_FRAMES,   MDS_CMD_MULTIPLY_CAMERA_FRAMES};
    for (std::uint32_t id : kEnable)
        EnableMenuItem(GetMenu(hwnd), id, 0u);
    static const std::uint32_t kGray2[] = {MDS_CMD_EDGE_THICKNESS,
                                           MDS_CMD_SELECT_ALL_BONE_FRAMES,
                                           MDS_CMD_SELECT_ALL_MORPH_FRAMES,
                                           MDS_CMD_SELECT_ALL_DISPLAY_FRAMES,
                                           MDS_CMD_LOAD_POSE,
                                           MDS_CMD_SAVE_POSE,
                                           MDS_CMD_CENTER_POSITION_BIAS,
                                           MDS_CMD_DELETE_UNUSED_FRAMES,
                                           MDS_CMD_MULTIPLY_BONE_FRAMES,
                                           MDS_CMD_MULTIPLY_MORPH_FRAMES};
    for (std::uint32_t id : kGray2)
        EnableMenuItem(GetMenu(hwnd), id, 1u);
    for (std::uint32_t id = MDS_CMD_LOAD_LIP_SYNC; id <= MDS_CMD_DELETE_EYEBROW_FRAMES; ++id)
        EnableMenuItem(GetMenu(hwnd), id, 1u);
    for (std::uint32_t id = MDS_CMD_SELECT_PHYSICS_BONES; id <= MDS_CMD_TOGGLE_FRAME_PHYSICS; ++id)
        EnableMenuItem(GetMenu(hwnd), id, 1u);
    {
        MENUITEMINFOA mii; // 0x44FF64
        std::memset(&mii, 0, sizeof mii);
        mii.cbSize = 48;
        mii.fMask = MIIM_STATE;
        mii.fState = MFS_DISABLED | MFS_GRAYED;
        SetMenuItemInfoA(GetSubMenu(GetMenu(hwnd), 7), 2u, TRUE,
                         &mii); // original passes 1024 (by position)
    }
    DrawMenuBar(hwnd);

    // ---- refresh tail -------------------------------------------------------
    RebuildTimelineTrackPanel(app); // 0x42F1E0
    PanelPaint(app);                // 0x414610
    SelectionReeval(app);           // 0x430510
    ReloadModels(app);              // 0x42E640
    RefreshLightPanel(app);         // 0x411070
    RefreshSelfShadowPanel(app);    // 0x411B90
    ApplyGravityTrack(app);         // 0x412330
    PostModelReload2(app);          // 0x40D940
    PostViewRefresh(app);           // 0x40D130
    InvalidateRect(hwnd, nullptr, FALSE);
}

} // namespace mikudancestudio
