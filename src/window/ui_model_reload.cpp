// ===========================================================================
// VA 0x0040D940 / 0x0044D610 - model/camera panel refresh helpers
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>
#include <commctrl.h>
#include <d3d9.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

void SelectionReeval(MMDApp* app);          // 0x430510
void ApplyGravityTrack(MMDApp* app);        // 0x412330
void ApplyAccessoryTrack(MMDApp* app, int); // 0x413120

namespace {

HWND MainControl(MMDApp* app, int id) {
    return GetDlgItem(static_cast<HWND>(app->Hwnd()), id);
}

void ShowControlRange(MMDApp* app, int first, int last, bool show) {
    for (int id = first; id <= last; ++id)
        ShowWindow(MainControl(app, id), show ? SW_SHOWNORMAL : SW_HIDE);
}

mdl::ModelRecord* ModelAt(MMDApp* app, int slot) {
    return app->ModelSlot(slot);
}

int FindModelSlotByComboId(MMDApp* app, int comboId) {
    // x64 twin sub_7FF7CB486B10: both comboSelIndex (model+0x3108/x86 11644)
    // scans run to 255 (cmp ecx, 0FFh at 0x7FF7CB48724D and 0x7FF7CB48735A).
    for (int slot = 0; slot < kModelSlotCount; ++slot) {
        mdl::ModelRecord* model = ModelAt(app, slot);
        if (model != nullptr && model->comboSelIndex == comboId)
            return slot;
    }
    return -1;
}

void EnableMenuCommands(HMENU menu, const int* ids, std::size_t count, UINT state) {
    for (std::size_t i = 0; i < count; ++i)
        EnableMenuItem(menu, ids[i], state);
}

void SetPhysicsMenuState(HWND hwnd, UINT state) {
    HMENU menu = GetMenu(hwnd);
    HMENU submenu = GetSubMenu(menu, 7);
    MENUITEMINFOA info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STATE;
    info.fState = state;
    SetMenuItemInfoA(submenu, 2, TRUE, &info);
    DrawMenuBar(hwnd);
}

mikudancestudio::mdl::BoneRecord* BoneAt(const mdl::ModelRecord* model, int boneIndex) {
    return model->boneTable + boneIndex;
}

void BonePoint(mikudancestudio::mdl::BoneRecord* bone, float out[3]) {
    const float* matrix = bone->matInit;
    const float* point = bone->position;
    out[0] = matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12];
    out[1] = matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13];
    out[2] = matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14];
}

} // namespace

void PostModelReload(MMDApp* app) { // 0x41A650
#ifdef MIKUDANCESTUDIO_DIAG
    if (getenv("MIKUDANCESTUDIO_TRACE_REC")) {
        FILE* tf = fopen(getenv("MIKUDANCESTUDIO_TRACE_REC"), "a");
        if (tf) {
            fprintf(tf, "PostModelReload targetSlot=%d\n", app->CameraParentModel());
            fclose(tf);
        }
    }
#endif
    auto& api = d3dx::Get();

    d3dx::D3DXMATRIXF rotationY{};
    d3dx::D3DXMATRIXF rotation{};
    d3dx::D3DXMATRIXF scratch{};
    api.rotY(&rotationY, app->CameraYaw());
    api.rotX(&rotation, app->CameraPitch());
    api.multiply(&rotation, &rotationY, &rotation);
    api.rotZ(&scratch, app->CameraRoll());
    api.multiply(&rotation, &rotation, &scratch);

    const int targetSlot = app->CameraParentModel();
    if (targetSlot >= 0) {
        mdl::ModelRecord* targetModel = ModelAt(app, targetSlot);
        mikudancestudio::mdl::BoneRecord* targetBone = BoneAt(targetModel, app->CameraParentBone());
        auto& targetBasis = reinterpret_cast<d3dx::D3DXMATRIXF&>(app->CameraAttachmentBasis());
        const float* boneMatrix = reinterpret_cast<const float*>(&targetBone->matInit[0]);
        targetBasis = {};
        targetBasis.m[0][0] = boneMatrix[0];
        targetBasis.m[0][1] = boneMatrix[4];
        targetBasis.m[0][2] = boneMatrix[8];
        targetBasis.m[1][0] = boneMatrix[1];
        targetBasis.m[1][1] = boneMatrix[5];
        targetBasis.m[1][2] = boneMatrix[9];
        targetBasis.m[2][0] = boneMatrix[2];
        targetBasis.m[2][1] = boneMatrix[6];
        targetBasis.m[2][2] = boneMatrix[10];
        targetBasis.m[3][3] = 1.0f;
        api.multiply(&rotation, &targetBasis, &rotation);

        float targetPoint[3]{};
        BonePoint(targetBone, targetPoint);
        d3dx::D3DXMATRIXF targetTranslation{};
        api.translation(&targetTranslation, targetPoint[0], targetPoint[1], targetPoint[2]);

        float referencePoint[3]{};
        const auto referenceMode = app->CameraReferenceMode();
        if (referenceMode == CameraAttachmentReference::ModelRoot) {
            mdl::ModelRecord* model = app->SelectedModel();
            mikudancestudio::mdl::BoneRecord* bone = BoneAt(model, model->centerBone);
            const float* matrix = reinterpret_cast<const float*>(bone->matInit);
            referencePoint[0] = matrix[12];
            referencePoint[1] = matrix[13];
            referencePoint[2] = matrix[14];
        } else if (referenceMode == CameraAttachmentReference::SelectedBone) {
            mdl::ModelRecord* model = app->SelectedModel();
            int selected = model->selectedBone;
            if (selected < 0)
                selected = 0;
            BonePoint(BoneAt(model, selected), referencePoint);
        }

        d3dx::D3DXMATRIXF referenceTranslation{};
        api.translation(&referenceTranslation, referencePoint[0], referencePoint[1],
                        referencePoint[2]);
        api.multiply(&targetTranslation, &targetTranslation, &rotation);
        api.multiply(&referenceTranslation, &referenceTranslation, &rotation);
        app->ViewOffsetX() =
            targetTranslation.m[3][0] - referenceTranslation.m[3][0] + app->CameraPositionX();
        app->ViewOffsetY() =
            targetTranslation.m[3][1] - referenceTranslation.m[3][1] + app->CameraPositionY();
        app->CameraDistance() =
            targetTranslation.m[3][2] - referenceTranslation.m[3][2] + app->CameraPositionZ();
        return;
    }

    d3dx::D3DXMATRIXF targetTranslation{};
    api.translation(&targetTranslation, -app->CameraPositionX(), -app->CameraPositionY(),
                    -app->CameraPositionZ());
    float referencePoint[3]{};
    const auto referenceMode = app->CameraReferenceMode();
    if (referenceMode == CameraAttachmentReference::ModelRoot) {
        mdl::ModelRecord* model = app->SelectedModel();
        mikudancestudio::mdl::BoneRecord* bone = BoneAt(model, model->centerBone);
        const float* matrix = reinterpret_cast<const float*>(bone->matInit);
        referencePoint[0] = -matrix[12];
        referencePoint[1] = -matrix[13];
        referencePoint[2] = -matrix[14];
    } else if (referenceMode == CameraAttachmentReference::SelectedBone) {
        mdl::ModelRecord* model = app->SelectedModel();
        const int selected = model->selectedBone;
        if (selected >= 0) {
            BonePoint(BoneAt(model, selected), referencePoint);
            referencePoint[0] = -referencePoint[0];
            referencePoint[1] = -referencePoint[1];
            referencePoint[2] = -referencePoint[2];
        }
    }

    d3dx::D3DXMATRIXF referenceTranslation{};
    api.translation(&referenceTranslation, referencePoint[0], referencePoint[1], referencePoint[2]);
    api.multiply(&targetTranslation, &targetTranslation, &rotation);
    api.multiply(&referenceTranslation, &referenceTranslation, &rotation);
    app->ViewOffsetX() -= targetTranslation.m[3][0] - referenceTranslation.m[3][0];
    app->ViewOffsetY() -= targetTranslation.m[3][1] - referenceTranslation.m[3][1];
    app->CameraDistance() -= targetTranslation.m[3][2] - referenceTranslation.m[3][2];
}

// (0x41A650 was a local alias wrapper of PostModelReload above - removed;
//  callers now use the PostModelReload name directly.)

void PostModelReload2(MMDApp* app) { // 0x40D940
    const bool cameraMode = app->CameraMode() != 0;

    const struct {
        int first;
        int last;
        int collapsed;
        UiPanel panel;
    } cameraGroups[] = {
        {panel::kPerspectiveCheckbox, panel::kCollapseCameraPanelButton,
         panel::kExpandCameraPanelButton, UiPanel::Camera},
        {panel::kLightColorSliderR, panel::kCollapseLightPanelButton,
         panel::kExpandLightPanelButton, UiPanel::Light},
        {panel::kAccessoryCombo, panel::kCollapseAccessoryPanelButton,
         panel::kExpandAccessoryPanelButton, UiPanel::Accessory},
        {panel::kSelfShadowRangeSlider, panel::kCollapseSelfShadowPanelButton,
         panel::kExpandSelfShadowPanelButton, UiPanel::SelfShadow},
    };
    for (const auto& group : cameraGroups) {
        const bool expanded = cameraMode && app->PanelExpanded(group.panel) != 0;
        ShowControlRange(app, group.first, group.last, expanded);
        ShowWindow(MainControl(app, group.collapsed),
                   cameraMode && !expanded ? SW_SHOWNORMAL : SW_HIDE);
    }

    const bool modelSelected =
        SendMessageA(MainControl(app, panel::kMainComboModel), CB_GETCURSEL, 0, 0) != 0;
    if (!modelSelected)
        SendMessageA(MainControl(app, panel::kIkChainCombo), CB_SETCURSEL, 0, 0);
    for (int id = panel::kDeleteModelButton; id <= panel::kIkOffRadio; ++id)
        EnableWindow(MainControl(app, id), modelSelected);

    const struct {
        int first;
        int last;
        int collapsed;
        UiPanel panel;
    } modelGroups[] = {
        {panel::kBoneSelectRadio, panel::kCollapseBonePanelButton, panel::kExpandBonePanelButton,
         UiPanel::Bone},
        {panel::kMorphCombo0, panel::kCollapseMorphPanelButton, panel::kExpandMorphPanelButton,
         UiPanel::Morph},
    };
    for (const auto& group : modelGroups) {
        const bool expanded = !cameraMode && app->PanelExpanded(group.panel) != 0;
        ShowControlRange(app, group.first, group.last, expanded);
        ShowWindow(MainControl(app, group.collapsed),
                   !cameraMode && !expanded ? SW_SHOWNORMAL : SW_HIDE);
    }

    // 0x40DC3A..0x40DC6D: until the bone-copy buffer contains records,
    // paste and reverse-paste remain disabled.  A successful copy command
    // enables both controls and stores the record count at 0x9DA24.
    if (!cameraMode && app->PanelExpanded(UiPanel::Morph) != 0 &&
        app->BonePoseClipboardCount() == 0) {
        EnableWindow(MainControl(app, panel::kBonePasteButton), FALSE);
        EnableWindow(MainControl(app, panel::kBoneReversePasteButton), FALSE);
    }

    HWND viewportWindow = app->state.floatingWindow;
    if (viewportWindow == nullptr)
        viewportWindow = static_cast<HWND>(app->Hwnd());

    if (cameraMode) {
        EnableWindow(MainControl(app, panel::kUndoButton), FALSE);
        EnableWindow(MainControl(app, panel::kRedoButton), FALSE);
        if (app->EnglishUI() != 0) {
            SetWindowTextA(MainControl(app, panel::kViewBottomButton), "btm");
            SetWindowTextA(GetDlgItem(viewportWindow, panel::kModelEditToggle), "To model");
        } else {
            SetWindowTextW(MainControl(app, panel::kViewBottomButton), L"\x4e0b\x9762");
            SetWindowTextW(GetDlgItem(viewportWindow, panel::kModelEditToggle),
                           L"\x30e2\x30c7\x30eb\x7de8");
        }
        ShowWindow(GetDlgItem(viewportWindow, panel::kCameraDistanceButton), SW_SHOW);
        ShowWindow(GetDlgItem(viewportWindow, panel::kReadoutDistEdit), SW_SHOW);
        return;
    }

    auto* model = app->SelectedModel();
    // undoDirty/redoDirty (x64 model+0x3558/+0x3559, 0x7FF7CB447074/86):
    // Flags enabling the undo/redo buttons.
    EnableWindow(MainControl(app, panel::kUndoButton), model->undoDirty != 0);
    EnableWindow(MainControl(app, panel::kRedoButton), model->redoDirty != 0);
    if (app->EnglishUI() != 0) {
        SetWindowTextA(MainControl(app, panel::kViewBottomButton), "camer");
        SetWindowTextA(GetDlgItem(viewportWindow, panel::kModelEditToggle), "To camera");
    } else {
        SetWindowTextW(MainControl(app, panel::kViewBottomButton), L"\x30ab\x30e1\x30e9");
        SetWindowTextW(GetDlgItem(viewportWindow, panel::kModelEditToggle),
                       L"\x30ab\x30e1\x30e9\x7de8");
    }
    ShowWindow(GetDlgItem(viewportWindow, panel::kCameraDistanceButton), SW_HIDE);
    ShowWindow(GetDlgItem(viewportWindow, panel::kReadoutDistEdit), SW_HIDE);
}

void RebuildModelModePanel(MMDApp* app) { // 0x44D610
    app->CameraMode() = 0;
    HWND combo = MainControl(app, panel::kInterpCurveCombo);
    SendMessageA(combo, CB_DELETESTRING, 5, 0);
    SendMessageA(combo, CB_DELETESTRING, 4, 0);
    SendMessageA(combo, CB_SETCURSEL, 3, 0);

    if (app->state.coordinateSystem == 2)
        app->state.coordinateSystem = 0;

    if (app->state.followCameraEnabled != 0) {
        app->ViewOffsetX() = 0.0f;
        app->ViewOffsetY() = 0.0f;
        ReloadModels(app);
        RefreshLightPanel(app);
        RefreshSelfShadowPanel(app);
        ApplyGravityTrack(app);
        for (int index = 0; index < 255; ++index) {
            if (app->ObjectSlot(index) != nullptr)
                ApplyAccessoryTrack(app, index);
        }
        SyncAccessoryEditPanel(app);
    }

    app->CameraAttachmentTransformSuppressed() = 0;
    PostModelReload(app);
    SelectionReeval(app);

    if (app->state.followCameraEnabled == 0) {
        D3DLIGHT9& light = app->SceneLight();
        D3DVECTOR& direction = *reinterpret_cast<D3DVECTOR*>(app->LightDirection());
        direction.x = -0.5f; // 0x44D711
        direction.y = -1.0f; // 0x44D71D
        direction.z = 0.5f;  // 0x44D726
        // 0x44D738..0x44D765: Specular.rgb = 0.602, Direction = the
        // 0x9E174 triple, Ambient.rgb copied from Specular, and Specular.a
        // (0x9E1A0) keeps the old Position.x dword read at 0x44D750 - the
        // struct copy `Specular = Ambient` used here before gave Specular.a
        // the old Ambient.a instead.
        const float oldPositionX = light.Position.x; // 0x44D750
        light.Specular.r = 0.602f;                   // 0x44D738
        light.Specular.g = 0.602f;                   // 0x44D73B
        light.Specular.b = 0.602f;                   // 0x44D741
        light.Direction = direction;                 // 0x44D73E..762
        light.Ambient.r = light.Specular.r;          // 0x44D74D
        light.Ambient.g = light.Specular.g;          // 0x44D759
        light.Ambient.b = light.Specular.b;          // 0x44D762
        light.Specular.a = oldPositionX;             // 0x44D765

        IDirect3DDevice9* device = app->Renderer()->device;
        device->SetLight(0, &light);
    }
}

void RebuildCameraModePanel(MMDApp* app) { // 0x44D780
    // Rebuild the camera-mode transform combo exactly as the original does.
    // Item four is the model-mode "all" entry; the three camera entries are
    // appended in its place.
    app->CameraMode() = 1;
    HWND combo = MainControl(app, panel::kInterpCurveCombo);
    SendMessageA(combo, CB_DELETESTRING, 4, 0);
    if (app->EnglishUI() != 0) {
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("distance"));
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("view angle"));
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("all"));
    } else {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\x8ddd\x96e2"));
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\x8996\x91ce\x89d2"));
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\x3059\x3079\x3066"));
    }

    app->ViewOffsetX() = 0.0f;
    app->ViewOffsetY() = 0.0f;
    SelectionReeval(app);
    ReloadModels(app);
    RefreshLightPanel(app);
    RefreshSelfShadowPanel(app);
    ApplyGravityTrack(app);
    for (int slot = 0; slot < 255; ++slot) {
        if (app->ObjectSlot(slot) != nullptr)
            ApplyAccessoryTrack(app, slot);
    }
    SyncAccessoryEditPanel(app);

    // 0xA0438 is the four-by-four selection/reference basis.  The original
    // writes all sixteen floats individually; the resulting value is identity.
    float* basis = &app->CameraAttachmentBasis().m[0][0];
    std::memset(basis, 0, 16 * sizeof(float));
    basis[0] = 1.0f;
    basis[5] = 1.0f;
    basis[10] = 1.0f;
    basis[15] = 1.0f;
}

void ApplyModelComboSelection(MMDApp* app) { // 0x44D940
    HWND hwnd = static_cast<HWND>(app->Hwnd());
    const int selection = static_cast<int>(
        SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_GETCURSEL, 0, 0));
    static constexpr int kModeCommands[] = {
        MDS_CMD_EDGE_THICKNESS,
        MDS_CMD_SELECT_ALL_BONE_FRAMES,
        MDS_CMD_SELECT_ALL_MORPH_FRAMES,
        MDS_CMD_SELECT_ALL_DISPLAY_FRAMES,
        MDS_CMD_LOAD_POSE,
        MDS_CMD_SAVE_POSE,
        MDS_CMD_CENTER_POSITION_BIAS,
        MDS_CMD_DELETE_UNUSED_FRAMES,
        MDS_CMD_MULTIPLY_BONE_FRAMES,
        MDS_CMD_MULTIPLY_MORPH_FRAMES,
    };

    if (selection == 0) {
        if (app->CameraMode() == 0) {
            SendMessageA(GetDlgItem(hwnd, panel::kIkChainCombo), CB_RESETCONTENT, 0, 0);
            HWND frameCombo = GetDlgItem(hwnd, panel::kRegisterScopeCombo);
            SendMessageA(frameCombo, CB_RESETCONTENT, 0, 0);
            if (app->EnglishUI() != 0) {
                static constexpr const char* kNames[] = {
                    "camera",
                    "light",
                    "s shadow",
                    "gravity",
                };
                for (const char* name : kNames) {
                    SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
                }
            } else {
                static constexpr const wchar_t* kNames[] = {
                    L"\x30ab\x30e1\x30e9",
                    L"\x7167\x660e",
                    L"\x30bb\x30eb\x30d5\x5f71",
                    L"\x91cd\x529b",
                };
                for (const wchar_t* name : kNames) {
                    SendMessageW(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
                }
            }

            HWND accessoryCombo = GetDlgItem(hwnd, panel::kAccessoryCombo);
            const int count = static_cast<int>(SendMessageA(accessoryCombo, CB_GETCOUNT, 0, 0));
            for (int index = 0; index < count; ++index) {
                char name[100]{};
                SendMessageA(accessoryCombo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(name));
                SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
            }
            SendMessageA(frameCombo, CB_SETCURSEL, 0, 0);
            SendMessageA(GetDlgItem(hwnd, panel::kModelVisibleCheckbox), BM_SETCHECK, BST_UNCHECKED,
                         0);
            RebuildCameraModePanel(app);
            app->CameraMode() = 1;
            PostModelReload2(app);
            HandleWindowSize(app);
            app->EditMode() = ViewportEditMode::None;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        HMENU menu = GetMenu(hwnd);
        EnableMenuCommands(menu, kModeCommands, sizeof(kModeCommands) / sizeof(kModeCommands[0]),
                           MF_GRAYED);
        EnableMenuItem(menu, MDS_CMD_PASTE_AT_DIFFERENT_FRAME,
                       app->ClipboardCounts().accessories != 0 ? MF_ENABLED : MF_GRAYED);
        for (int id = MDS_CMD_LOAD_LIP_SYNC; id <= MDS_CMD_DELETE_EYEBROW_FRAMES; ++id)
            EnableMenuItem(menu, id, MF_GRAYED);
        for (int id = MDS_CMD_SELECT_PHYSICS_BONES; id <= MDS_CMD_TOGGLE_FRAME_PHYSICS; ++id)
            EnableMenuItem(menu, id, MF_GRAYED);
        for (int id = MDS_CMD_SELECT_ALL_CAMERA_FRAMES; id <= MDS_CMD_MULTIPLY_CAMERA_FRAMES; ++id)
            EnableMenuItem(menu, id, MF_ENABLED);

        EnableWindow(GetDlgItem(hwnd, panel::kExpandShrinkButton), FALSE);
        const bool canRegister =
            app->ClipboardCounts().accessories != 0 || app->ClipboardCounts().lights != 0 ||
            app->ClipboardCounts().shadows != 0 || app->ClipboardCounts().cameras != 0 ||
            app->ClipboardCounts().gravity != 0;
        EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), canRegister ? TRUE : FALSE);
        EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton), FALSE);
        SendMessageA(GetDlgItem(hwnd, panel::kShadowCheckbox), BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kAddBlendCheckbox), BM_SETCHECK, BST_UNCHECKED, 0);
        HWND optionWindow = app->EdgeThicknessDialog();
        if (optionWindow != nullptr) {
            DestroyWindow(optionWindow);
            app->EdgeThicknessDialog() = nullptr;
        }
        SetPhysicsMenuState(hwnd, MFS_DISABLED);
    } else {
        if (app->state.coordinateSystem == 2)
            app->state.coordinateSystem = 0;

        const int selectedSlot = FindModelSlotByComboId(app, selection);
        if (selectedSlot >= 0) {
            app->SetSelectedModelSlot(static_cast<std::uint8_t>(selectedSlot));
            mdl::ModelRecord* model = ModelAt(app, selectedSlot);
            PostLoadInit(model);
            model->skeletonCalibration.legLength = -1.0f;
            model->skeletonCalibration.heightScale = 90.0f;
            model->skeletonCalibration.heightOffset = 11.5f;
            model->skeletonCalibration.legIkXOffset = 1.0f;
        }

        if (app->CameraMode() != 0) {
            RebuildModelModePanel(app);
            app->CameraMode() = 0;
            PostModelReload2(app);
            HandleWindowSize(app);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (app->CameraParentModel() >= 0 && app->state.followCameraEnabled != 0) {
            const int oldSelection = app->state.mainModelComboSelection;
            int oldSlot = 0;
            if (oldSelection != 0) {
                const int found = FindModelSlotByComboId(app, oldSelection);
                if (found >= 0)
                    oldSlot = found;
            }
            const int trackedSlot = app->CameraParentModel();
            if (oldSlot == trackedSlot || app->SelectedModelSlot() == trackedSlot) {
                app->ViewOffsetX() = 0.0f;
                app->CameraAttachmentTransformSuppressed() = 0;
                app->ViewOffsetY() = 0.0f;
                ReloadModels(app);
                PostModelReload(app);
            }
        }

        app->state.mainModelComboSelection = selection;
        SendMessageA(GetDlgItem(hwnd, panel::kBoxSelectRadio), BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneMoveRadio), BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneRotateRadio), BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneSelectRadio), BM_SETCHECK, BST_CHECKED, 0);
        app->EditMode() = ViewportEditMode::Bone;

        HMENU menu = GetMenu(hwnd);
        EnableMenuCommands(menu, kModeCommands, sizeof(kModeCommands) / sizeof(kModeCommands[0]),
                           MF_ENABLED);
        EnableMenuItem(menu, MDS_CMD_PASTE_AT_DIFFERENT_FRAME,
                       app->ClipboardCounts().bones != 0 ? MF_ENABLED : MF_GRAYED);
        for (int id = MDS_CMD_LOAD_LIP_SYNC; id <= MDS_CMD_DELETE_EYEBROW_FRAMES; ++id)
            EnableMenuItem(menu, id, MF_ENABLED);
        for (int id = MDS_CMD_SELECT_PHYSICS_BONES; id <= MDS_CMD_TOGGLE_FRAME_PHYSICS; ++id)
            EnableMenuItem(menu, id, MF_ENABLED);
        for (int id = MDS_CMD_SELECT_ALL_CAMERA_FRAMES; id <= MDS_CMD_MULTIPLY_CAMERA_FRAMES; ++id)
            EnableMenuItem(menu, id, MF_GRAYED);

        EnableWindow(GetDlgItem(hwnd, panel::kExpandShrinkButton), TRUE);
        EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), FALSE);
        const bool hasMainSelection = app->ClipboardCounts().bones != 0;
        const bool hasOtherSelection =
            app->ClipboardCounts().morphs != 0 || app->ClipboardCounts().displays != 0;
        EnableWindow(GetDlgItem(hwnd, panel::kPasteButton),
                     (hasOtherSelection || hasMainSelection) ? TRUE : FALSE);
        EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton), hasMainSelection ? TRUE : FALSE);

        mdl::ModelRecord* model = app->SelectedModel();
        HWND optionWindow = app->EdgeThicknessDialog();
        if (optionWindow != nullptr && model != nullptr) {
            char text[256]{};
            sprintf_s(text, "%3.2f", model->edgeScale);
            HWND edit = GetDlgItem(optionWindow, panel::kEdgeThicknessEdit);
            SendMessageA(edit, EM_SETSEL, 0, GetWindowTextLengthA(edit));
            SendMessageA(edit, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(text));
            SendMessageA(GetDlgItem(optionWindow, panel::kEdgeThicknessSlider), TBM_SETPOS, TRUE,
                         static_cast<LPARAM>(model->edgeScale * 100.0f));
        }
        SetPhysicsMenuState(hwnd, model != nullptr && model->physicsMode == 2 ? MFS_DISABLED
                                                                              : MFS_ENABLED);
    }

    if (app->CameraMode() != 0) {
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), FALSE);
        EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
    } else {
        mdl::ModelRecord* model = app->SelectedModel();
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton),
                     model != nullptr && model->undoDirty != 0);
        EnableWindow(GetDlgItem(hwnd, panel::kRedoButton),
                     model != nullptr && model->redoDirty != 0);
    }
    RebuildTimelineTrackPanel(app);
    InvalidateSelectionReadouts(app);
}

} // namespace mikudancestudio
