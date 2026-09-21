// ===========================================================================
// Select-dialog family (original sub_47A3F0 SelectNavDlgProc helpers)
// ===========================================================================
// VA 0x00466630  InitSelectNavDialog(app, hDlg)  - WM_INITDIALOG list fill
// VA 0x00461C20  SyncSelectAttachCombo(app, hDlg)  - selection refresh (walks combo 669)
// VA 0x004256C0  ResetModelSelection(app)        - "all select / reset" apply (id 632)
// VA 0x0043D2E0  RebuildTargetBoneList(app, hDlg, keep) - bone list rebuild (combo 677)
// VA 0x0043D560  CommitTargetBonePick(app, hDlg)  - commit bone pick (combo 677 -> rec[4])
// VA 0x0043D610  ApplyBoneAttach(app, hDlg)  - apply selection to the model (id 630)
//
// All six are __thiscall in the original with this = Block (g_Block); the
// caller sub_47A3F0 loads ECX from the Block global before each call
// (0x47A462/0x47A4C4/0x47A4DF/0x47A4FD/0x47A536/0x47A556/0x47A57C/0x47A594).
// The former stub signatures in dialog_procs.cpp (HWND-only) were therefore
// missing the MMDApp* first parameter and have been corrected here.
//
// Dialog controls (SelectNavDlg template):
//   669 = bone combo (root entry 0 + bone-list entries from model+0x4CCE4)
//   673 = attach combo (0 = root, 1 = ground, 2.. = models by display order)
//   677 = target bone combo (per-model selectable-bone list)
//   436 = main-window model combo (used only for its item count)
//
// Working state:
//   app+0xA0668 -> array of 20-byte mdl::BoneOrderEntry values, one per
//     combo-669 item.
//   app+0xA0B1C -> int[] mapping combo-673 item-2 to model slot
//   app+0xA0B24 -> int[] mapping combo-677 item to bone id
//   app+0xA0664 = "select state changed" byte set after each apply
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// ---- display refresh --------------------------------------------------------
// VA 0x004250C0 - select-dialog value display refresh (thiscall app, hDlg);
// full port at the bottom of this file (local matrix + euler decomposition
// into edits 688..693).
void RefreshSelectNavDisplay(MMDApp* app, HWND hDlg);                // VA 0x004250C0

// ---- intra-family forward declarations (mutual recursion) ------------------
void SyncSelectAttachCombo(MMDApp* app, HWND hDlg);                // VA 0x00461C20
void RebuildTargetBoneList(MMDApp* app, HWND hDlg, int keepSelection); // VA 0x0043D2E0

namespace {

// ---- app offsets, kept file-local -------------------------------------------

// ---- model-object access ---------------------------------------------------
// All model state goes through the typed mdl::Mdl() view; the former raw
// offsets were x86-only and read garbage on the x64 baseline:
//   0x26E0/0x26E4/0x26E8 were never separate selection buffers - they are
//     boneKeys/morphKeys/displayKeys (x64 0x2790/0x2798/0x27A0), whose
//     per-record "allocated" byte doubles as the selection flag;
//   0x2248/0x227A are name/nameEn (x64 0x22C0/0x22F2);
//   0x2D7D is the display-order byte comboSelIndex2 (x64 0x3109);
//   0x4CCE4/0x4CCE8 are boneOrderTable/boneOrderCount (x64 0x96470/0x96478),
//     the 20-byte entries the dialog reads as attach records.

// ---- .rdata literals --------------------------------------------------------
// JP 0x52D354 "ルート" (root)
const char kJpRoot[] = "\x83\x8b\x81\x5b\x83\x67";
// JP 0x52E7DC "なし" (none) - combo 673 entry 0
const char kJpNone[] = "\x82\xc8\x82\xb5";
// JP 0x52E7D4 "地面" (ground) - combo 673 entry 1
const char kJpGround[] = "\x92\x6e\x96\xca";

unsigned char* ActiveModel(MMDApp* app) {
    return app->SelectedModel();
}

unsigned char* ModelAt(MMDApp* app, int slot) {
    return app->ModelSlot(slot);
}

bool English(MMDApp* app) {
    return app->state.englishUI != 0;  // 0xA0B4C / x64 0xA1B90
}

// The dialog and model use the same external-parent selector type.
mdl::BoneOrderEntry* ModelSelectRecords(unsigned char* model) {
    return mdl::BoneOrder(model);
}

mdl::BoneOrderEntry* SelRecord(MMDApp* app, int item) {
    return app->state.selectNavRecords + item;
}

LPARAM StrParam(const void* s) {
    return reinterpret_cast<LPARAM>(const_cast<void*>(s));
}

}  // namespace

// ===========================================================================
// VA 0x00466630 - InitSelectNavDialog: fill the select dialog on WM_INITDIALOG
// ===========================================================================
void InitSelectNavDialog(MMDApp* app, HWND hDlg) {  // VA 0x00466630
    app->AccessoryApplyGate() = 0;                     // 0x46665D
    if (app->state.selectNavRecords != nullptr) {      // 0x466664
        delete[] app->state.selectNavRecords;
        app->state.selectNavRecords = nullptr;
    }

    unsigned char* model = ActiveModel(app);
    const int count = static_cast<int>(
        mdl::Mdl(model)->boneOrderCount);              // x86 0x4CCE8 / x64 0x96478
    auto* const records = mdl::CopyBoneBindings(
        ModelSelectRecords(model), static_cast<std::size_t>(count));
    app->state.selectNavRecords = records;

    const HWND combo669 = GetDlgItem(hDlg, panel::kBoneNameCombo);                    // 0x46671F
    const HWND combo673 = GetDlgItem(hDlg, panel::kMorphNameCombo);                    // 0x466733
    SendMessageA(combo669, CB_RESETCONTENT, 0, 0);        // 0x466737
    SendMessageA(combo673, CB_RESETCONTENT, 0, 0);                            // 0x466747
    SendMessageA(combo669, CB_ADDSTRING, 0,               // 0x46675E
                 English(app) ? StrParam("root")          // 0x52E7E4
                              : StrParam(kJpRoot));      // 0x52D354

    int selItem = 0;                                                 // 0x46676B
    int item = 1;                                                    // 0x466773
    if (count > 1) {
        mdl::BoneRecord* const bones = mdl::Bones(model);
        do {
            const int boneIdx = records[item].boneIndex;             // 0x46679D
            const char* name =
                English(app) ? bones[boneIdx].nameEn : bones[boneIdx].name;
            SendMessageA(combo669, CB_ADDSTRING, 0, StrParam(name));        // 0x4667C1
            if (mdl::Mdl(model)->selectedBone == boneIdx)  // 0x4667F1
                selItem = item;
            ++item;
        } while (item < count);
    }
    const LRESULT cnt669 =
        SendMessageA(combo669, CB_GETCOUNT, 0, 0);         // 0x466819
    if (selItem <= 0)
        SendMessageA(combo669, CB_SETCURSEL, cnt669 > 0, 0); // 0x466838
    else
        SendMessageA(combo669, CB_SETCURSEL, selItem, 0);                   // 0x466826

    if (English(app)) {                                              // 0x46683A
        SendMessageA(combo673, CB_ADDSTRING, 0, StrParam("non"));     // 0x52D38C (sic)
        SendMessageA(combo673, CB_ADDSTRING, 0, StrParam("ground"));   // 0x52D2A4
    } else {
        SendMessageA(combo673, CB_ADDSTRING, 0, StrParam(kJpNone));    // 0x52E7DC
        SendMessageA(combo673, CB_ADDSTRING, 0, StrParam(kJpGround));  // 0x52E7D4
    }

    // Fill combo 673 item 2.. with the loaded models in display order,
    // mirroring the main-window model combo (item count - 1).
    const HWND mainCombo = GetDlgItem(
        static_cast<HWND>(app->state.hwnd), panel::kMainComboModel); // 0x4668A0
    const int modelCount =
        static_cast<int>(SendMessageA(mainCombo, CB_GETCOUNT, 0, 0)) - 1;  // 0x4668B1
    if (app->DialogOrders().modelIndices != nullptr) {                    // 0x4668AB
        app->DialogOrders().modelIndices.reset();
    }
    app->DialogOrders().modelIndices.reset(
        new std::int32_t[static_cast<std::size_t>(modelCount)]);
    std::int32_t* const order = app->DialogOrders().modelIndices.get();

    char Buffer[256];                                                // 0x466906
    int nOrder = 0;
    // x64 twin sub_7FF7CB4BA7B0: order and slot walks both run to 255
    // (cmp esi, 0FFh at 0x7FF7CB4BABB2 / cmp edi, 0FFh at 0x7FF7CB4BAB0D).
    for (int ord = 0; ord < kModelSlotCount; ++ord) {                // 0x4669DE
        int slot = 0;
        unsigned char* m = nullptr;
        for (; slot < kModelSlotCount; ++slot) {                     // 0x466911
            unsigned char* cand = ModelAt(app, slot);
            if (cand != nullptr &&
                mdl::Mdl(cand)->comboSelIndex2 ==
                    static_cast<unsigned char>(ord)) {  // x86 0x2D7D / x64 0x3109
                m = cand;
                break;
            }
        }
        if (m == nullptr)
            continue;                                                // LABEL_40
        const mdl::ModelRecord* const mrec = mdl::Mdl(m);
        const char* name =
            English(app) ? mrec->nameEn : mrec->name;  // x64 0x22F2 / 0x22C0
        if (slot == app->SelectedModelSlot()) {                      // 0x46692A
            sprintf_s(Buffer, 0x100, "(%s)", name);                  // 0x466951
            SendMessageA(combo673, CB_ADDSTRING, 0, StrParam(Buffer));
        } else {
            SendMessageA(combo673, CB_ADDSTRING, 0, StrParam(name));
        }
        order[nOrder++] = slot;                                      // 0x4669CA
    }

    SyncSelectAttachCombo(app, hDlg);                                            // 0x4669F0
}

// ===========================================================================
// VA 0x00461C20 - SyncSelectAttachCombo: sync combo 673 with the combo-669 record
// ===========================================================================
void SyncSelectAttachCombo(MMDApp* app, HWND hDlg) {  // VA 0x00461C20
    const HWND combo673 = GetDlgItem(hDlg, panel::kMorphNameCombo);                     // 0x461C47
    const HWND combo669 = GetDlgItem(hDlg, panel::kBoneNameCombo);                     // 0x461C49
    const int attach = SelRecord(
        app,
        static_cast<int>(SendMessageA(combo669, CB_GETCURSEL, 0, 0)))
        ->linkedModel;

    if (attach == -1) {                                              // 0x461C66
        SendMessageA(combo673, CB_SETCURSEL, 0, 0);
    } else if (attach == -2) {                                       // 0x461C6F
        SendMessageA(combo673, CB_SETCURSEL, 1, 0);
    } else {
        const int n = static_cast<int>(
                         SendMessageA(combo673, CB_GETCOUNT, 0, 0)) - 2;
        if (n > 0) {                                                 // 0x461C86
            const int* order = app->DialogOrders().modelIndices.get();
            int idx = 0;
            while (attach != order[idx]) {
                ++idx;
                if (idx >= n) {
                    RebuildTargetBoneList(app, hDlg, 1);                         // 0x461C9C
                    return;
                }
            }
            SendMessageA(combo673, CB_SETCURSEL, idx + 2, 0);               // 0x461CBF
        }
    }
    RebuildTargetBoneList(app, hDlg, 1);                                         // 0x461CAC
}

// ===========================================================================
// VA 0x0043D2E0 - RebuildTargetBoneList: rebuild the combo-677 bone list for the
// attach target selected in combo 673
// ===========================================================================
void RebuildTargetBoneList(MMDApp* app, HWND hDlg, int keepSelection) {  // VA 0x0043D2E0
    const HWND combo677 = GetDlgItem(hDlg, panel::kGroupNameCombo);                     // 0x43D30A
    const int boneSel = static_cast<int>(SendMessageA(
        GetDlgItem(hDlg, panel::kBoneNameCombo), CB_GETCURSEL, 0, 0));       // 0x43D320
    const int attach = static_cast<int>(SendMessageA(
        GetDlgItem(hDlg, panel::kMorphNameCombo), CB_GETCURSEL, 0, 0));                        // 0x43D333
    mdl::BoneOrderEntry* const rec = SelRecord(app, boneSel);

    if (attach == 0 || attach == 1) {                                // 0x43D348
        rec->linkedModel = attach == 0 ? -1 : -2;                // 0x43D350 / 0x43D365
        // LABEL_5: empty target-bone list                          // 0x43D36D
        rec->linkedBone = 0;
        SendMessageA(combo677, CB_RESETCONTENT, 0, 0);
        SendMessageA(combo677, CB_ADDSTRING, 0, StrParam("------"));        // 0x52C0E8
        SendMessageA(combo677, CB_SETCURSEL, 0, 0);
        RefreshSelectNavDisplay(app, hDlg);                                        // 0x43D394
        return;
    }

    const int modelSlot = app->DialogOrders().modelIndices.get()[attach - 2];
    rec->linkedModel = modelSlot;                                // 0x43D3B1
    SendMessageA(combo677, CB_RESETCONTENT, 0, 0);                             // 0x43D3B5

    unsigned char* const model = ModelAt(app, modelSlot);
    mdl::BoneRecord* const bones = mdl::Bones(model);
    const int boneCount =
        static_cast<int>(mdl::Mdl(model)->boneCount);                 // 0x2D84

    int nSelectable = 0;                                             // 0x43D3CA
    for (int i = 0; i < boneCount; ++i) {                            // 0x43D3E8
        const mdl::BoneType type = bones[i].type;
        if (type < mdl::BoneType::InertTip ||
            type == mdl::BoneType::FixedAxis)
            ++nSelectable;
    }

    if (app->AccessoryEditArray() != nullptr) {                     // 0x43D40A
        free(app->AccessoryEditArray());
        app->AccessoryEditArray() = nullptr;
    }
    int* const boneIds = static_cast<int*>(
        operator new(static_cast<std::size_t>(4 * nSelectable)));    // 0x43D441
    app->AccessoryEditArray() = boneIds;

    int n = 0;                                                       // 0x43D454
    for (int i = 0; i < boneCount; ++i) {                            // 0x43D4EA
        const mdl::BoneType type = bones[i].type;
        if (type < mdl::BoneType::InertTip ||
            type == mdl::BoneType::FixedAxis) {
            const char* name = English(app) ? bones[i].nameEn : bones[i].name;
            SendMessageA(combo677, CB_ADDSTRING, 0, StrParam(name));        // 0x43D4AC
            boneIds[n++] = i;                                        // 0x43D4BC
        }
    }

    if (keepSelection) {                                             // 0x43D4F7
        if (n > 0) {
            int idx = 0;
            while (rec->linkedBone != boneIds[idx]) {
                ++idx;
                if (idx >= n) {
                    RefreshSelectNavDisplay(app, hDlg);                            // 0x43D51F
                    return;
                }
            }
            SendMessageA(combo677, CB_SETCURSEL, idx, 0);                   // 0x43D52C
        }
    } else {
        SendMessageA(combo677, CB_SETCURSEL, 0, 0);                         // 0x43D538
        rec->linkedBone = 0;                                    // 0x43D544
    }
    RefreshSelectNavDisplay(app, hDlg);                                            // 0x43D554
}

// ===========================================================================
// VA 0x0043D560 - CommitTargetBonePick: store the combo-677 pick into rec[4]
// ===========================================================================
void CommitTargetBonePick(MMDApp* app, HWND hDlg) {  // VA 0x0043D560
    const int boneSel = static_cast<int>(SendMessageA(
        GetDlgItem(hDlg, panel::kBoneNameCombo), CB_GETCURSEL, 0, 0));       // 0x43D597
    const int attach = static_cast<int>(SendMessageA(
        GetDlgItem(hDlg, panel::kMorphNameCombo), CB_GETCURSEL, 0, 0));                        // 0x43D5A2
    mdl::BoneOrderEntry* const rec = SelRecord(app, boneSel);
    if (attach >= 2) {                                               // 0x43D5A5
        const int pick = static_cast<int>(SendMessageA(
            GetDlgItem(hDlg, panel::kGroupNameCombo), CB_GETCURSEL, 0, 0));                    // 0x43D5DF
        rec->linkedBone =
            static_cast<int*>(app->AccessoryEditArray())[pick];
    } else {
        rec->linkedBone = 0;                                    // 0x43D5B0
    }
    RefreshSelectNavDisplay(app, hDlg);                                            // 0x43D5C0
}

// ===========================================================================
// VA 0x004256C0 - ResetModelSelection: clear all per-vertex/per-morph selection state
// and restore the select records (button id 632)
// ===========================================================================
void ResetModelSelection(MMDApp* app) {  // VA 0x004256C0
    app->SceneModified() = 1;                                       // 0x4256C5

    unsigned char* const model = ActiveModel(app);

    // The "selection tracks" are the timeline key pools themselves: the
    // x64 twin sub_7FF7CB4BBA80 walks boneKeys (+0x2790) 100000 groups of
    // stride 360 clearing +0x38 per 60-byte record, morphKeys (+0x2798)
    // 4000 groups of stride 100 clearing +0x10 per 20-byte record, and
    // displayKeys (+0x27A0) 200 groups of stride 200 clearing +0x18 per
    // 40-byte record - i.e. the full pool capacities, one flag byte per
    // record (the x86 original 0x4256CC only covered the smaller x86 pools:
    // 50000x6 bone / 4000x5 morph / 200x5 display keys).  The flag byte is
    // the record's "allocated" slot, the same one ui_editor_click.cpp's
    // ClearActiveModelFlags sweeps, so follow the capacity constants
    // instead of hard-coding group counts.
    mdl::BoneKey* const boneKeys = mdl::BoneKeys(model);
    for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i)          // 0x4256CC
        boneKeys[i].allocated = 0;
    mdl::MorphKey* const morphKeys = mdl::MorphKeys(model);
    for (std::size_t j = 0; j < mdl::kMorphKeyCapacity; ++j)         // 0x42577C
        morphKeys[j].allocated = 0;
    mdl::DisplayKey* const displayKeys = mdl::DisplayKeys(model);
    for (std::size_t k = 0; k < mdl::kDisplayKeyCapacity; ++k)       // 0x425806
        displayKeys[k].allocated = 0;

    // restore the saved 20-byte select records into the model
    const int count = static_cast<int>(
        mdl::Mdl(model)->boneOrderCount);                            // 0x4258B2 / x64 0x96478
    if (count > 0) {
        std::copy_n(app->state.selectNavRecords, count,
                    ModelSelectRecords(model));
    }

    RegisterDisplayKeyCurrent(model, app->state.currentFrame);    // 0x42592E
    const auto registeredFrames = mdl::Mdl(model)->maxFrame;         // 0x31B0
    if (app->LastRegisteredFrame() < registeredFrames)                // 0x42594E
        app->LastRegisteredFrame() = registeredFrames;

    PanelPaint(app);                                                 // 0x414610
    app->AccessoryApplyGate() = 1;                         // 0x42595D
}

// ===========================================================================
// VA 0x0043D610 - ApplyBoneAttach: apply the selected attach to the current bone
// (button id 630) - matrix bake + selection flag + refresh
// ===========================================================================
void ApplyBoneAttach(MMDApp* app, HWND hDlg) {  // VA 0x0043D610
    const int boneSel = static_cast<int>(SendMessageA(
        GetDlgItem(hDlg, panel::kBoneNameCombo), CB_GETCURSEL, 0, 0));       // 0x43D650
    const int attach = static_cast<int>(SendMessageA(
        GetDlgItem(hDlg, panel::kMorphNameCombo), CB_GETCURSEL, 0, 0));                        // 0x43D655
    const mdl::BoneOrderEntry* const rec = SelRecord(app, boneSel);

    unsigned char* const model = ActiveModel(app);
    mdl::BoneRecord* const bones = mdl::Bones(model);                // 0x26BC
    mdl::BoneRecord& bone = bones[rec->boneIndex];

    auto* d3dx = &d3dx::Get();
    {                                              // D3DX imports
        d3dx::D3DXMATRIXF X = {};                                    // var_90 matrix
        d3dx::D3DXMATRIXF T = {};                                    // var_40 matrix
        if (attach == 0) {
            // root: relative to the parent bone
            const int parent = bone.parent;                           // 0x43D69B
            if (parent < 0) {
                X = *reinterpret_cast<d3dx::D3DXMATRIXF*>(
                    bone.matInit);                                   // 0x43D6F8
            } else {
                d3dx->inverse(
                    &T, nullptr, reinterpret_cast<d3dx::D3DXMATRIXF*>(
                                     bones[parent].matInit));         // 0x43D6BE
                d3dx->multiply(
                    &X, reinterpret_cast<d3dx::D3DXMATRIXF*>(bone.matInit),
                    &T);                                             // 0x43D884
            }
        } else if (attach == 1) {
            // ground: the bone's own local matrix
            X = *reinterpret_cast<d3dx::D3DXMATRIXF*>(bone.matInit); // 0x43D734
        } else {
            // attach to a bone of another model: bake the target bone's
            // world placement relative to the current bone's position
            unsigned char* const tmodel =
                ModelAt(app, rec->linkedModel);                  // 0x43D73B
            mdl::BoneRecord& target =
                mdl::Bones(tmodel)[rec->linkedBone];
            const float* const tpos = target.position;
            X = *reinterpret_cast<d3dx::D3DXMATRIXF*>(
                target.matExtra);                                    // 0x43D764
            d3dx->translation(&T, tpos[0], tpos[1], tpos[2]);        // 0x43D7A9
            d3dx->multiply(&X, &T, &X);                              // 0x43D7BE
            const float* const cpos = bone.position;
            d3dx->translation(&T, -cpos[0], -cpos[1], -cpos[2]);     // 0x43D817
            d3dx->multiply(&X, &T, &X);                              // 0x43D82C
            d3dx->inverse(&X, nullptr, &X);                          // 0x43D83B
            T = *reinterpret_cast<d3dx::D3DXMATRIXF*>(bone.matInit); // 0x43D870
            d3dx->multiply(&X, &T, &X);                              // 0x43D884
        }

        // offset of the bone position through the composed matrix
        const float* const cpos2 = bone.position;                    // 0x43D8AC
        float vin[3] = {cpos2[0], cpos2[1], cpos2[2]};
        float vout[4];                                               // var_50
        d3dx->vec3Transform(vout, vin, &X);                          // 0x43D8FB
        bone.trans[0] = vout[0] - vin[0];                            // 0x43D933
        bone.trans[1] = vout[1] - vin[1];                            // 0x43D96D
        bone.trans[2] = vout[2] - vin[2];                            // 0x43D9AB

        // pure-rotation matrix -> bone quaternion
        X.m[3][0] = 0.0f;                                            // 0x43D9BB
        X.m[3][1] = 0.0f;                                            // 0x43D9BF
        X.m[3][2] = 0.0f;                                            // 0x43D9CA
        d3dx->quatFromMatrix(bone.rotQuat, &X);                      // 0x43D9EC
    }

    app->SceneModified() = 1;                                       // 0x43D9F8

    // Single-bone selection state.
    unsigned char* const selFlags = mdl::Mdl(model)->boneSelection;
    const int boneCount = static_cast<int>(mdl::Mdl(model)->boneCount);
    for (int i = 0; i < boneCount; ++i)                              // 0x43DA3F
        selFlags[i] = 0;
    selFlags[rec->boneIndex] = 1;                                    // 0x43DA5E

    RegisterSelectedBoneKeys(model, app->state.currentFrame,     // 0x43DA7E
              app->PlaybackPhysicsMode());
    const auto registeredFrames = mdl::Mdl(model)->maxFrame;         // 0x31B0
    if (app->LastRegisteredFrame() < registeredFrames)                // 0x43DA9D
        app->LastRegisteredFrame() = registeredFrames;

    PanelPaint(app);                                                 // 0x414610
    SelectionReeval(app);                                            // 0x430510
    app->AccessoryApplyGate() = 1;                         // 0x43DAB5
}

// ---------------------------------------------------------------------------
// VA 0x004250C0 - select-dialog value display refresh (thiscall app, hDlg;
// called from 0x43D394/0x43D51F/0x43D554/0x43D5C0).  Reads combo 669 (bone
// list entry, 0 = none) and combo 673 (attach kind: 0 root / 1 ground /
// >=2 attached-to-model), computes the bone's local matrix, decomposes it
// into position offset + euler angles and shows the six "%f" values in
// edits 688..693; no selection shows "------" x6 and disables button 630.
//
// Matrix assembly (all via d3dx9_32.dll):
//   kind 1 (ground): local = bone matrix (&bones[rec.bone].matInit[0])
//   kind 0 (root):   parent id at &bone->parent: <0 -> plain bone matrix, else
//                    boneMat * Inverse(parentBoneMat)
//   kind >=2:        T(srcBone.pos) * srcBoneMat, then
//                    T(-parentBone.pos) * that, Inverse, then parentBoneMat
//                    * result (cross-model attach chain through the record's
//                      target-model and target-bone fields)
// Euler extraction (x64 twin sub_7FF7CB4BB3E0 uses the single-precision
// intrinsics atan2f/asinf/cosf, plus the original's two-tier epsilon
// cleanups and the 3.141592025756836 degree constant):
//   pitch = asinf(-m[9]); yaw = atan2f(m[1], m[5]); roll = atan2f(m[8],
//   m[10]); gimbal patch when |cos(pitch)| < 1e-6 (±3.141592 on yaw/roll
//   by matrix signs).
// ---------------------------------------------------------------------------
void RefreshSelectNavDisplay(MMDApp* app, HWND hDlg) {  // VA 0x004250C0
    auto& s = *app;
    const int sel = static_cast<int>(SendMessageA(
        GetDlgItem(hDlg, panel::kBoneNameCombo), CB_GETCURSEL, 0, 0));             // 0x425111
    const int kind = static_cast<int>(SendMessageA(
        GetDlgItem(hDlg, panel::kMorphNameCombo), CB_GETCURSEL, 0, 0));             // 0x42511A
    if (sel == 0) {
        for (int i = 0; i < 6; ++i)                              // 0x42513D
            SetWindowTextA(GetDlgItem(hDlg, 688 + i), "------");
        EnableWindow(GetDlgItem(hDlg, panel::kOrderUpButton), FALSE);              // 0x42569A
        return;
    }

    unsigned char* model = s.SelectedModel();
    mikudancestudio::mdl::BoneRecord* bones =
        mikudancestudio::mdl::Bones(model);
    const mdl::BoneOrderEntry* const rec =
        SelRecord(app, sel);                             // 0xA0668 + 20*sel
    mikudancestudio::mdl::BoneRecord* bone = &bones[rec->boneIndex];

    auto& api = d3dx::Get();
    d3dx::D3DXMATRIXF m{}, t{}, t2{};
    if (kind == 1) {                                             // 0x42521A
        std::memcpy(&m, bone->matInit, sizeof m);
    } else if (kind >= 2) {                                      // 0x425286
        unsigned char* srcModel = s.ModelSlot(rec->linkedModel);
        mikudancestudio::mdl::BoneRecord* srcBones =
            mikudancestudio::mdl::Bones(srcModel);
        mikudancestudio::mdl::BoneRecord& src =
            srcBones[rec->linkedBone];
        std::memcpy(&m, &src.matInit[0], sizeof m);
        const float* const p = src.position;  // x64 0x13C (bone+316)
        api.translation(&t, p[0], p[1], p[2]);
        api.multiply(&m, &t, &m);
        api.translation(&t,
                        -bone->position[0],
                        -bone->position[1],
                        -bone->position[2]);
        api.multiply(&m, &t, &m);
        api.inverse(&m, nullptr, &m);
        std::memcpy(&t2, bone->matInit, sizeof t2);
        api.multiply(&m, &t2, &m);
    } else {                                                     // kind 0
        const std::int32_t parent = bone->parent;
        if (parent < 0) {                                        // 0x425192
            std::memcpy(&m, bone->matInit, sizeof m);
        } else {
            api.inverse(&t, nullptr,
                        reinterpret_cast<d3dx::D3DXMATRIXF*>(
                            &bones[parent].matInit[0]));
            api.multiply(&m,
                         reinterpret_cast<d3dx::D3DXMATRIXF*>(bone->matInit),
                         &t);
        }
    }

    // ---- offset + euler decomposition ------------------------------------
    const float* const pos = bone->position;
    float out[4]{};
    float world[3] = {pos[0], pos[1], pos[2]};
    api.vec3Transform(out, world, &m);                           // 0x42543B
    float values[6];
    values[0] = out[0] - pos[0];
    values[1] = out[1] - pos[1];
    values[2] = out[2] - pos[2];

    float& pitch = values[3];                                    // v54
    float& roll = values[4];                                     // v55
    float& yaw = values[5];                                      // v56
    // x64 twin sub_7FF7CB4BB3E0: single-precision atan2f/asinf/cosf
    // (0x7FF7CB4BB83C atan2f(m01,m11), 0x7FF7CB4BB84D asinf(-m21),
    // 0x7FF7CB4BB867 atan2f(m20,m22)), not the x86 single-argument atan.
    yaw = atan2f(m.m[0][1], m.m[1][1]);
    pitch = asinf(-m.m[2][1]);
    roll = atan2f(m.m[2][0], m.m[2][2]);
    const float c = cosf(pitch);
    if (fabsf(c) < 0.000001f) {                                  // gimbal lock
        yaw = static_cast<float>(
            static_cast<double>(yaw) +
            (static_cast<double>(m.m[0][1]) <= 0.0 ? -3.141592
                                                   : 3.141592));
        roll = static_cast<float>(
            static_cast<double>(roll) +
            (static_cast<double>(m.m[2][0]) <= 0.0 ? -3.141592
                                                   : 3.141592));
    }
    if (fabs(static_cast<double>(pitch)) < 0.000001) pitch = 0.0f;
    if (fabs(static_cast<double>(roll)) < 0.000001) roll = 0.0f;
    if (fabs(static_cast<double>(yaw)) < 0.000001) yaw = 0.0f;
    constexpr float kPiDeg = 3.141592025756836f;
    pitch = static_cast<float>(static_cast<double>(pitch) /
                               kPiDeg * 180.0);
    roll = static_cast<float>(-static_cast<double>(roll) /
                              kPiDeg * 180.0);
    yaw = static_cast<float>(180.0 *
                             (-static_cast<double>(yaw) / kPiDeg));
    const double eps = 0.0000001000000011686097;
    if (fabs(static_cast<double>(pitch)) < eps) pitch = 0.0f;
    if (fabs(static_cast<double>(roll)) < eps) roll = 0.0f;
    if (fabs(static_cast<double>(yaw)) < eps) yaw = 0.0f;

    for (int i = 0; i < 6; ++i) {                                // 0x42563A
        char text[0x100];
        sprintf_s(text, 0x100, "%f", values[i]);
        SetWindowTextA(GetDlgItem(hDlg, 688 + i), text);
    }
    EnableWindow(GetDlgItem(hDlg, panel::kOrderUpButton), TRUE);                   // 0x425685
}

}  // namespace mikudancestudio
