// ===========================================================================
// VA 0x0040D940 / 0x0044D610 - model/camera panel refresh helpers
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
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

void SelectionReeval(MMDApp* app);  // 0x430510
void ApplyGravityTrack(MMDApp* app);        // 0x412330
void ApplyAccessoryTrack(MMDApp* app, int);   // 0x413120

namespace {

HWND MainControl(MMDApp* app, int id) {
    return GetDlgItem(static_cast<HWND>(app->Hwnd()), id);
}

void ShowControlRange(MMDApp* app, int first, int last, bool show) {
    for (int id = first; id <= last; ++id)
        ShowWindow(MainControl(app, id), show ? SW_SHOWNORMAL : SW_HIDE);
}

unsigned char* ModelAt(MMDApp* app, int slot) {
    return app->ModelSlot(slot);
}

int FindModelSlotByComboId(MMDApp* app, int comboId) {
    // x64 twin sub_7FF7CB486B0: both comboSelIndex (model+0x3108/x86 11644)
    // scans run to 255 (cmp ecx, 0FFh at 0x7FF7CB48724D and 0x7FF7CB48735A).
    for (int slot = 0; slot < kModelSlotCount; ++slot) {
        unsigned char* model = ModelAt(app, slot);
        if (model != nullptr && mdl::Mdl(model)->comboSelIndex == comboId)
            return slot;
    }
    return -1;
}

void EnableMenuCommands(HMENU menu, const int* ids, std::size_t count,
                        UINT state) {
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

mikudancestudio::mdl::BoneRecord* BoneAt(unsigned char* model, int boneIndex) {
    return mikudancestudio::mdl::Bones(model) + boneIndex;
}

void BonePoint(mikudancestudio::mdl::BoneRecord* bone, float out[3]) {
    const float* matrix = reinterpret_cast<const float*>(bone->matInit);
    const float* point = reinterpret_cast<const float*>(bone->position);
    out[0] = matrix[0] * point[0] + matrix[4] * point[1] +
             matrix[8] * point[2] + matrix[12];
    out[1] = matrix[1] * point[0] + matrix[5] * point[1] +
             matrix[9] * point[2] + matrix[13];
    out[2] = matrix[2] * point[0] + matrix[6] * point[1] +
             matrix[10] * point[2] + matrix[14];
}

}  // namespace

void PostModelReload(MMDApp* app) {  // 0x41A650
#ifdef MIKUDANCESTUDIO_DIAG
    if (getenv("MIKUDANCESTUDIO_TRACE_REC")) {
        FILE* tf = fopen(getenv("MIKUDANCESTUDIO_TRACE_REC"), "a");
        if (tf) { fprintf(tf, "PostModelReload targetSlot=%d\n",
                          app->CameraParentModel()); fclose(tf); }
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
        unsigned char* targetModel = ModelAt(app, targetSlot);
        mikudancestudio::mdl::BoneRecord* targetBone = BoneAt(
            targetModel, app->CameraParentBone());
        auto& targetBasis = reinterpret_cast<d3dx::D3DXMATRIXF&>(
            app->CameraAttachmentBasis());
        const float* boneMatrix =
            reinterpret_cast<const float*>(&targetBone->matInit[0]);
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
        api.translation(&targetTranslation, targetPoint[0], targetPoint[1],
                        targetPoint[2]);

        float referencePoint[3]{};
        const auto referenceMode = app->CameraReferenceMode();
        if (referenceMode == CameraAttachmentReference::ModelRoot) {
            unsigned char* model = app->SelectedModel();
            mikudancestudio::mdl::BoneRecord* bone = BoneAt(
                model, mikudancestudio::mdl::Mdl(model)->centerBone);
            const float* matrix = reinterpret_cast<const float*>(bone->matInit);
            referencePoint[0] = matrix[12];
            referencePoint[1] = matrix[13];
            referencePoint[2] = matrix[14];
        } else if (referenceMode == CameraAttachmentReference::SelectedBone) {
            unsigned char* model = app->SelectedModel();
            int selected = mikudancestudio::mdl::Mdl(model)->selectedBone;
            if (selected < 0)
                selected = 0;
            BonePoint(BoneAt(model, selected), referencePoint);
        }

        d3dx::D3DXMATRIXF referenceTranslation{};
        api.translation(&referenceTranslation, referencePoint[0],
                        referencePoint[1], referencePoint[2]);
        api.multiply(&targetTranslation, &targetTranslation, &rotation);
        api.multiply(&referenceTranslation, &referenceTranslation, &rotation);
        app->ViewOffsetX() =
            targetTranslation.m[3][0] - referenceTranslation.m[3][0] +
            app->CameraPositionX();
        app->ViewOffsetY() =
            targetTranslation.m[3][1] - referenceTranslation.m[3][1] +
            app->CameraPositionY();
        app->CameraDistance() =
            targetTranslation.m[3][2] - referenceTranslation.m[3][2] +
            app->CameraPositionZ();
        return;
    }

    d3dx::D3DXMATRIXF targetTranslation{};
    api.translation(&targetTranslation, -app->CameraPositionX(),
                    -app->CameraPositionY(), -app->CameraPositionZ());
    float referencePoint[3]{};
    const auto referenceMode = app->CameraReferenceMode();
    if (referenceMode == CameraAttachmentReference::ModelRoot) {
        unsigned char* model = app->SelectedModel();
        mikudancestudio::mdl::BoneRecord* bone = BoneAt(
            model, mikudancestudio::mdl::Mdl(model)->centerBone);
        const float* matrix = reinterpret_cast<const float*>(bone->matInit);
        referencePoint[0] = -matrix[12];
        referencePoint[1] = -matrix[13];
        referencePoint[2] = -matrix[14];
    } else if (referenceMode == CameraAttachmentReference::SelectedBone) {
        unsigned char* model = app->SelectedModel();
        const int selected = mikudancestudio::mdl::Mdl(model)->selectedBone;
        if (selected >= 0) {
            BonePoint(BoneAt(model, selected), referencePoint);
            referencePoint[0] = -referencePoint[0];
            referencePoint[1] = -referencePoint[1];
            referencePoint[2] = -referencePoint[2];
        }
    }

    d3dx::D3DXMATRIXF referenceTranslation{};
    api.translation(&referenceTranslation, referencePoint[0],
                    referencePoint[1], referencePoint[2]);
    api.multiply(&targetTranslation, &targetTranslation, &rotation);
    api.multiply(&referenceTranslation, &referenceTranslation, &rotation);
    app->ViewOffsetX() -= targetTranslation.m[3][0] -
                          referenceTranslation.m[3][0];
    app->ViewOffsetY() -= targetTranslation.m[3][1] -
                          referenceTranslation.m[3][1];
    app->CameraDistance() -= targetTranslation.m[3][2] -
                             referenceTranslation.m[3][2];
}

// (0x41A650 was a local alias wrapper of PostModelReload above - removed;
//  callers now use the PostModelReload name directly.)

void PostModelReload2(MMDApp* app) {  // 0x40D940
    const bool cameraMode =
        app->state.optflag[0] != 0;

    const struct {
        int first;
        int last;
        int collapsed;
        int flagIndex;
    } cameraGroups[] = {
        {446, 453, 454, 1},
        {455, 469, 470, 2},
        {471, 488, 489, 3},
        {560, 566, 567, 6},
    };
    for (const auto& group : cameraGroups) {
        const bool expanded = cameraMode &&
            app->state.optflag[group.flagIndex] != 0;
        ShowControlRange(app, group.first, group.last, expanded);
        ShowWindow(MainControl(app, group.collapsed),
                   cameraMode && !expanded ? SW_SHOWNORMAL : SW_HIDE);
    }

    const bool modelSelected =
        SendMessageA(MainControl(app, 436), CB_GETCURSEL, 0, 0) != 0;
    if (!modelSelected)
        SendMessageA(MainControl(app, 443), CB_SETCURSEL, 0, 0);
    for (int id = 437; id <= 445; ++id)
        EnableWindow(MainControl(app, id), modelSelected);

    const struct {
        int first;
        int last;
        int collapsed;
        int flagIndex;
    } modelGroups[] = {
        {490, 502, 503, 4},
        {504, 528, 529, 5},
    };
    for (const auto& group : modelGroups) {
        const bool expanded = !cameraMode &&
            app->state.optflag[group.flagIndex] != 0;
        ShowControlRange(app, group.first, group.last, expanded);
        ShowWindow(MainControl(app, group.collapsed),
                   !cameraMode && !expanded ? SW_SHOWNORMAL : SW_HIDE);
    }

    // 0x40DC3A..0x40DC6D: until the bone-copy buffer contains records,
    // paste and reverse-paste remain disabled.  A successful copy command
    // enables both controls and stores the record count at 0x9DA24.
    if (!cameraMode &&
        app->state.optflag[5] != 0 &&
        app->state.copiedBoneCount == 0) {
        EnableWindow(MainControl(app, 497), FALSE);
        EnableWindow(MainControl(app, 498), FALSE);
    }

    HWND viewportWindow =
        app->state.floatingWindow;
    if (viewportWindow == nullptr)
        viewportWindow = static_cast<HWND>(app->Hwnd());

    if (cameraMode) {
        EnableWindow(MainControl(app, 400), FALSE);
        EnableWindow(MainControl(app, 401), FALSE);
        if (app->EnglishUI() != 0) {
            SetWindowTextA(MainControl(app, 407), "btm");
            SetWindowTextA(GetDlgItem(viewportWindow, panel::kModelEditToggle), "To model");
        } else {
            SetWindowTextW(MainControl(app, 407), L"\x4e0b\x9762");
            SetWindowTextW(GetDlgItem(viewportWindow, panel::kModelEditToggle),
                           L"\x30e2\x30c7\x30eb\x7de8");
        }
        ShowWindow(GetDlgItem(viewportWindow, panel::kCameraDistanceButton), SW_SHOW);
        ShowWindow(GetDlgItem(viewportWindow, panel::kReadoutDistEdit), SW_SHOW);
        return;
    }

    auto* model = app->SelectedModel();
    // undoDirty/redoDirty（x64 model+0x3558/+0x3559，0x7FF7CB447074/86）：
    // 撤消/重做按钮的开门标志。
    EnableWindow(MainControl(app, 400),
                 mdl::Mdl(model)->undoDirty != 0);
    EnableWindow(MainControl(app, 401),
                 mdl::Mdl(model)->redoDirty != 0);
    if (app->EnglishUI() != 0) {
        SetWindowTextA(MainControl(app, 407), "camer");
        SetWindowTextA(GetDlgItem(viewportWindow, panel::kModelEditToggle), "To camera");
    } else {
        SetWindowTextW(MainControl(app, 407), L"\x30ab\x30e1\x30e9");
        SetWindowTextW(GetDlgItem(viewportWindow, panel::kModelEditToggle),
                       L"\x30ab\x30e1\x30e9\x7de8");
    }
    ShowWindow(GetDlgItem(viewportWindow, panel::kCameraDistanceButton), SW_HIDE);
    ShowWindow(GetDlgItem(viewportWindow, panel::kReadoutDistEdit), SW_HIDE);
}

void RebuildModelModePanel(MMDApp* app) {  // 0x44D610
    app->state.optflag[0] = 0;
    HWND combo = MainControl(app, 433);
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
        D3DVECTOR& direction = *reinterpret_cast<D3DVECTOR*>(
            app->LightDirection());
        direction.x = -0.5f;                                   // 0x44D711
        direction.y = -1.0f;                                   // 0x44D71D
        direction.z = 0.5f;                                    // 0x44D726
        // 0x44D738..0x44D765: Specular.rgb = 0.602, Direction = the
        // 0x9E174 triple, Ambient.rgb copied from Specular, and Specular.a
        // (0x9E1A0) keeps the old Position.x dword read at 0x44D750 - the
        // struct copy `Specular = Ambient` used here before gave Specular.a
        // the old Ambient.a instead.
        const float oldPositionX = light.Position.x;           // 0x44D750
        light.Specular.r = 0.602f;                             // 0x44D738
        light.Specular.g = 0.602f;                             // 0x44D73B
        light.Specular.b = 0.602f;                             // 0x44D741
        light.Direction = direction;                           // 0x44D73E..762
        light.Ambient.r = light.Specular.r;                    // 0x44D74D
        light.Ambient.g = light.Specular.g;                    // 0x44D759
        light.Ambient.b = light.Specular.b;                    // 0x44D762
        light.Specular.a = oldPositionX;                       // 0x44D765

        IDirect3DDevice9* device = app->Renderer()->device;
        device->SetLight(0, &light);
    }
}

void RebuildCameraModePanel(MMDApp* app) {  // 0x44D780
    // Rebuild the camera-mode transform combo exactly as the original does.
    // Item four is the model-mode "all" entry; the three camera entries are
    // appended in its place.
    app->state.optflag[0] = 1;
    HWND combo = MainControl(app, 433);
    SendMessageA(combo, CB_DELETESTRING, 4, 0);
    if (app->EnglishUI() != 0) {
        SendMessageA(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("distance"));
        SendMessageA(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("view angle"));
        SendMessageA(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("all"));
    } else {
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"\x8ddd\x96e2"));
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"\x8996\x91ce\x89d2"));
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"\x3059\x3079\x3066"));
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

void ApplyModelComboSelection(MMDApp* app) {  // 0x44D940
    HWND hwnd = static_cast<HWND>(app->Hwnd());
    const int selection = static_cast<int>(
        SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_GETCURSEL, 0, 0));
    static constexpr int kModeCommands[] = {
        0xFD, 0xD9, 0xDC, 0xDA, 0xCA, 0xCB, 0xDB, 0xDE,
        0xFB, 0xFC,
    };

    if (selection == 0) {
        if (app->state.optflag[0] == 0) {
            SendMessageA(GetDlgItem(hwnd, panel::kIkChainCombo), CB_RESETCONTENT, 0, 0);
            HWND frameCombo = GetDlgItem(hwnd, panel::kRegisterScopeCombo);
            SendMessageA(frameCombo, CB_RESETCONTENT, 0, 0);
            if (app->EnglishUI() != 0) {
                static constexpr const char* kNames[] = {
                    "camera", "light", "s shadow", "gravity",
                };
                for (const char* name : kNames) {
                    SendMessageA(frameCombo, CB_ADDSTRING, 0,
                                 reinterpret_cast<LPARAM>(name));
                }
            } else {
                static constexpr const wchar_t* kNames[] = {
                    L"\x30ab\x30e1\x30e9", L"\x7167\x660e",
                    L"\x30bb\x30eb\x30d5\x5f71", L"\x91cd\x529b",
                };
                for (const wchar_t* name : kNames) {
                    SendMessageW(frameCombo, CB_ADDSTRING, 0,
                                 reinterpret_cast<LPARAM>(name));
                }
            }

            HWND accessoryCombo = GetDlgItem(hwnd, panel::kAccessoryCombo);
            const int count = static_cast<int>(
                SendMessageA(accessoryCombo, CB_GETCOUNT, 0, 0));
            for (int index = 0; index < count; ++index) {
                char name[100]{};
                SendMessageA(accessoryCombo, CB_GETLBTEXT, index,
                             reinterpret_cast<LPARAM>(name));
                SendMessageA(frameCombo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(name));
            }
            SendMessageA(frameCombo, CB_SETCURSEL, 0, 0);
            SendMessageA(GetDlgItem(hwnd, panel::kModelVisibleCheckbox), BM_SETCHECK, BST_UNCHECKED, 0);
            RebuildCameraModePanel(app);
            app->state.optflag[0] = 1;
            PostModelReload2(app);
            HandleWindowSize(app);
            app->EditMode() = ViewportEditMode::None;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        HMENU menu = GetMenu(hwnd);
        EnableMenuCommands(menu, kModeCommands,
                           sizeof(kModeCommands) / sizeof(kModeCommands[0]),
                           MF_GRAYED);
        EnableMenuItem(menu, 250,
                       app->ClipboardCounts().accessories != 0
                           ? MF_ENABLED
                           : MF_GRAYED);
        for (int id = 224; id <= 231; ++id)
            EnableMenuItem(menu, id, MF_GRAYED);
        for (int id = 273; id <= 275; ++id)
            EnableMenuItem(menu, id, MF_GRAYED);
        for (int id = 0xED; id <= 0xF2; ++id)
            EnableMenuItem(menu, id, MF_ENABLED);

        EnableWindow(GetDlgItem(hwnd, panel::kExpandShrinkButton), FALSE);
        const bool canRegister =
            app->ClipboardCounts().accessories != 0 ||
            app->ClipboardCounts().lights != 0 ||
            app->ClipboardCounts().shadows != 0 ||
            app->ClipboardCounts().cameras != 0 ||
            app->ClipboardCounts().gravity != 0;
        EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), canRegister ? TRUE : FALSE);
        EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton), FALSE);
        SendMessageA(GetDlgItem(hwnd, panel::kShadowCheckbox), BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kAddBlendCheckbox), BM_SETCHECK, BST_UNCHECKED, 0);
        HWND optionWindow = app->state.edgeThicknessDialog;
        if (optionWindow != nullptr) {
            DestroyWindow(optionWindow);
            app->state.edgeThicknessDialog = nullptr;
        }
        SetPhysicsMenuState(hwnd, MFS_DISABLED);
    } else {
        if (app->state.coordinateSystem == 2)
            app->state.coordinateSystem = 0;

        const int selectedSlot = FindModelSlotByComboId(app, selection);
        if (selectedSlot >= 0) {
            app->SetSelectedModelSlot(static_cast<std::uint8_t>(selectedSlot));
            unsigned char* model = ModelAt(app, selectedSlot);
            PostLoadInit(model);
            mdl::Mdl(model)->lightDir[0] = -1.0f;
            mdl::Mdl(model)->lightDir[1] = 90.0f;
            mdl::Mdl(model)->lightDir[2] = 11.5f;
            mdl::Mdl(model)->legIkXOffset = 1.0f;
        }

        if (app->state.optflag[0] != 0) {
            RebuildModelModePanel(app);
            app->state.optflag[0] = 0;
            PostModelReload2(app);
            HandleWindowSize(app);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (app->CameraParentModel() >= 0 &&
                   app->state.followCameraEnabled != 0) {
            const int oldSelection =
                app->state.mainModelComboSelection;
            int oldSlot = 0;
            if (oldSelection != 0) {
                const int found = FindModelSlotByComboId(app, oldSelection);
                if (found >= 0)
                    oldSlot = found;
            }
            const int trackedSlot = app->CameraParentModel();
            if (oldSlot == trackedSlot ||
                app->SelectedModelSlot() == trackedSlot) {
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
        EnableMenuCommands(menu, kModeCommands,
                           sizeof(kModeCommands) / sizeof(kModeCommands[0]),
                           MF_ENABLED);
        EnableMenuItem(menu, 250,
                       app->ClipboardCounts().bones != 0
                           ? MF_ENABLED
                           : MF_GRAYED);
        for (int id = 224; id <= 231; ++id)
            EnableMenuItem(menu, id, MF_ENABLED);
        for (int id = 273; id <= 275; ++id)
            EnableMenuItem(menu, id, MF_ENABLED);
        for (int id = 0xED; id <= 0xF2; ++id)
            EnableMenuItem(menu, id, MF_GRAYED);

        EnableWindow(GetDlgItem(hwnd, panel::kExpandShrinkButton), TRUE);
        EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), FALSE);
        const bool hasMainSelection =
            app->ClipboardCounts().bones != 0;
        const bool hasOtherSelection =
            app->ClipboardCounts().morphs != 0 ||
            app->ClipboardCounts().displays != 0;
        EnableWindow(GetDlgItem(hwnd, panel::kPasteButton),
                     (hasOtherSelection || hasMainSelection) ? TRUE : FALSE);
        EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton),
                     hasMainSelection ? TRUE : FALSE);

        unsigned char* model = app->SelectedModel();
        HWND optionWindow = app->state.edgeThicknessDialog;
        if (optionWindow != nullptr && model != nullptr) {
            char text[256]{};
            sprintf_s(text, "%3.2f", mikudancestudio::mdl::Mdl(model)->edgeScale);
            HWND edit = GetDlgItem(optionWindow, panel::kEdgeThicknessEdit);
            SendMessageA(edit, EM_SETSEL, 0, GetWindowTextLengthA(edit));
            SendMessageA(edit, EM_REPLACESEL, 0,
                         reinterpret_cast<LPARAM>(text));
            SendMessageA(GetDlgItem(optionWindow, panel::kEdgeThicknessSlider), TBM_SETPOS, TRUE,
                         static_cast<LPARAM>(
                             mikudancestudio::mdl::Mdl(model)->edgeScale *
                             100.0f));
        }
        SetPhysicsMenuState(
            hwnd, model != nullptr && mdl::Mdl(model)->physicsMode == 2
                      ? MFS_DISABLED
                      : MFS_ENABLED);
    }

    if (app->state.optflag[0] != 0) {
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), FALSE);
        EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
    } else {
        unsigned char* model = app->SelectedModel();
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton),
                     model != nullptr &&
                         mdl::Mdl(model)->undoDirty != 0);
        EnableWindow(GetDlgItem(hwnd, panel::kRedoButton),
                     model != nullptr &&
                         mdl::Mdl(model)->redoDirty != 0);
    }
    PostLanguageSweep(app);
    PostLanguageSweep2(app);
}

}  // namespace mikudancestudio
