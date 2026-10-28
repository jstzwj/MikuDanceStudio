// ===========================================================================
// VA 0x0049C850 - PostLoadInit  (original: sub_49C850, 3007 bytes)
// ===========================================================================
// Model selector panel refresh after a PMD/PMX load.  Rebuilds:
//   combo 443  bone list from the IK-chain array (m+9920, 24 B records -
//              record +0 is a bone index into the 604 B bone records;
//              chain[0]+18 picks radio 444/445);
//   BM_SETCHECK on 439 (m+11661 loadComplete), 441 (m+12734),
//              440 (m+14272);
//   four morph-category groups, category byte at morph+88 (morphs at
//   m+9924, 136 B records, count m+11648):
//       cat 1 -> combo 504 / slider 505 / edit 506 / selIdx m+11676
//       cat 2 -> combo 509 / slider 510 / edit 511 / selIdx m+11680
//       cat 3 -> combo 514 / slider 515 / edit 516 / selIdx m+11684
//       cat 4 -> combo 519 / slider 520 / edit 521 / selIdx m+11688
//     Each: rebuild combo from names (+20 English / +0 SJIS), set the
//     slider to (int)(morph[sel]+48 * 100.0) and "%5.4f" into the edit
//     ("0.0000" when sel < 0), then re-select; when sel < 0 the first
//     matching record index is stored back into the selIdx field.
//   combo 434  frame-registration list.  Models whose SJIS name equals
//              "ダミーボーン" (memcmp 13 bytes vs 0x5310A8, x64 0x5523E0)
//              get the fixed list ("All frame"/"disp/IK/OP"/"Sel Bone"/
//              bone01..bone15);
//              normal models get "All frame", center-bone name (index
//              m+14592), "disp/IK/OP", "Sel Bone", "Sel facial",
//              "All facial", facial-display groups (m+9948, 46 B stride,
//              byte count m+11692), "All bone", then disp frames
//              (m+9944, 46 B stride, count m+11696); JP labels are the
//              wide strings for all frames/display-IK-external parent/selected bones/selected morphs/
//              all morph frames/all bone frames sent via SendMessageW and the
//              SJIS format "ﾎﾞｰﾝ%02d" (0x531054).  Final
//              CB_SETCURSEL with m+314608.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/panel_controls.hpp"
#include <Windows.h>
#include <commctrl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

// JP wide labels (.rdata 0x531090/0x53107C/0x53106C/0x531048/0x531038/
// 0x531024) and the SJIS name/format constants (0x5310A8 / 0x531054).
const wchar_t* kJpAllFrame = L"\x5168\xFF8C\xFF9A\xFF70\xFF91"; // all frames
const wchar_t* kJpDispIkOp =
    L"\x8868\x793A\xFF65I\x004B\xFF65\x5916\x89AA";                  // display/IK/external parent
const wchar_t* kJpSelBone = L"\x9078\x629E\xFF8E\xFF9E\xFF70\xFF9D"; // selected bones
const wchar_t* kJpSelFacial = L"\x9078\x629E\x8868\x60C5";           // selected morphs
const wchar_t* kJpAllFacial = L"\x5168\x8868\x60C5\xFF8C\xFF9A\xFF70\xFF91";     // all morph frames
const wchar_t* kJpAllBone = L"\x5168\x30DC\x30FC\x30F3\xFF8C\xFF9A\xFF70\xFF91"; // all bone frames
const char kJpBoneFmt[] = "\xCE\xDE\xB0\xDD"
                          "%02d"; // ﾎﾞｰﾝ%02d
const char kDummyBoneName[13] = {char(0x83), char(0x5F), char(0x83), char(0x7E), char(0x81),
                                 char(0x5B), char(0x83), char(0x7B), char(0x81), char(0x5B),
                                 char(0x83), char(0x93), '\0'}; // ダミーボーン (0x5310A8 =
                                                                // x64 0x5523E0; the SJIS was
                                                                // previously misread as
                                                                // ミクボーン - 0x835F is ダ)

HWND Dlg(mdl::ModelRecord* model, int id) {
    return GetDlgItem(static_cast<HWND>(model->hwnd), id);
}

// One morph-category group (see header comment).
void RefreshMorphGroup(mdl::ModelRecord* model, mdl::MorphPanel panel, int comboId, int sliderId,
                       int editId, std::size_t selector) {
    HWND main = static_cast<HWND>(model->hwnd);
    HWND combo = Dlg(model, comboId);
    SendMessage(combo, CB_RESETCONTENT, 0, 0);
    const int n = static_cast<int>(model->morphCount);
    mdl::MorphRecord* morphs = model->morphs;
    const bool useEnglishNames = model->physicsFlags != 0;
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            const mdl::MorphRecord& morph = morphs[i];
            if (morph.panel == panel)
                SendMessageA(combo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(useEnglishNames ? morph.nameEn : morph.name));
        }
    }
    if (morphs != nullptr) {
        const char* text;
        char buf[256];
        const std::int32_t sel = model->selectedMorphs[selector];
        if (sel < 0) {
            SendMessage(Dlg(model, sliderId), TBM_SETPOS, TRUE, 0);
            text = "0.0000";
        } else {
            const float val = morphs[sel].value;
            SendMessage(Dlg(model, sliderId), TBM_SETPOS, TRUE, static_cast<LPARAM>(val * 100.0f));
            sprintf_s(buf, 0x100, "%5.4f", val);
            text = buf;
        }
        SetWindowTextA(Dlg(model, editId), text);
    }
    if (model->selectedMorphs[selector] != -1) {
        const std::int32_t sel = model->selectedMorphs[selector];
        if (n > 0) {
            int catIdx = 0;
            for (int i = 0; i < n; ++i) {
                if (morphs[i].panel == panel) {
                    if (i == sel) {
                        SendMessageA(combo, CB_SETCURSEL, catIdx, 0);
                        return;
                    }
                    ++catIdx;
                }
            }
        }
        return;
    }
    SendMessageA(combo, CB_SETCURSEL, 0, 0);
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            if (morphs[i].panel == panel) {
                model->selectedMorphs[selector] = i;
                return;
            }
        }
    }
}

} // namespace

void PostLoadInit(mdl::ModelRecord* model) {
    HWND main = static_cast<HWND>(model->hwnd);

    // ---- bone combo 443 (from IK chains) ----------------------------------
    HWND boneCombo = Dlg(model, panel::kIkChainCombo);
    SendMessage(boneCombo, CB_RESETCONTENT, 0, 0);
    mikudancestudio::mdl::BoneRecord* bones = model->boneTable;
    const bool useEnglishNames = model->physicsFlags != 0;
    const int chainCount = static_cast<int>(model->ikChainCount);
    if (chainCount > 0) {
        for (int i = 0; i < chainCount; ++i) {
            const std::int32_t boneIdx = model->ikChains[i].boneIndex;
            SendMessageA(boneCombo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(useEnglishNames ? bones[boneIdx].nameEn
                                                                  : bones[boneIdx].name));
        }
    }
    if (model->ikChains != nullptr) {
        SendMessageA(boneCombo, CB_SETCURSEL, 0, 0);
        CheckRadioButton(main, panel::kIkOnRadio, panel::kIkOffRadio,
                         model->ikChains[0].enabled != 0 ? panel::kIkOnRadio : panel::kIkOffRadio);
    }

    // ---- three checkboxes ---------------------------------------------------
    SendMessage(Dlg(model, panel::kModelVisibleCheckbox), BM_SETCHECK,
                model->loadComplete != 0 ? 1 : 0, 0);
    SendMessage(Dlg(model, panel::kAddBlendCheckbox), BM_SETCHECK,
                model->postLoadFlag2 != 0 ? 1 : 0, 0);
    SendMessage(Dlg(model, panel::kShadowCheckbox), BM_SETCHECK, model->toonFlag != 0 ? 1 : 0, 0);

    // ---- four morph groups --------------------------------------------------
    RefreshMorphGroup(model, mdl::MorphPanel::eyebrow, panel::kMorphCombo0, panel::kMorphSlider0,
                      panel::kMorphEdit0, 0);
    RefreshMorphGroup(model, mdl::MorphPanel::eye, panel::kMorphCombo1, panel::kMorphSlider1,
                      panel::kMorphEdit1, 1);
    RefreshMorphGroup(model, mdl::MorphPanel::mouth, panel::kMorphCombo2, panel::kMorphSlider2,
                      panel::kMorphEdit2, 2);
    RefreshMorphGroup(model, mdl::MorphPanel::other, panel::kMorphCombo3, panel::kMorphSlider3,
                      panel::kMorphEdit3, 3);

    // ---- frame-registration combo 434 ---------------------------------------
    HWND frameCombo = Dlg(model, panel::kRegisterScopeCombo);
    SendMessage(frameCombo, CB_RESETCONTENT, 0, 0);
    char buf[256];
    if (std::memcmp(model->name, kDummyBoneName, 13) == 0) {
        // bundled "ダミーボーン" dummy-bone model: fixed registration list
        if (useEnglishNames) {
            SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("All frame"));
            SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("disp/IK/OP"));
            SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Sel Bone"));
        } else {
            SendMessageW(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kJpAllFrame));
            SendMessageW(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kJpDispIkOp));
            SendMessageW(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kJpSelBone));
        }
        for (int i = 1; i <= 15; ++i) {
            if (useEnglishNames)
                sprintf_s(buf, 0x100, "bone%02d", i);
            else
                sprintf_s(buf, 0x100, kJpBoneFmt, i);
            SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
        }
    } else if (useEnglishNames) {
        SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("All frame"));
        SendMessageA(frameCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(bones[model->displayRootBone].nameEn));
        SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("disp/IK/OP"));
        SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Sel Bone"));
        SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Sel facial"));
        SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("All facial"));
        const auto* groups = model->displayFrames;
        for (std::uint8_t g = 0; g < model->facialFrameCount; ++g)
            SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(groups[g].nameEn));
        SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("All bone"));
        const auto* frames = model->displayBones;
        const std::int32_t frameCount = model->displayBoneCount;
        for (int f = 0; f < frameCount; ++f)
            SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(frames[f].nameEn));
    } else {
        SendMessageW(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kJpAllFrame));
        SendMessageA(frameCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(bones[model->displayRootBone].name));
        SendMessageW(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kJpDispIkOp));
        SendMessageW(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kJpSelBone));
        SendMessageW(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kJpSelFacial));
        SendMessageW(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kJpAllFacial));
        const auto* groups = model->displayFrames;
        for (std::uint8_t g = 0; g < model->facialFrameCount; ++g)
            SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(groups[g].name));
        SendMessageW(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kJpAllBone));
        const auto* frames = model->displayBones;
        const std::int32_t frameCount = model->displayBoneCount;
        for (int f = 0; f < frameCount; ++f)
            SendMessageA(frameCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(frames[f].name));
    }
    SendMessageA(frameCombo, CB_SETCURSEL, model->frameRegistrationSelection, 0);
}

} // namespace mikudancestudio
