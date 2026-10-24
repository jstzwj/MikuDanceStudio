// ===========================================================================
// English-name dialog (menu 259, sub_464BD0 family)
// ===========================================================================
// Split out of src/window/command_view_menu.cpp (the menu-251..302 command
// family).  This is MMD's "input English notation" dialog (template
// 0x29A EN / 0x299 JP, modeless at app slot 0xA0B50): it edits the English
// model name / comment and the English names of the bones, morphs and
// display groups of the active model.
//
// Dialog layout (control ids, template res 666/665):
//   667 0x29B edit  model English name      668 0x29C edit  model EN comment
//   669 0x29D combo bone list               672 0x2A0 edit  bone EN name
//   673 0x2A1 combo morph list              676 0x2A4 edit  morph EN name
//   677 0x2A5 combo display-group list      680 0x2A8 edit  group EN name
//   0x29E/0x29F, 0x2A2/0x2A3, 0x2A6/0x2A7 next/prev buttons of the combos.
//
// The dialog procedure, edit subclass and name-commit helpers live together.
// Every function keeps its original x86 VA.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/panel_controls.hpp"
#include <Windows.h>

#include <cstdint>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "enhanced_model_messages.hpp"

namespace mikudancestudio {
namespace {

// ---- dialog controls (template 0x29A EN / 0x299 JP) ----------------------
constexpr int kEditModelNameEn = 667;    // 0x29B
constexpr int kEditModelCommentEn = 668; // 0x29C
constexpr int kComboBoneName = 669;      // 0x29D
constexpr int kEditBoneNameEn = 672;     // 0x2A0
constexpr int kComboMorphName = 673;     // 0x2A1
constexpr int kEditMorphNameEn = 676;    // 0x2A4
constexpr int kComboGroupName = 677;     // 0x2A5
constexpr int kEditGroupNameEn = 680;    // 0x2A8

// ---- main-window controls touched while the dialog is up -----------------
constexpr int kMainComboModel = panel::kMainComboModel;   // 0x1B4 model combo (disabled on open)
constexpr int kMainComboGround = panel::kMainComboGround; // 0x1DA
constexpr int kMainComboNormal = panel::kMainComboNormal; // 0x1C1
constexpr int kMainRadioFirst = panel::kBoneSelectRadio;  // BM_SETCHECK: 490 on, 491..493 off

// Fixed heads of the three main-window combos rebuilt by the collector
// (x64 .rdata 0x54B6B8 / 0x54B768 / 0x551074).
const char kHeadModelCombo[] = "camera/light/accessory";
const char kHeadGroundCombo[] = "ground";
const char kHeadNormalCombo[] = "normal";

// The morph combo lists morphs[1..]; the group combo lists display groups
// starting after the fixed "Root"/"Morph" slots - slot 3 when the model owns
// facial frame groups, slot 2 otherwise.
int MorphNameSlot(MMDApp* app) {
    return app->EnglishNameComboCursor(1) + 1;
}

int GroupNameSlot(MMDApp* app, mdl::ModelRecord* model) {
    return app->EnglishNameComboCursor(2) + (model->facialFrameCount != 0 ? 3 : 2);
}

// Mirror the English names into the facial display-frame rows (each row's
// targetIndex addresses the morph table).
void MirrorFacialFrameNames(mdl::ModelRecord* model) {
    mdl::FrameGroup* frames = model->displayFrames;
    mdl::MorphRecord* morphs = model->morphs;
    for (int i = 0; i < model->facialFrameCount; ++i)
        strcpy_s(frames[i].nameEn, 0x14, morphs[frames[i].targetIndex].nameEn);
}

// Same mirror for the bone display-frame rows (the 46-byte table behind
// model->rbGroups; targetIndex addresses the bone table).  The row count is
// the dword also used by the loaders (model->rigidBodyCount).
void MirrorBoneFrameNames(mdl::ModelRecord* model) {
    mdl::FrameGroup* frames = static_cast<mdl::FrameGroup*>(model->rbGroups);
    mdl::BoneRecord* bones = model->boneTable;
    const std::uint32_t count = model->rigidBodyCount;
    for (std::uint32_t i = 0; i < count; ++i)
        strcpy_s(frames[i].nameEn, 0x14, bones[frames[i].targetIndex].nameEn);
}

// Select-all + replace + select-all + focus: the edit refresh shared by the
// three combo helpers (EM_SETSEL(0, len), EM_REPLACESEL, EM_SETSEL, focus).
void RefreshNameEdit(HWND edit, const char* text) {
    SendMessageA(edit, EM_SETSEL, 0, GetWindowTextLengthA(edit));
    SendMessageA(edit, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(text));
    SendMessageA(edit, EM_SETSEL, 0, GetWindowTextLengthA(edit));
    SetFocus(edit);
}

// ===========================================================================
// 0x0043BED0 - English-name collector (original: sub_43BED0)
// ===========================================================================
// Commits one English-name edit into the model.  idx picks the target
// (matching the subclassed edits 0x29B/0x2A0/0x2A4/0x2A8):
//   0: model English name (nameEn, 20 bytes).  In English UI mode the three
//      main-window combos 436/474/449 are also rebuilt from the model slots
//      in combo order (the 1-based order byte comboSelIndex, orders 1..254,
//      first matching slot per order), restoring their old cursors.
//   1: bone English name of combo cursor 0; the bone display-frame rows
//      mirror the new name from their target bone.
//   2: morph English name (combo cursor 1 -> morph index cursor+1), only
//      when the model has facial frame groups; the facial rows mirror, then
//      RebuildTimelineTrackPanel + PostLoadInit refresh the panel.
//   3: display-group English name (combo cursor 2 -> group slot cursor+3 /
//      cursor+2), capped at the 50-byte group record.
// Edit texts are capped at the record widths before the copy (19 chars for
// the 20-byte names, 49 for the 50-byte group name).
//
// =========================================================================//
void CollectEnglishNameEdit(MMDApp* app, HWND hEdit, int idx) { // VA 0x0043BED0
    mdl::ModelRecord* model = mdl::Mdl(app->SelectedModel());
    char text[256];

    if (idx == 0) { // model English name
        GetWindowTextA(hEdit, text, 256);
        if (std::strlen(text) >= 0x14)
            text[19] = '\0';
        strcpy_s(model->nameEn, 0x32, text);

        if (app->state.englishUI != 0) {
            HWND modelCombo = GetDlgItem(app->state.hwnd, kMainComboModel);
            HWND groundCombo = GetDlgItem(app->state.hwnd, kMainComboGround);
            HWND normalCombo = GetDlgItem(app->state.hwnd, kMainComboNormal);
            const int selModel = static_cast<int>(SendMessageA(modelCombo, CB_GETCURSEL, 0, 0));
            const int selGround = static_cast<int>(SendMessageA(groundCombo, CB_GETCURSEL, 0, 0));
            const int selNormal = static_cast<int>(SendMessageA(normalCombo, CB_GETCURSEL, 0, 0));
            SendMessageA(modelCombo, CB_RESETCONTENT, 0, 0);
            SendMessageA(groundCombo, CB_RESETCONTENT, 0, 0);
            SendMessageA(normalCombo, CB_RESETCONTENT, 0, 0);
            SendMessageA(modelCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kHeadModelCombo));
            SendMessageA(groundCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kHeadGroundCombo));
            SendMessageA(normalCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kHeadNormalCombo));
            // x64 sub_7FF7CB4AC790+0x4AC9B4..0x4ACA92: order runs 1..254
            // (inc r12d; cmp r12d,0FFh), the slot scan to 0xFF (cmp
            // edx,0FFh @0x4AC9ED) - kModelSlotCount wide on both walks
            // (255 == kModelSlotCount on x64; the literal also kept the x86
            // build walking past its 100-slot array).
            for (int order = 1; order < kModelSlotCount; ++order) {
                // first model slot carrying this combo-order byte
                mdl::ModelRecord* ordered = nullptr;
                for (int slot = 0; slot < kModelSlotCount; ++slot) {
                    unsigned char* other = app->ModelSlot(slot);
                    if (other != nullptr && mdl::Mdl(other)->comboSelIndex == order) {
                        ordered = mdl::Mdl(other);
                        break;
                    }
                }
                if (ordered == nullptr)
                    continue;
                SendMessageA(modelCombo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(ordered->nameEn));
                SendMessageA(groundCombo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(ordered->nameEn));
                SendMessageA(normalCombo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(ordered->nameEn));
            }
            SendMessageA(modelCombo, CB_SETCURSEL, selModel, 0);
            SendMessageA(groundCombo, CB_SETCURSEL, selGround, 0);
            SendMessageA(normalCombo, CB_SETCURSEL, selNormal, 0);
        }
        return;
    }

    switch (idx) {
    case 1: { // bone English name
        GetWindowTextA(hEdit, text, 256);
        if (std::strlen(text) >= 0x14)
            text[19] = '\0';
        mdl::BoneRecord* bones = model->boneTable;
        strcpy_s(bones[app->EnglishNameComboCursor(0)].nameEn, 0x14, text);
        MirrorBoneFrameNames(model);
        break;
    }
    case 2: { // morph English name (facial models only)
        if (model->facialFrameCount != 0) {
            GetWindowTextA(hEdit, text, 256);
            if (std::strlen(text) >= 0x14)
                text[19] = '\0';
            strcpy_s(model->morphs[MorphNameSlot(app)].nameEn, 0x14, text);
            RebuildTimelineTrackPanel(app); // 0x42F1E0
            MirrorFacialFrameNames(model);
            PostLoadInit(app->SelectedModel()); // 0x49C850
        }
        break;
    }
    case 3: { // display-group English name
        GetWindowTextA(hEdit, text, 256);
        if (std::strlen(text) >= 0x32)
            text[49] = '\0';
        mdl::DisplayGroup* groups = mdl::DisplayGroups(app->SelectedModel());
        strcpy_s(groups[GroupNameSlot(app, model)].nameEn, 0x32, text);
        break;
    }
    default:
        break;
    }
}

// ===========================================================================
// 0x0043C430 - English-name dialog init (original: sub_43C430)
// ===========================================================================
// WM_INITDIALOG body of the English-name dialog:
//   edit 667  <- model nameEn; edit 668 <- model commentEn with line breaks
//               normalized to CRLF ("\r" pairs and lone "\n" both become
//               "\r\n" for the multiline edit);
//   combo 669 <- bone English names bones[0..boneCount-1], select 0;
//   edit 672  <- bones[0].nameEn;
//   combo 673 <- morph English names morphs[1..morphCount-1], select 0;
//   edit 676  <- morphs[1].nameEn (only when facial frame groups exist);
//   combo 677 <- display-group English names starting at group slot 3/2
//               (with/without facial frame groups) up to groupCount-1;
//   edit 680  <- groups[first].nameEn; combo 677 select 0.
// Main window: model combo 436 disabled, radio 490 checked and 491..493
// cleared, editMode reset to 0.  Model: bone selection flags cleared, bone 0
// marked selected (selectedBone = 0).  Finishes with the panel sweep and
// zeroes the three combo cursors.
// =========================================================================//
void InitEnglishNameDialog(HWND hDlg) { // VA 0x0043C430
    MMDApp* app = g_Block;
    mdl::ModelRecord* model = mdl::Mdl(app->SelectedModel());
    mdl::BoneRecord* bones = model->boneTable;
    mdl::MorphRecord* morphs = model->morphs;
    char buf[512];

    // model English name
    strcpy_s(buf, 512, model->nameEn);
    SendMessageA(GetDlgItem(hDlg, kEditModelNameEn), EM_REPLACESEL, 0,
                 reinterpret_cast<LPARAM>(buf));

    // model English comment: expand CR / LF to CRLF for the edit control
    std::size_t out = 0;
    for (const char* p = model->commentEn; *p != '\0';) {
        if (*p == '\r') { // stored CRLF pair -> one CRLF
            buf[out++] = '\r';
            buf[out++] = '\n';
            p += 2;
        } else if (*p == '\n') { // lone LF -> CRLF
            buf[out++] = '\r';
            buf[out++] = '\n';
            ++p;
        } else {
            buf[out++] = *p++;
        }
    }
    buf[out] = '\0';
    SendMessageA(GetDlgItem(hDlg, kEditModelCommentEn), EM_REPLACESEL, 0,
                 reinterpret_cast<LPARAM>(buf));

    // bone combo + edit
    HWND boneCombo = GetDlgItem(hDlg, kComboBoneName);
    SendMessageA(boneCombo, CB_RESETCONTENT, 0, 0);
    for (std::uint32_t j = 0; j < model->boneCount; ++j)
        SendMessageA(boneCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(bones[j].nameEn));
    SendMessageA(boneCombo, CB_SETCURSEL, 0, 0);
    strcpy_s(buf, 512, bones[0].nameEn);
    SendMessageA(GetDlgItem(hDlg, kEditBoneNameEn), EM_REPLACESEL, 0,
                 reinterpret_cast<LPARAM>(buf));

    // morph combo + edit (combo skips morph slot 0)
    HWND morphCombo = GetDlgItem(hDlg, kComboMorphName);
    SendMessageA(morphCombo, CB_RESETCONTENT, 0, 0);
    for (std::uint32_t k = 1; k < model->morphCount; ++k)
        SendMessageA(morphCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(morphs[k].nameEn));
    SendMessageA(morphCombo, CB_SETCURSEL, 0, 0);
    if (model->facialFrameCount != 0) {
        strcpy_s(buf, 512, morphs[1].nameEn);
        SendMessageA(GetDlgItem(hDlg, kEditMorphNameEn), EM_REPLACESEL, 0,
                     reinterpret_cast<LPARAM>(buf));
    }

    // display-group combo + edit (starts after the fixed slots)
    HWND groupCombo = GetDlgItem(hDlg, kComboGroupName);
    SendMessageA(groupCombo, CB_RESETCONTENT, 0, 0);
    mdl::DisplayGroup* groups = mdl::DisplayGroups(app->SelectedModel());
    const std::uint8_t firstGroup = model->facialFrameCount != 0 ? 3 : 2;
    for (std::uint8_t g = firstGroup; g < model->groupCount; ++g)
        SendMessageA(groupCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(groups[g].nameEn));
    strcpy_s(buf, 0x100, groups[firstGroup].nameEn);
    SendMessageA(GetDlgItem(hDlg, kEditGroupNameEn), EM_REPLACESEL, 0,
                 reinterpret_cast<LPARAM>(buf));
    SendMessageA(groupCombo, CB_SETCURSEL, 0, 0);

    // main window: lock the model combo, select the "model" radio group
    EnableWindow(GetDlgItem(app->state.hwnd, kMainComboModel), FALSE);
    SendMessageA(GetDlgItem(app->state.hwnd, kMainRadioFirst + 1), BM_SETCHECK, 0, 0);
    SendMessageA(GetDlgItem(app->state.hwnd, kMainRadioFirst + 2), BM_SETCHECK, 0, 0);
    SendMessageA(GetDlgItem(app->state.hwnd, kMainRadioFirst + 3), BM_SETCHECK, 0, 0);
    SendMessageA(GetDlgItem(app->state.hwnd, kMainRadioFirst), BM_SETCHECK, 1, 0);
    app->state.editMode = 0;

    // model: select bone 0
    unsigned char* selection = model->boneSelection;
    for (std::uint32_t i = 0; i < model->boneCount; ++i)
        selection[i] = 0;
    selection[0] = 1;
    model->selectedBone = 0;

    RebuildTimelineTrackPanel(app); // 0x42F1E0
    app->EnglishNameComboCursor(0) = 0;
    app->EnglishNameComboCursor(1) = 0;
    app->EnglishNameComboCursor(2) = 0;
}

// ===========================================================================
// 0x0045F240 - English-name dialog apply (original: sub_45F240)
// ===========================================================================
// OK/close body of the English-name dialog.  Reads the English comment,
// strips the CRs of the edit's CRLF line breaks and stores it into the
// model's commentEn (256 bytes).  Commits the three name edits through the
// collector (0x43BED0 idx 0/1/2), then the group name edit (identical to
// collector idx 3: cursor 2 -> group slot +3/+2, capped at 49 chars).
// Re-runs both frame-row name mirrors (facial rows from the morph table,
// bone rows from the bone table - the collector already did them; the
// original repeats both loops) and finishes with the panel sweep, the panel
// rebuild PostLoadInit and the second-stage sweep.
// =========================================================================//
void ApplyEnglishNameDialog(HWND hDlg) { // VA 0x0045F240
    MMDApp* app = g_Block;
    mdl::ModelRecord* model = mdl::Mdl(app->SelectedModel());
    char text[256];

    // English comment: model stores LF line breaks, edit uses CRLF
    GetWindowTextA(GetDlgItem(hDlg, kEditModelCommentEn), text, 256);
    if (std::strlen(text) >= 0x100)
        text[255] = '\0';
    char clean[256];
    char* d = clean;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p != '\r')
            *d++ = *p;
    }
    *d = '\0';
    strcpy_s(model->commentEn, 0x100, clean);

    // model / bone / morph English names
    CollectEnglishNameEdit(app, GetDlgItem(hDlg, kEditModelNameEn), 0); // 0x43BED0
    CollectEnglishNameEdit(app, GetDlgItem(hDlg, kEditBoneNameEn), 1);  // 0x43BED0
    CollectEnglishNameEdit(app, GetDlgItem(hDlg, kEditMorphNameEn), 2); // 0x43BED0

    // display-group English name (collector idx-3 commit)
    GetWindowTextA(GetDlgItem(hDlg, kEditGroupNameEn), text, 256);
    if (std::strlen(text) >= 0x32)
        text[49] = '\0';
    mdl::DisplayGroup* groups = mdl::DisplayGroups(app->SelectedModel());
    strcpy_s(groups[GroupNameSlot(app, model)].nameEn, 0x32, text);

    // frame rows mirror the English names (repeat of the collector loops)
    MirrorFacialFrameNames(model);
    MirrorBoneFrameNames(model);

    RebuildTimelineTrackPanel(app);     // 0x42F1E0
    PostLoadInit(app->SelectedModel()); // 0x49C850
    InvalidateSelectionReadouts(app);   // 0x40D070
}

// ===========================================================================
// 0x0045F050 - English-name combo 669 refresh (original: sub_45F050)
// ===========================================================================
// Bone-combo change: commits the bone-name edit (collector idx 1), stores
// the new combo selection as cursor 0, clears the model's bone selection
// flags and marks the bone selected - except for bones whose type byte is
// 6, 7 or 9 (special/invisible bones), which leave the model without a
// selected bone (selectedBone = -1).  Panel sweep, then the bone-name edit
// is refreshed from bones[cursor].nameEn and focused.
// =========================================================================//
void SelectEnglishNameBone(HWND hDlg) { // VA 0x0045F050
    MMDApp* app = g_Block;
    mdl::ModelRecord* model = mdl::Mdl(app->SelectedModel());

    CollectEnglishNameEdit(app, GetDlgItem(hDlg, kEditBoneNameEn), 1); // 0x43BED0

    const int cursor =
        static_cast<int>(SendMessageA(GetDlgItem(hDlg, kComboBoneName), CB_GETCURSEL, 0, 0));
    app->EnglishNameComboCursor(0) = cursor;

    unsigned char* selection = model->boneSelection;
    for (std::uint32_t i = 0; i < model->boneCount; ++i)
        selection[i] = 0;

    const mdl::BoneType kind = model->boneTable[cursor].type;
    if (kind == mdl::BoneType::Effector || kind == mdl::BoneType::InertTip ||
        kind == mdl::BoneType::CoRotate) {
        model->selectedBone = -1;
    } else {
        selection[cursor] = 1;
        model->selectedBone = cursor;
    }
    RebuildTimelineTrackPanel(app); // 0x42F1E0

    char buf[256];
    strcpy_s(buf, 0x100, model->boneTable[cursor].nameEn);
    RefreshNameEdit(GetDlgItem(hDlg, kEditBoneNameEn), buf);
}

// ===========================================================================
// 0x0045EF10 - English-name combo 673 refresh (original: sub_45EF10)
// ===========================================================================
// Morph-combo change, effective only while the model has facial frame
// groups: commits the morph-name edit (collector idx 2 - which already runs
// the panel sweep and PostLoadInit), stores the new selection as cursor 1
// and refreshes the morph-name edit from morphs[cursor+1].nameEn.
// =========================================================================//
void SelectEnglishNameMorph(HWND hDlg) { // VA 0x0045EF10
    MMDApp* app = g_Block;
    mdl::ModelRecord* model = mdl::Mdl(app->SelectedModel());
    if (model->facialFrameCount == 0)
        return;

    CollectEnglishNameEdit(app, GetDlgItem(hDlg, kEditMorphNameEn), 2); // 0x43BED0

    app->EnglishNameComboCursor(1) =
        static_cast<int>(SendMessageA(GetDlgItem(hDlg, kComboMorphName), CB_GETCURSEL, 0, 0));
    RebuildTimelineTrackPanel(app); // 0x42F1E0

    char buf[256];
    strcpy_s(buf, 0x100, model->morphs[MorphNameSlot(app)].nameEn);
    RefreshNameEdit(GetDlgItem(hDlg, kEditMorphNameEn), buf);
}

// ===========================================================================
// 0x0045EDC0 - English-name combo 677 refresh (original: sub_45EDC0)
// ===========================================================================
// Group-combo change: first commits the current edit text into the group
// slot named by the OLD cursor 2 (slot +3/+2, capped at 49 chars), then
// stores the new selection as cursor 2, runs the panel sweep and refreshes
// the group-name edit from the newly selected group slot.
// =========================================================================//
void SelectEnglishNameGroup(HWND hDlg) { // VA 0x0045EDC0
    MMDApp* app = g_Block;
    mdl::ModelRecord* model = mdl::Mdl(app->SelectedModel());

    // commit the edit at the old group slot (collector idx-3 commit)
    char text[256];
    GetWindowTextA(GetDlgItem(hDlg, kEditGroupNameEn), text, 256);
    if (std::strlen(text) >= 0x32)
        text[49] = '\0';
    mdl::DisplayGroup* groups = mdl::DisplayGroups(app->SelectedModel());
    strcpy_s(groups[GroupNameSlot(app, model)].nameEn, 0x32, text);

    app->EnglishNameComboCursor(2) =
        static_cast<int>(SendMessageA(GetDlgItem(hDlg, kComboGroupName), CB_GETCURSEL, 0, 0));
    RebuildTimelineTrackPanel(app); // 0x42F1E0

    char buf[256];
    strcpy_s(buf, 0x100, groups[GroupNameSlot(app, model)].nameEn);
    RefreshNameEdit(GetDlgItem(hDlg, kEditGroupNameEn), buf);
}

// VA 0x0045ECC0 - English-name dialog (259) edit subclass: WM_KEYDOWN +
// VK_RETURN sets the in-dialog flag (app+0xA0D6C) and dispatches to the
// edge collector (0x43BED0) with 0/1/2/3 for edits 0x29B/0x2A0/0x2A4/
// 0x2A8 (edit 0x2A0 additionally runs the language sweep 0x42F1E0);
// everything else goes to the saved original proc (app+0xA0B54).
LRESULT CALLBACK EnglishNameEditSubclassProc( // VA 0x0045ECC0
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MMDApp* app = g_Block;
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        app->state.messageSeen = 1;
        const HWND dlg = app->EnglishNameDialog();
        if (hwnd == GetDlgItem(dlg, panel::kModelNameEnEdit)) {
            CollectEnglishNameEdit(app, hwnd, 0); // 0x43BED0
            return 0;
        }
        if (hwnd == GetDlgItem(dlg, panel::kBoneNameEnEdit)) {
            CollectEnglishNameEdit(app, hwnd, 1);
            RebuildTimelineTrackPanel(app); // 0x42F1E0
            return 0;
        }
        if (hwnd == GetDlgItem(dlg, panel::kMorphNameEnEdit)) {
            CollectEnglishNameEdit(app, hwnd, 2);
            return 0;
        }
        if (hwnd == GetDlgItem(dlg, panel::kGroupNameEnEdit)) {
            CollectEnglishNameEdit(app, hwnd, 3);
            return 0;
        }
    }
    return CallWindowProcA(app->EnglishNameEditProc(), hwnd, msg, wParam, lParam);
}
} // namespace

// ===========================================================================
// 0x464BD0 - case 259 English-name dialog (0x29A EN / 0x299 JP), modeless at
// app+0xA0B50.  WM_INITDIALOG: subclass edits 667/672/676/680 with
// sub_45ECC0 (old proc of 667 saved at app+0xA0B54), sub_43C430 init.
// WM_COMMAND: sets app+0xA0B6C = 1.  id 1/2 close (sub_45F240 apply,
// DestroyWindow, app+0xA0B50 = 0, re-enable main-window combo 0x1B4; id 1
// additionally shows the "preserve edit result" box and sets app+0xA0B64).
// ids 0x29E/0x29F/0x2A2/0x2A3/0x2A6/679 are the prev/next buttons of the
// three combos 669/673/677 (CB_SETCURSEL wrap-around + refresh helper).
// CBN_SELCHANGE of 669/673/677 refreshes the matching combo helper.
// Reference: MikuMikuDance.exe sub_464BD0.
// ===========================================================================
INT_PTR CALLBACK EnglishNameDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                    LPARAM lParam) { // VA 0x00464BD0
    MMDApp* app = g_Block;
    const HWND hCtrl = reinterpret_cast<HWND>(lParam);
    if (msg == WM_INITDIALOG) {
        app->EnglishNameEditProc() = reinterpret_cast<WNDPROC>(
            GetWindowLongPtrA(GetDlgItem(hDlg, panel::kModelNameEnEdit), GWLP_WNDPROC));
        // Original x64 RVAs 0x8BBA0..0x8BC09 keep all four callback addresses pointer-sized.
        SetWindowLongPtrA(GetDlgItem(hDlg, panel::kModelNameEnEdit), GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(EnglishNameEditSubclassProc));
        SetWindowLongPtrA(GetDlgItem(hDlg, panel::kBoneNameEnEdit), GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(EnglishNameEditSubclassProc));
        SetWindowLongPtrA(GetDlgItem(hDlg, panel::kMorphNameEnEdit), GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(EnglishNameEditSubclassProc));
        SetWindowLongPtrA(GetDlgItem(hDlg, panel::kGroupNameEnEdit), GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(EnglishNameEditSubclassProc));
        InitEnglishNameDialog(hDlg); // 0x43C430
        return 0;
    }
    if (msg != WM_COMMAND) {
        return 0;
    }
    // dirty write: the English-name dialog deliberately invalidates the
    // cached time (original blob reuse of app+0xA0B6C == timeNowHigh)
    reinterpret_cast<std::int32_t&>(app->state.timeNowHigh) = 1;
    switch (LOWORD(wParam)) {
    case IDOK:
    case IDCANCEL:
        ApplyEnglishNameDialog(hDlg); // 0x45F240
        DestroyWindow(hDlg);
        app->EnglishNameDialog() = nullptr;
        EnableWindow(GetDlgItem(app->state.hwnd, panel::kMainComboModel), TRUE);
        if (LOWORD(wParam) == IDOK) {
            if (app->state.englishUI != 0) {
                MessageBoxA(app->state.hwnd,
                            "Please preserve the edit result as a new model by "
                            "'save enhanced model'.",
                            "enhance model", 0);
            } else {
                MessageBoxA(app->state.hwnd, kMsgEnhanceModelJp, kCaptionEnhanceModelJp, 0);
            }
            app->EnhancedModelDirty() = 1;
        }
        return 1;
    case 0x29E: { // combo 669 next button
        const int cur = app->EnglishNameComboCursor(0);
        const int count = static_cast<int>(
            SendMessageA(GetDlgItem(hDlg, panel::kBoneNameCombo), CB_GETCOUNT, 0, 0));
        SendMessageA(GetDlgItem(hDlg, panel::kBoneNameCombo), CB_SETCURSEL,
                     cur + 1 >= count ? 0 : cur + 1, 0);
        SelectEnglishNameBone(hDlg); // 0x45F050
        return 0;
    }
    case 0x29F: { // combo 669 prev button
        int sel = app->EnglishNameComboCursor(0) - 1;
        if (sel < 0) {
            sel = static_cast<int>(
                      SendMessageA(GetDlgItem(hDlg, panel::kBoneNameCombo), CB_GETCOUNT, 0, 0)) -
                  1;
        }
        SendMessageA(GetDlgItem(hDlg, panel::kBoneNameCombo), CB_SETCURSEL, sel, 0);
        SelectEnglishNameBone(hDlg); // 0x45F050 (LABEL_16)
        return 0;
    }
    case 0x2A2: { // combo 673 next button
        const int cur = app->EnglishNameComboCursor(1);
        const int count = static_cast<int>(
            SendMessageA(GetDlgItem(hDlg, panel::kMorphNameCombo), CB_GETCOUNT, 0, 0));
        SendMessageA(GetDlgItem(hDlg, panel::kMorphNameCombo), CB_SETCURSEL,
                     cur + 1 >= count ? 0 : cur + 1, 0);
        SelectEnglishNameMorph(hDlg); // 0x45EF10
        return 0;
    }
    case 0x2A3: { // combo 673 prev button
        int sel = app->EnglishNameComboCursor(1) - 1;
        if (sel < 0) {
            sel = static_cast<int>(
                      SendMessageA(GetDlgItem(hDlg, panel::kMorphNameCombo), CB_GETCOUNT, 0, 0)) -
                  1;
        }
        SendMessageA(GetDlgItem(hDlg, panel::kMorphNameCombo), CB_SETCURSEL, sel, 0);
        SelectEnglishNameMorph(hDlg); // 0x45EF10 (LABEL_23)
        return 0;
    }
    case 0x2A6: { // combo 677 next button
        const int cur = app->EnglishNameComboCursor(2);
        const int count = static_cast<int>(
            SendMessageA(GetDlgItem(hDlg, panel::kGroupNameCombo), CB_GETCOUNT, 0, 0));
        SendMessageA(GetDlgItem(hDlg, panel::kGroupNameCombo), CB_SETCURSEL,
                     cur + 1 >= count ? 0 : cur + 1, 0);
        SelectEnglishNameGroup(hDlg); // 0x45EDC0
        return 0;
    }
    case 679: { // combo 677 prev button
        int sel = app->EnglishNameComboCursor(2) - 1;
        if (sel < 0) {
            sel = static_cast<int>(
                      SendMessageA(GetDlgItem(hDlg, panel::kGroupNameCombo), CB_GETCOUNT, 0, 0)) -
                  1;
        }
        SendMessageA(GetDlgItem(hDlg, panel::kGroupNameCombo), CB_SETCURSEL, sel, 0);
        SelectEnglishNameGroup(hDlg); // 0x45EDC0
        return 0;
    }
    default:
        break;
    }
    if (HIWORD(wParam) == 1 /*CBN_SELCHANGE*/) {
        if (hCtrl == GetDlgItem(hDlg, panel::kBoneNameCombo)) {
            SelectEnglishNameBone(hDlg); // 0x45F050 (LABEL_16)
            return 0;
        }
        if (hCtrl == GetDlgItem(hDlg, panel::kMorphNameCombo)) {
            SelectEnglishNameMorph(hDlg); // 0x45EF10 (LABEL_23)
            return 0;
        }
        if (hCtrl == GetDlgItem(hDlg, panel::kGroupNameCombo)) {
            SelectEnglishNameGroup(hDlg); // 0x45EDC0
        }
    }
    return 0;
}

} // namespace mikudancestudio
