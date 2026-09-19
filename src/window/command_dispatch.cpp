// ===========================================================================
// VA 0x0047E8A0 - CommandDispatch  (original: sub_47E8A0, 0x10A6A = 68KB)
// ===========================================================================
// WM_COMMAND target from MainWndProc (0x004C3A10): a ~368-case switch on
// LOWORD(wParam).  The switch body is split by id family into the files
// below (each keeps its own case -> VA mapping in the header comment):
//   0x000..0x0FF  File menu          200..250  command_file_menu.cpp
//   0x0FB..0x12E  View/option menu   251..302  command_view_menu.cpp
//   0x190..0x1C1  control notif.     400..449  command_frame_edit.cpp
//   0x1C2..0x1F3  control notif.     450..499  command_panel_toggles.cpp
//   0x1F4..0x237  control notif.     500..567  command_frame_register.cpp
// The 19 cases implemented directly below (200 / 0xC9..0xDC) predate the
// family split and stay here; everything else funnels to the families.
//
// The original default region def_47E903 (0x482897; x64 dispatcher
// sub_7FF7CB45F550 default at 0x7ff7cb472c40) is NOT a no-op: ids without a
// jump-table entry whose HIWORD(wParam) == 1 (CBN_SELCHANGE) are dispatched
// by control HWND - lParam is compared against GetDlgItem(hwnd, id) of the
// combos 0x1B4/0x1BB/0x1D7/0x1C1/0x1C2/0x1DA/0x1DB/0x1F8/0x1FD/0x202/0x207/
// 0x1B1/0x1B2 in this fixed order.  That chain is ported below as
// DefaultSelChangeChain (control 0x1B4 heads the original chain but stays
// ported as family case 436 in command_frame_edit.cpp, equivalent
// semantics).  x86 handler VA per control:
//   0x1B4 0x4828AA  0x1BB 0x48E214  0x1D7 0x48E2A4  0x1C1 0x48E37C
//   0x1C2 0x48E62F  0x1DA 0x48E75E  0x1DB 0x48E8FD  0x1F8 0x48EA48
//   0x1FD 0x48EC56  0x202 0x48EE66  0x207 0x48F066  0x1B1 0x48F263
//   0x1B2 0x48F27E
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <cstdio>
#include <cstdint>
#include <cstring>

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// Family entry points (defined in the per-family TUs listed above).
void CmdFileMenu(MMDApp* app, HWND hwnd, std::uint16_t id, std::uint16_t notify);
void CmdViewMenu(MMDApp* app, HWND hwnd, std::uint16_t id, std::uint16_t notify);
void CmdControl400(MMDApp* app, HWND hwnd, std::uint16_t id, std::uint16_t notify);
void CmdControl450(MMDApp* app, HWND hwnd, std::uint16_t id, std::uint16_t notify);
void CmdControl500(MMDApp* app, HWND hwnd, std::uint16_t id, std::uint16_t notify);

namespace {

// Model combo-order byte (model+0x2D7C): the value matched against the
// cursor of the parent-model combos 0x1B4/0x1C1/0x1DA (case 437's lookup;
// same constant as the 400 family).
constexpr std::size_t kModelComboOrder2D7C = 0x2D7C;

// Accessory timeline-row selection flag (x86 acc+0x4AC, x64 acc+0x4BC): set
// on the accessory chosen in combo 0x1D7, cleared on all others, and read by
// the (not yet ported) pump Enter-registration block.  No promoted accessor
// exists yet - the byte lives in the record's reserved tail.
// rowSelected lives on AccessoryRecord (accessory_layout.hpp).

// Shared body of the 0x1C1/0x1DA model-found branches (0x48e439..0x48e4ed /
// 0x48e7f5..0x48e8db): repopulate a bone combo with the model's selectable
// bones (type byte +0x1E4 == 8 or < 7; EN name +0x14 when the english-UI
// byte 0xA0B4C is set, JP name +0x0 otherwise) and set the cursor to 0.
void RepopulateBoneCombo(MMDApp* app, HWND combo, std::int32_t modelSlot) {
    unsigned char* model = app->ModelSlot(modelSlot);
    const mdl::ModelRecord* record = mdl::Mdl(model);
    const mdl::BoneRecord* bones = record->boneTable;
    for (std::int32_t i = 0;
         i < static_cast<std::int32_t>(record->boneCount); ++i) {
        const mdl::BoneType type = bones[i].type;
        if (type == mdl::BoneType::FixedAxis ||
            type < mdl::BoneType::InertTip) {
            const char* name = app->state.englishUI != 0
                                   ? bones[i].nameEn
                                   : bones[i].name;
            SendMessageA(combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(name));
        }
    }
    SendMessageA(combo, CB_SETCURSEL, 0, 0);
}

// Shared body of the 0x1C2/0x1DB bone combos (0x48e6b3..0x48e745 /
// 0x48e98d..0x48ea43): the first bone whose EN (+0x14) or JP (+0x0) name
// equals the combo text (the original's inline two-byte-step compare).
std::int32_t FindBoneByName(unsigned char* model, const char* text) {
    const mdl::ModelRecord* record = mdl::Mdl(model);
    const mdl::BoneRecord* bones = record->boneTable;
    for (std::int32_t i = 0;
         i < static_cast<std::int32_t>(record->boneCount); ++i) {
        if (strcmp(bones[i].nameEn, text) == 0 ||
            strcmp(bones[i].name, text) == 0) {
            return i;
        }
    }
    return -1;
}

// Shared body of the four facial morph-row combos 0x1F8/0x1FD/0x202/0x207
// (0x48ea48 / 0x48ec56 / 0x48ee66 / 0x48f066): the combo text is matched
// against the morph table (model+0x26C4, 0x88 stride, EN name +0x14 / JP
// +0x0, current value float +0x30) and the index stored into
// selectedMorphs[lane] (0x2D9C+4*lane); the row slider receives
// TBM_SETPOS (int)(v*100) and the edit the "%5.4f" echo.  Every
// display-frame row (model+0x26DC, 0x2E stride) then gets its selected byte
// (+0x2C) set when its target word (+0x2A) equals the new morph index,
// cleared otherwise.  Tail: the two language sweeps.
void MorphRowSelect(MMDApp* app, HWND hwnd, HWND combo, std::size_t lane,
                    int spinId, int textId) {
    char text[0x100];
    const LRESULT sel =
        SendMessageA(combo, CB_GETCURSEL, 0, 0);
    SendMessageA(combo, CB_GETLBTEXT, sel,
                 reinterpret_cast<LPARAM>(text));
    const mdl::ModelRecord* record = mdl::Mdl(app->SelectedModel());
    if (static_cast<std::int32_t>(record->morphCount) <= 0) {
        return;
    }
    const mdl::MorphRecord* morphs = record->morphs;
    std::int32_t index = -1;
    for (std::uint32_t i = 0; i < record->morphCount; ++i) {
        if (strcmp(morphs[i].nameEn, text) == 0 ||
            strcmp(morphs[i].name, text) == 0) {
            index = static_cast<std::int32_t>(i);
            break;
        }
    }
    if (index < 0) {
        return;
    }
    mdl::Mdl(app->SelectedModel())->selectedMorphs[lane] = index;
    const float v = record->morphs[index].value;
    SendMessageA(GetDlgItem(hwnd, spinId), TBM_SETPOS, 1,
                 static_cast<LPARAM>(
                     static_cast<std::int32_t>(v * 100.0)));  // dbl_52B8E0
    sprintf_s(text, 0x100u, "%5.4f", static_cast<double>(v));
    SetWindowTextA(GetDlgItem(hwnd, textId), text);
    // display-frame row sweep (0x48ec01..0x48ec41)
    mdl::ModelRecord* live = mdl::Mdl(app->SelectedModel());
    mdl::FrameGroup* groups = live->displayFrames;
    for (std::uint8_t i = 0; i < live->facialFrameCount; ++i) {
        groups[i].selected = (groups[i].targetIndex == index) ? 1 : 0;
    }
    PostLanguageSweep(app);   // 0x42F1E0 (x64 sub_7FF7CB47F7F0)
    PostLanguageSweep2(app);  // 0x40D070 (x64 sub_7FF7CB440CE0)
}

// The default-chain compare chain (def_47E903 @ 0x482897, x64 default at
// 0x7ff7cb472c40).  Returns true when a control matched and its handler ran;
// every matched path ends at the SetFocus(hwnd) exit (0x48f2d3) except
// 0x1DA's "no accessory selected" bail (0x48f2e0), which keeps the focus.
bool DefaultSelChangeChain(MMDApp* app, HWND hwnd, HWND ctrl) {
    // ---- 0x1BB (0x48e214): IK list combo -> on/off radio pair sync.  The
    // flag byte of the 24-byte-stride IK table model+0x26C0 (the same entry
    // cases 444/445 write) picks between radios 0x1BC (on) / 0x1BD (off).
    if (ctrl == GetDlgItem(hwnd, panel::kIkChainCombo)) {
        const LRESULT sel = SendMessageA(GetDlgItem(hwnd, panel::kIkChainCombo),
                                         CB_GETCURSEL, 0, 0);
        mdl::IkChain* chains = mdl::Mdl(app->SelectedModel())->ikChains;
        if (chains != nullptr) {
            CheckRadioButton(hwnd, 0x1BC, 0x1BD,
                             chains[sel].enabled != 0 ? 0x1BC : 0x1BD);
        }
        SetFocus(hwnd);
        return true;
    }

    // ---- 0x1D7 (0x48e2a4): main accessory combo.  Finds the accessory
    // (order byte +0x49D) whose combo entry was picked, drops the four
    // global-row selection bytes 0xA03E4..0xA03E7 and every accessory row
    // flag, marks the new accessory, and re-syncs its edit panel
    // (0x4134E0).  Both paths end with the two language sweeps.
    if (ctrl == GetDlgItem(hwnd, panel::kAccessoryCombo)) {
        const LRESULT sel = SendMessageA(GetDlgItem(hwnd, panel::kAccessoryCombo),
                                         CB_GETCURSEL, 0, 0);
        std::int32_t slot = 0;
        bool found = false;
        for (; slot < 0xFF; ++slot) {
            mdl::AccessoryRecord* acc = app->AccessorySlot(slot);
            if (acc != nullptr &&
                static_cast<int>(acc->order) == static_cast<int>(sel)) {
                found = true;
                break;
            }
        }
        if (found) {
            app->GlobalTrackSelected(GlobalTimelineTrack::Camera) = 0;
            app->GlobalTrackSelected(GlobalTimelineTrack::Light) = 0;
            app->GlobalTrackSelected(GlobalTimelineTrack::SelfShadow) = 0;
            app->GlobalTrackSelected(GlobalTimelineTrack::Gravity) = 0;
            for (std::int32_t i = 0; i < 0xFF; ++i) {
                mdl::AccessoryRecord* acc = app->AccessorySlot(i);
                if (acc != nullptr) {
                    acc->rowSelected = 0;
                }
            }
            app->SelectedAccessorySlot() =
                static_cast<std::uint8_t>(slot);
            app->AccessorySlot(slot)->rowSelected = 1;
            SyncAccessoryEditPanel(app);  // 0x4134E0 accessory edit panel sync
        }
        PostLanguageSweep(app);
        PostLanguageSweep2(app);
        SetFocus(hwnd);
        return true;
    }

    // ---- 0x1C1 (0x48e37c): camera manipulation parent MODEL combo.
    // Cursor > 0 selects the model (combo-order byte 0x2D7C), rebuilds the
    // 0x1C2 bone list from its selectable bones and resets the parent bone;
    // cursor <= 0 detaches, keeping the attach bone's world position
    // (matInit * position, x87 double intermediates), zeroing the rotation
    // and setting the distance to -20.0f (0x52F0B4).  Tail:
    // RefreshRequest(-1) + PostViewRefresh.
    if (ctrl == GetDlgItem(hwnd, panel::kMainComboNormal)) {
        const LRESULT sel = SendMessageA(GetDlgItem(hwnd, panel::kMainComboNormal),
                                         CB_GETCURSEL, 0, 0);
        HWND boneCombo = GetDlgItem(hwnd, panel::kBoneRegisterCombo);
        SendMessageA(boneCombo, CB_RESETCONTENT, 0, 0);
        if (sel > 0) {
            std::int32_t slot = 0;
            bool found = false;
            // 槽扫描界 255：x64 0x7FF7CB472E70 处 mov r13d,0FFh 后按 8 字节
            // 步进扫槽数组（x86 原版才是 0x64）
            for (; slot < kModelSlotCount; ++slot) {
                unsigned char* model = app->ModelSlot(slot);
                if (model != nullptr &&
                    mdl::Mdl(model)->comboSelIndex ==
                        static_cast<int>(sel)) {
                    found = true;
                    break;
                }
            }
            if (found) {
                if (app->CameraParentModel() < 0) {
                    // no previous parent: clear the manipulation floats
                    // (0x334..0x33C, 0x310..0x318)
                    app->CameraPosition()[0] = 0.0f;
                    app->CameraPosition()[1] = 0.0f;
                    app->CameraPosition()[2] = 0.0f;
                    app->CameraRotation()[0] = 0.0f;
                    app->CameraRotation()[1] = 0.0f;
                    app->CameraRotation()[2] = 0.0f;
                }
                app->CameraParentModel() = slot;
                RepopulateBoneCombo(app, boneCombo, slot);
                app->CameraParentBone() = 0;
            }
        } else {
            const std::int32_t parent = app->CameraParentModel();
            if (parent >= 0) {
                const mdl::BoneRecord& bone =
                    mdl::Mdl(app->ModelSlot(parent))
                        ->boneTable[app->CameraParentBone()];
                const double px = bone.position[0];
                const double py = bone.position[1];
                const double pz = bone.position[2];
                app->CameraPosition()[0] = static_cast<float>(
                    py * bone.matInit[4] + px * bone.matInit[0] +
                    pz * bone.matInit[8] + bone.matInit[12]);
                app->CameraPosition()[1] = static_cast<float>(
                    py * bone.matInit[5] + px * bone.matInit[1] +
                    pz * bone.matInit[9] + bone.matInit[13]);
                app->CameraPosition()[2] = static_cast<float>(
                    py * bone.matInit[6] + px * bone.matInit[2] +
                    pz * bone.matInit[10] + bone.matInit[14]);
                app->CameraDistance() = -20.0f;
                app->CameraRotation()[0] = 0.0f;
                app->CameraRotation()[1] = 0.0f;
                app->CameraRotation()[2] = 0.0f;
            }
            SendMessageA(GetDlgItem(hwnd, panel::kBoneRegisterCombo),
                         CB_RESETCONTENT, 0, 0);
            app->CameraParentModel() = -1;
        }
        RefreshRequest(-1);    // 0x440AC0 (x64 sub_7FF7CB4BF4F0)
        PostViewRefresh(app);  // 0x40D130 (x64 sub_7FF7CB440DD0)
        SetFocus(hwnd);
        return true;
    }

    // ---- 0x1C2 (0x48e62f): camera manipulation parent BONE combo.  The
    // combo text is matched against the parent model's bone names; a hit
    // stores the bone index into the parent-bone field (0xA0434) and
    // refreshes the timeline row selection.
    if (ctrl == GetDlgItem(hwnd, panel::kBoneRegisterCombo)) {
        char text[0x100];
        const LRESULT sel = SendMessageA(GetDlgItem(hwnd, panel::kBoneRegisterCombo),
                                         CB_GETCURSEL, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneRegisterCombo), CB_GETLBTEXT, sel,
                     reinterpret_cast<LPARAM>(text));
        const std::int32_t parent = app->CameraParentModel();
        if (parent >= 0) {
            const std::int32_t index =
                FindBoneByName(app->ModelSlot(parent), text);
            if (index >= 0) {
                app->CameraParentBone() = index;
                RefreshRequest(-1);
            }
        }
        SetFocus(hwnd);
        return true;
    }

    // ---- 0x1DA (0x48e75e): accessory parent MODEL combo (accessory from
    // the 0x9E170 slot byte).  Cursor > 0 re-parents to the model found by
    // its 0x2D7C order byte, rebuilding the 0x1DB bone list; cursor <= 0
    // detaches (parentModel = -1).  With no selected accessory the whole
    // handler bails without touching the focus (0x48f2e0).
    if (ctrl == GetDlgItem(hwnd, panel::kMainComboGround)) {
        mdl::AccessoryRecord* acc =
            app->AccessorySlot(app->SelectedAccessorySlot());
        if (acc == nullptr) {
            return true;
        }
        const LRESULT sel = SendMessageA(GetDlgItem(hwnd, panel::kMainComboGround),
                                         CB_GETCURSEL, 0, 0);
        HWND boneCombo = GetDlgItem(hwnd, panel::kAttachBoneCombo);
        SendMessageA(boneCombo, CB_RESETCONTENT, 0, 0);
        if (sel > 0) {
            std::int32_t slot = 0;
            bool found = false;
            // 槽扫描界 255：x64 0x7FF7CB4732F7 处 mov r13d,0FFh（同 0x1C1）
            for (; slot < kModelSlotCount; ++slot) {
                unsigned char* model = app->ModelSlot(slot);
                if (model != nullptr &&
                    mdl::Mdl(model)->comboSelIndex ==
                        static_cast<int>(sel)) {
                    found = true;
                    break;
                }
            }
            if (found) {
                acc->parentModel = slot;
                RepopulateBoneCombo(app, boneCombo, slot);
                acc->parentBone = 0;
                RefreshRequest(
                    static_cast<int>(app->SelectedAccessorySlot()));
            }
        } else {
            acc->parentModel = -1;
        }
        SetFocus(hwnd);
        return true;
    }

    // ---- 0x1DB (0x48e8fd): accessory parent BONE combo.  The combo text
    // is matched against the accessory's parent model bones; a hit stores
    // the bone index into acc+0x234 and refreshes the timeline row.
    if (ctrl == GetDlgItem(hwnd, panel::kAttachBoneCombo)) {
        char text[0x100];
        const LRESULT sel = SendMessageA(GetDlgItem(hwnd, panel::kAttachBoneCombo),
                                         CB_GETCURSEL, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kAttachBoneCombo), CB_GETLBTEXT, sel,
                     reinterpret_cast<LPARAM>(text));
        mdl::AccessoryRecord* acc =
            app->AccessorySlot(app->SelectedAccessorySlot());
        if (acc != nullptr && acc->parentModel >= 0) {
            const std::int32_t index =
                FindBoneByName(app->ModelSlot(acc->parentModel), text);
            if (index >= 0) {
                acc->parentBone = index;
                RefreshRequest(
                    static_cast<int>(app->SelectedAccessorySlot()));
            }
        }
        SetFocus(hwnd);
        return true;
    }

    // ---- 0x1F8 / 0x1FD / 0x202 / 0x207: facial morph row combos (the
    // slider/edit partners 0x1F9/0x1FA etc. never emit WM_COMMAND).
    if (ctrl == GetDlgItem(hwnd, panel::kMorphCombo0)) {
        MorphRowSelect(app, hwnd, ctrl, 0, 0x1F9, 0x1FA);
        SetFocus(hwnd);
        return true;
    }
    if (ctrl == GetDlgItem(hwnd, panel::kMorphCombo1)) {
        MorphRowSelect(app, hwnd, ctrl, 1, 0x1FE, 0x1FF);
        SetFocus(hwnd);
        return true;
    }
    if (ctrl == GetDlgItem(hwnd, panel::kMorphCombo2)) {
        MorphRowSelect(app, hwnd, ctrl, 2, 0x203, 0x204);
        SetFocus(hwnd);
        return true;
    }
    if (ctrl == GetDlgItem(hwnd, panel::kMorphCombo3)) {
        MorphRowSelect(app, hwnd, ctrl, 3, 0x208, 0x209);
        SetFocus(hwnd);
        return true;
    }

    // ---- 0x1B1 (0x48f263): light-colour interpolation row combo ->
    // SelectionReeval (the x64 chain jumps straight into case 431's tail
    // at 0x7ff7cb463665 for the same effect).
    if (ctrl == GetDlgItem(hwnd, panel::kInterpCurveCombo)) {
        SelectionReeval(app);
        SetFocus(hwnd);
        return true;
    }

    // ---- 0x1B2 (0x48f27e): selection-target combo.  The cursor is stored
    // on the active model (0x4CCF0, restored when the model is re-selected;
    // cleared to 3 on scene load).
    if (ctrl == GetDlgItem(hwnd, panel::kRegisterScopeCombo)) {
        unsigned char* model = app->SelectedModel();
        if (model != nullptr) {
            const LRESULT sel = SendMessageA(GetDlgItem(hwnd, panel::kRegisterScopeCombo),
                                             CB_GETCURSEL, 0, 0);
            mdl::Mdl(model)->frameRegistrationSelection =
                static_cast<std::int32_t>(sel);
        }
        SetFocus(hwnd);
        return true;
    }

    return false;
}

}  // namespace

void CommandDispatch(HWND ctrl, WPARAM wParam) {
    MMDApp* app = g_Block;
    auto& s = *app;
    HWND hwnd = static_cast<HWND>(s.Hwnd());
    const std::uint16_t id = LOWORD(wParam);
    const std::uint16_t notification = HIWORD(wParam);

    switch (id) {
    case 200:        // File: Exit (0x48BCF9) - flag + WM_CLOSE
        s.state.dialogFlags[0] = 1;
        SendMessageA(hwnd, WM_CLOSE, 0, 0);
        break;

    case 0xC9: {     // Help: About (x64 0x7FF7CB472BAD)
        // Raise the dialog-in-flight flags (x64 app+0x58/+0xC0 =
        // dialogFlags[9]/enterKeyState), compose the banner into a 256-byte ANSI
        // buffer - the Japanese branch carries the Shift-JIS bytes of the
        // author's name (樋口優, 0x7FF7CB54F980) - and pop it over the
        // floating viewport window when one exists (x64 app+0xA1DE0).
        // 中段括注按构建架构选择：x64 参考二进制 0x7FF7CB54F930（EN）/
        // 0x7FF7CB54F980（JP）为 "(64bitOS Version)"，x86 原版为
        // "(DirectX9 Version)"（机制同 kModelSlotCount 的按架构取值，但括注
        // 嵌在字面量中间，只能用预处理拼接而非 sizeof(void*) 三元）。
        s.state.dialogFlags[9] = 1;
        s.state.enterKeyState = 1;
        char text[256];
#if defined(_M_X64)
#define MDS_ABOUT_ARCH_TAG "(64bitOS Version)"
#else
#define MDS_ABOUT_ARCH_TAG "(DirectX9 Version)"
#endif
        sprintf_s(text, 256,
                  s.EnglishUI() != 0
                      ? "MikuDanceStudio Ver.%4.2f\n  " MDS_ABOUT_ARCH_TAG "\n\n"
                        "programmed by Yu Higuchi"
                      : "MikuDanceStudio Ver.%4.2f\n  " MDS_ABOUT_ARCH_TAG "\n\n"
                        "programmed by \x94\xF3\x8C\xFB\x97\x44",
                  9.32);
#undef MDS_ABOUT_ARCH_TAG
        const HWND owner = s.state.floatingWindow != 0
                               ? reinterpret_cast<HWND>(s.state.floatingWindow)
                               : hwnd;
        MessageBoxA(owner, text, "About", MB_OK);
        break;
    }

    case 0xCA:      // File: load VPD pose
        CmdLoadPose(app);
        break;
    case 0xCB:      // File: save VPD pose (selected-bones check inside)
        CmdSavePose(app);
        break;
    case 0xCC:      // File: New - reset scene state
        CmdResetState(app);
        break;
    case 0xCD:      // File: open PMM (dirty confirm + dialog)
        CmdOpenScene(app);
        break;
    case 0xCE:      // File: open WAV
        CmdOpenWave(app);
        break;
    case 0xCF:      // File: save PMM (0x489AF7) - overwrite via stored path,
        //          falling into the save-as flow (case 0xD0) when unnamed
        // Entry guard first (x64 0x7FF7CB46E041: mov dword [rbx+54h],1 =
        // dialogFlags[8] before the stored-path test), then the path check
        // (cmp [rbx+0A1924h] = EnvFileName[0]): quick save via sub_7FF7CB4950A0
        // when a path exists, else the case 0xD0 save-as flow (which raises
        // dialogFlags[5]/enterKeyState itself inside CmdSaveScene).
        s.state.dialogFlags[8] = 1;             // 0x7FF7CB46E041 [app+0x54]
        if (app->EnvFileName()[0] != L'\0') {
            SaveSceneFile(app);
        } else {
            CmdSaveScene(app);
        }
        break;
    case 0xD0:      // File: save PMM as (0x489B12 GetSaveFileNameW flow)
        CmdSaveScene(app);
        break;
    case 0xD1:      // File: load VMD motion
        CmdLoadMotion(app);
        break;
    case 0xD2:      // File: save VMD motion
        CmdSaveMotion(app);
        break;
    case 0xD3: {    // View: information display toggle (0x47EAC7/0x47EB00)
        // Entry guard (x64 0x7FF7CB45F6EE: mov dword [rbx+44h],1 =
        // dialogFlags[4]) before the app+0x356 toggle - same pattern as
        // sibling case 297 (command_view_menu.cpp).
        s.state.dialogFlags[4] = 1;             // 0x7FF7CB45F6EE [app+0x44]
        auto& flag = s.state.fpsOverlayEnabled;
        if (flag != 0) {
            flag = 0;
            CheckMenuItem(GetMenu(hwnd), 0xD3, MF_UNCHECKED);
        } else {
            flag = 1;
            s.state.fpsOverlayElapsedSeconds = 0.0f;   // timer cluster reset
            s.state.fpsOverlayFrameCount = 0;
            CheckMenuItem(GetMenu(hwnd), 0xD3, MF_CHECKED);
        }
        // 0x47EB56/0x47EB6F: sync the 0x227 checkbox - owned by the
        // floating window (0xA0D38) when open, else the main window.
        HWND owner3 = static_cast<HWND>(s.FloatingWindow());
        if (owner3 == nullptr) owner3 = hwnd;
        SendMessageA(GetDlgItem(owner3, panel::kInfoCheckbox), BM_SETCHECK,
                     s.state.fpsOverlayEnabled, 0);
        break;
    }
    case 0xD5:      // Background: load AVI file
        CmdLoadAvi(app);
        break;
    case 0xD6: {    // View: character transparent mode (0x487FA0)
        auto& flag = s.state.characterTransparentMode;
        flag = flag ? 0 : 1;
        CheckMenuItem(GetMenu(hwnd), 0xD6, flag ? MF_CHECKED : MF_UNCHECKED);
        // 0x487FF5..0x48803C: push the new flag into every loaded model's
        // displayState (+0x2D8C)
        for (int slot = 0; slot < kModelSlotCount; ++slot) {
            unsigned char* model = s.ModelSlot(slot);
            if (model != nullptr)
                mdl::Mdl(model)->displayState = flag;
        }
        break;
    }
    case 0xD7: {    // View: coordinate axis display (0x47FBF5)
        auto& flag = s.state.groundGridEnabled;
        flag = flag ? 0 : 1;
        CheckMenuItem(GetMenu(hwnd), 0xD7, flag ? MF_CHECKED : MF_UNCHECKED);
        // 0x47FC4F/0x47FC8B: sync the 0x22D checkbox - owned by the
        // floating window (0xA0D38) when open, else the main window.
        HWND owner7 = static_cast<HWND>(s.FloatingWindow());
        if (owner7 == nullptr) owner7 = hwnd;
        SendMessageA(GetDlgItem(owner7, panel::kCoordAxisCheckbox), BM_SETCHECK,
                     flag, 0);
        break;
    }
    case 0xD8: {    // Background: AVI display (0x4871F9)
        // Original: clearing is free; setting requires a loaded AVI
        // stream (dword 0x9E400 != 0) - with no AVI the case is a no-op.
        auto& flag = s.state.aviBackgroundEnabled;
        if (flag == 1) {
            flag = 0;
            CheckMenuItem(GetMenu(hwnd), 0xD8, MF_UNCHECKED);
        } else if (s.state.aviStream != 0) {
            flag = 1;
            CheckMenuItem(GetMenu(hwnd), 0xD8, MF_CHECKED);
        }
        break;
    }
    case 0xD9:      // Edit: select all bone frames (0x4831EA)
        SelectFrameGroup(app, 0);
        break;
    case 0xDA:      // Edit: select all disp/IK/OP frames (0x4832C8)
        SelectFrameGroup(app, 1);
        break;
    case 0xDC:      // Edit: select all facial frames (0x483258)
        SelectFrameGroup(app, 2);
        break;

    default:
        // Original default region def_47E903 (0x482897): a CBN_SELCHANGE
        // notification is dispatched by control HWND through the combo chain
        // ported above (control 0x1B4 = id 436 stays in the 400 family);
        // everything else funnels into the per-family TUs by id range (each
        // family switches on its own cases; unlisted ids and the original
        // default region 303..399 are no-ops, matching def_47E903).
        if (notification == 1 /*CBN_SELCHANGE*/ &&
            DefaultSelChangeChain(app, hwnd, ctrl)) {
            break;
        }
        if (id >= 200 && id <= 250) {
            CmdFileMenu(app, hwnd, id, notification);
        } else if (id >= 251 && id <= 302) {
            CmdViewMenu(app, hwnd, id, notification);
        } else if (id >= 400 && id <= 449) {
            CmdControl400(app, hwnd, id, notification);
        } else if (id >= 450 && id <= 499) {
            CmdControl450(app, hwnd, id, notification);
        } else if (id >= 500 && id <= 567) {
            CmdControl500(app, hwnd, id, notification);
        }
        break;
    }
}

}  // namespace mikudancestudio
