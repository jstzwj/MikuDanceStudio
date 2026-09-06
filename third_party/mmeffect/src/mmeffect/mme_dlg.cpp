// mme_dlg.cpp - the effect-assignment dialog, the WndProc port of
// FUN_180044080 and its helper cluster (FUN_180041620 selected-row
// collection, FUN_180041aa0 selection probes, FUN_180041b00 the effect-text
// writer, FUN_1800418b0 the per-object assignment writer, FUN_180042610 the
// checkbox/hide-show applier, FUN_180042a80 subset-extract toggle,
// FUN_1800424a0 reset-with-default, FUN_180042f60/FUN_1800431e0 EMM open/
// save, FUN_180043420/FUN_180043b00 EMD open/save, FUN_180043fd0 the selected
// effect-path probe, FUN_1800429a0 the subset-checkmark probe,
// FUN_18003fff0 the row rebuild + menu sync, FUN_18003fb70 the WH_GETMESSAGE
// keyboard router).
//
// Faithful behaviors (decompile-derived; audited 2026-08):
//   - The list keeps a parallel row table (the original DAT_1800d9ce8):
//     {object pointer (null = the "(default)" row), subset index (-1 = whole
//     object)}. The "(default)" row is always row 0 and carries NO checkbox
//     (the original sets its state image to 0 in FUN_18003fff0).
//   - FUN_180041620 excludes the "(default)" row from every selection
//     collection while the tab control has a live item rect (its *param_2
//     tab-rect gate). The port queries TCM_GETITEMRECT the same way.
//   - LVN_ITEMCHANGED (-101), guarded by DAT_1800d99db while the list is
//     being rebuilt: a state-image change runs the FUN_180042610 port (write
//     shown, latch DAT_1800d9f4b); a selection/focus change (mask 3) runs the
//     full menu/button sync.
//   - The sync (FUN_180041aa0 probes): 40006/40007/40012/40008 + buttons
//     1007/1008 <- "any selected normal-object row"; 40013 + button 1011 <-
//     "any selected row"; 40016 <- count(whole-object normal rows) != 0;
//     40017 <- count == 1; the 40006 checkmark <- FUN_1800429a0 (every
//     selected row expanded); 40014 <- FUN_180043fd0 (the FIRST selected row
//     carries an effect path; the "(default)" row disables it). The dialog
//     resource pre-marks the Edit items INACTIVE; the runtime MF_ENABLED
//     overrides the preset.
//   - NM_DBLCLK on a row runs the Set Effect flow (the original -3 handler;
//     the "(default)" row collects to an empty set -> no-op).
//   - Every edit enables the Apply button; Apply (1005) commits: refresh the
//     default-effect snapshot + EMM auto-save + the button disables itself
//     (the original 0x3ed case). Cancel (2) restores the default-effect
//     snapshot (FUN_180041550) and closes; OK (1) and 40011 close as-is.
//   - Subset-Extract (40006) is a TOGGLE over all selected whole-object
//     normal rows (FUN_180042a80): unchecked -> expand into per-material
//     rows ("  |-- name:" / "  `-- name:"), checked -> collapse. Expansion
//     survives the per-frame row rebuilds (the DAT_1800d9cc8 set).
//   - WM_SIZE moves the 10 controls through the DAT_1800d7788 anchor table
//     (caption/list/tab grow with the client; OK/Cancel/Apply stick to the
//     bottom-right; the 1006 group + buttons stick to the bottom), scales
//     the two list columns proportionally, invalidates the dialog, and saves
//     the window rect (DAT_1800d9f50) unless minimized/maximized; dialog
//     open restores it.
//   - NM_CUSTOMDRAW: row 0 paints with COLOR_GRAYTEXT on COLOR_BTNFACE; rows
//     with an empty effect text gray the Effect File column text
//     (COLOR_GRAYTEXT) - the 0x4e branch of FUN_180044080.
//   - WM_DROPFILES: exactly one file ("Two or more files cannot be
//     specified." otherwise); .emm loads the mapping, .fx/.fxm/.fxsub
//     validate + assign to the selection, anything else shows "Please
//     specify .fx file or .emm file." (the original accepts no .emd here).
//   - Tool menu: 40014 disassembles the selected effect into a text window;
//     40028 toggles [System] SkipValidation (GetMenuState flip + ini write,
//     the original 0x9c5c case).

#include "mme_dlg.h"

#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "effect_engine.h"
#include "emm_manager.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "mme_log.h"
#include "mme_ui.h"
#include "mme_util.h"
#include "model_data.h"

#include "mmhack_api.h"
#include "MMDExport.h"

namespace mme {

void MmeDlgCommitAssignment();   // [OK/Apply] the EMM auto-save commit (file tail)

namespace {

// Control IDs from the original template (dialog 108).
const int kDlgListView   = 1003;
const int kDlgTab        = 1002;
const int kDlgTabCaption = 1004;
const int kBtnSetFile    = 1007;   // 0x3ef
const int kBtnRemove     = 1008;   // 0x3f0
const int kBtnHideShow   = 1011;   // 0x3f3
const int kBtnGroup      = 1006;   // 0x3ee - the "Selected Items" group box
const int kBtnRefresh    = 1005;   // 0x3ed - Apply

const int kColObject = 0;
const int kColEffect = 1;

// One row of the parallel row table (the original DAT_1800d9ce8 entries:
// {object pointer, subset}). model == null marks the "(default)" row.
struct RowRef {
    ModelData* model;
    int subset;
};
std::vector<RowRef> g_rows;

// The expanded-object set (the original DAT_1800d9cc8); consulted by the
// row rebuild so per-material rows survive FUN_18003fff0 refreshes.
std::set<ModelData*> g_expanded;

HWND g_dlgWindow = nullptr;      // the modeless dialog handle (DAT_1800d9a48)
size_t g_lastModelCount = 0;     // the per-frame list-refresh bookkeeping
bool g_dirty = false;            // pending edits -> Apply enables
bool g_skipValidation = false;   // DAT_1800d99d9 ([System] SkipValidation)
bool g_clickLatch = false;       // DAT_1800d9f4b (swallow the post-checkbox click)
bool g_rebuilding = false;       // DAT_1800d99db (suppress LVN_ITEMCHANGED while rebuilding)
bool g_selectedObject = false;   // DAT_1800d9f48
bool g_selectedAny = false;      // DAT_1800d9f49
bool g_subsetCheck = false;      // DAT_1800d9f4a (the Subset-Extract checkmark)
std::string g_lastFxDir;         // DAT_1800da130 (the open-dialog initial dir)
std::string g_defaultSnapshot;   // DAT_1800d74d8 (the default effect at open/Apply)

// The WH_GETMESSAGE keyboard router state (DAT_1800d9a50 / DAT_1800d9a48 /
// DAT_1800d99dc).
HHOOK g_getMessageHook = nullptr;
HWND g_hookDialogHwnd = nullptr;
unsigned char g_sizingFlag = 0;  // DAT_1800d99dc (WM_ENTERSIZEMOVE/EXIT)

// DAT_1800d9f50..DAT_1800d9f5c - the saved window rect restored on open.
RECT g_savedWindowRect = { 0, 0, 0, 0 };
bool g_hasSavedWindowRect = false;

// DAT_1800da1a0/1a8 - the fractional column widths for WM_SIZE scaling.
double g_colFraction[2] = { 0.0, 0.0 };

HWND DlgList()
{
    return GetDlgItem(g_dlgWindow, kDlgListView);
}

// The client-anchored WM_SIZE table entry modes (DAT_1800d7788).
enum AnchorMode {
    kAnchorGrowBoth = 0,    // 1002/1003: right += dw, bottom += dh
    kAnchorGrowRight,       // 1004: right += dw
    kAnchorStickBottom,     // 1006/1007/1008/1011: top += dh, bottom += dh
    kAnchorStickBottomRight // 1/2/1005: all four += dw/dh
};
struct AnchorEntry {
    int id;
    AnchorMode mode;
};
const AnchorEntry kAnchors[] = {
    { kDlgTabCaption, kAnchorGrowRight },
    { kDlgListView,   kAnchorGrowBoth },
    { kDlgTab,        kAnchorGrowBoth },
    { kBtnSetFile,    kAnchorStickBottom },
    { kBtnRemove,     kAnchorStickBottom },
    { kBtnHideShow,   kAnchorStickBottom },
    { kBtnGroup,      kAnchorStickBottom },
    { 1,              kAnchorStickBottomRight },
    { 2,              kAnchorStickBottomRight },
    { kBtnRefresh,    kAnchorStickBottomRight },
};
RECT g_anchorRects[sizeof(kAnchors) / sizeof(kAnchors[0])];

const char* EffectTextForModel(ModelData* model)
{
    static std::string text;
    if (model == nullptr) {
        text = "(default)";
        return text.c_str();
    }
    if (!model->shown()) {
        text = "(hide)";
        return text.c_str();
    }
    const std::string& assigned = model->effectFile();
    if (!assigned.empty()) {
        text = assigned;
        return text.c_str();
    }
    text = "(none)";
    return text.c_str();
}

void DlgInsertColumn(HWND list, int index, const char* title, int width)
{
    LVCOLUMNA column;
    memset(&column, 0, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = const_cast<LPSTR>(title);
    column.cx = width;
    column.iSubItem = index;
    SendMessageA(list, LVM_INSERTCOLUMNA, (WPARAM)index, (LPARAM)&column);
}

void DlgSetRowText(HWND list, int index, int subItem, const char* text)
{
    LVITEMA item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT;
    item.iItem = index;
    item.iSubItem = subItem;
    item.pszText = const_cast<LPSTR>(text);
    if (subItem == 0) {
        SendMessageA(list, LVM_INSERTITEMA, 0, (LPARAM)&item);
    } else {
        SendMessageA(list, LVM_SETITEMTEXTA, (WPARAM)index, (LPARAM)&item);
    }
}

void DlgSetRowCheck(HWND list, int index, int stateImage)
{
    // LVM_SETITEMSTATE with the 0xf000 state-image mask (FUN_18003fff0's
    // 0x102b calls). stateImage 0 = no checkbox (the "(default)" row),
    // 1 = unchecked, 2 = checked. Skipped when the image already matches so
    // the refresh does not repaint the whole control (flicker audit).
    LVITEMA state;
    memset(&state, 0, sizeof(state));
    state.mask = LVIF_STATE;
    state.stateMask = LVIS_STATEIMAGEMASK;
    state.state = INDEXTOSTATEIMAGEMASK(stateImage);
    LRESULT current = SendMessageA(list, LVM_GETITEMSTATE, (WPARAM)index,
                                   LVIS_STATEIMAGEMASK);
    if ((UINT)current == state.state) {
        return;
    }
    state.iItem = index;
    SendMessageA(list, LVM_SETITEMSTATE, (WPARAM)index, (LPARAM)&state);
}

// [FUN_180041620] collect the selected rows as (object, subset) pairs.
// filters: onlyNormalObject (param_4) / onlyWholeObjectRows (param_5).
// The "(default)" row (row 0) collects as {null, -1} only while the tab
// control's item rect gate (FUN_180041620's *param_2) is zero.
struct SelectedRef {
    ModelData* model;   // null = the (default) row
    int subset;
};

LONG DlgTabRectGate()
{
    // [FUN_180041620] *param_2 = the TCM_GETITEMRECT result field: zero only
    // when the retrieval fails, in which case row 0 joins the collections.
    HWND tab = GetDlgItem(g_dlgWindow, kDlgTab);
    if (tab == nullptr) {
        return 0;
    }
    RECT rc;
    rc.left = 8;
    rc.top = 0;
    rc.right = 0;
    rc.bottom = 0;
    LRESULT cursel = SendMessageA(tab, TCM_GETCURSEL, 0, 0);
    if (SendMessageA(tab, TCM_GETITEMRECT, (WPARAM)cursel, (LPARAM)&rc) == 0) {
        return 0;
    }
    return rc.bottom;
}

bool DlgCollectSelected(bool onlyNormalObject, bool onlyWholeObjectRows,
                        std::vector<SelectedRef>& out)
{
    out.clear();
    HWND list = DlgList();
    if (list == nullptr) {
        return false;
    }
    bool defaultRowLive = DlgTabRectGate() == 0;
    int index = (int)SendMessageA(list, LVM_GETNEXTITEM, (WPARAM)-1,
                                  MAKELPARAM(LVNI_SELECTED, 0));
    while (index != -1) {
        if (index >= 0 && index < (int)g_rows.size()) {
            const RowRef& row = g_rows[index];
            if (row.model == nullptr) {
                if (defaultRowLive && !onlyNormalObject) {
                    SelectedRef ref;
                    ref.model = nullptr;
                    ref.subset = -1;
                    out.push_back(ref);
                }
            } else if ((!onlyNormalObject || row.model->renderClass() == 0) &&
                       (!onlyWholeObjectRows || row.subset < 0)) {
                SelectedRef ref;
                ref.model = row.model;
                ref.subset = row.subset;
                out.push_back(ref);
            }
        }
        index = (int)SendMessageA(list, LVM_GETNEXTITEM, (WPARAM)index,
                                  MAKELPARAM(LVNI_SELECTED, 0));
    }
    return !out.empty();
}

// [FUN_180041aa0] the selection probe used by the menu/button sync: true when
// at least one row survived the filter (vector begin != end).
bool DlgHasSelection(bool onlyNormalObject)
{
    std::vector<SelectedRef> unused;
    return DlgCollectSelected(onlyNormalObject, false, unused);
}

// [FUN_180043fd0] the FIRST selected row's effect path ("" when none / the
// "(default)" row / an empty mapping).
std::string DlgSelectedEffectPath()
{
    HWND list = DlgList();
    if (list == nullptr) {
        return std::string();
    }
    int index = (int)SendMessageA(list, LVM_GETNEXTITEM, (WPARAM)-1,
                                  MAKELPARAM(LVNI_SELECTED, 0));
    if (index <= 0 || index >= (int)g_rows.size()) {
        return std::string();   // no selection or the "(default)" row
    }
    ModelData* model = g_rows[index].model;
    if (model == nullptr) {
        return std::string();
    }
    return model->effectFile();
}

// [FUN_1800429a0] the Subset-Extract checkmark: every selected row's object
// is in the expanded set (an empty selection or the lone default row = off).
bool DlgExpandedProbe()
{
    std::vector<SelectedRef> refs;
    DlgCollectSelected(true, false, refs);
    if (refs.empty()) {
        return false;
    }
    for (size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].model == nullptr) {
            return false;
        }
        if (g_expanded.find(refs[i].model) == g_expanded.end()) {
            return false;
        }
    }
    return true;
}

void DlgRebuildList();
void DlgRefreshTexts();
void DlgSyncSelectionState();

// [FUN_180041b00 / FUN_180042610 tail] after every edit: enable Apply,
// refresh the row texts and repaint the main window so the change shows on
// the next frame. The original repaint is the 1x1 rectangle at (0,0)
// (InvalidateRect(main, {0,0,1,1}, FALSE)) - a full-window invalidate makes
// every MMD control flicker.
void DlgAfterEdit()
{
    if (g_dlgWindow != nullptr) {
        HWND apply = GetDlgItem(g_dlgWindow, kBtnRefresh);
        if (apply != nullptr) {
            EnableWindow(apply, TRUE);
        }
        DlgRefreshTexts();
        DlgSyncSelectionState();
    }
    if (g_mainWindow != nullptr) {
        RECT rect;
        rect.left = 0;
        rect.top = 0;
        rect.right = 1;
        rect.bottom = 1;
        InvalidateRect(g_mainWindow, &rect, FALSE);
    }
}

// [FUN_180042610] the checkbox / hide-show applier: write the shown state of
// every selected object (or the single `singleItem` row when >= 0). The
// repaint is the 1x1 main-window rect of the original tail.
void DlgApplyShown(bool shown, int singleItem)
{
    std::vector<SelectedRef> refs;
    DlgCollectSelected(false, false, refs);
    if (singleItem >= 0 && singleItem < (int)g_rows.size() &&
        g_rows[singleItem].model != nullptr) {
        bool already = false;
        for (size_t i = 0; i < refs.size(); ++i) {
            if (refs[i].model == g_rows[singleItem].model) {
                already = true;
                break;
            }
        }
        if (!already) {
            SelectedRef ref;
            ref.model = g_rows[singleItem].model;
            ref.subset = g_rows[singleItem].subset;
            refs.push_back(ref);
        }
    }
    for (size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].model != nullptr) {
            refs[i].model->setShown(shown);
        }
    }
    if (!refs.empty() && g_mainWindow != nullptr) {
        RECT rect;
        rect.left = 0;
        rect.top = 0;
        rect.right = 1;
        rect.bottom = 1;
        InvalidateRect(g_mainWindow, &rect, FALSE);
    }
}

// [FUN_180041b00] the effect-text writer: object rows write their assignment
// (the default row only joins through the tab-gate, as in the original).
void DlgWriteEffectForSelection(const char* path)
{
    std::vector<SelectedRef> refs;
    DlgCollectSelected(true, false, refs);
    for (size_t i = 0; i < refs.size(); ++i) {
        ModelData* model = refs[i].model;
        if (model == nullptr) {
            continue;
        }
        MmeAssignEffect(model->objectId(), refs[i].subset, path);
        model->setEffectFile(path);
        if (path[0] != 0 && !model->shown()) {
            model->setShown(true);
        }
    }
    if (!refs.empty() && g_mainWindow != nullptr) {
        RECT rect;
        rect.left = 0;
        rect.top = 0;
        rect.right = 1;
        rect.bottom = 1;
        InvalidateRect(g_mainWindow, &rect, FALSE);
    }
}

// --- the list construction [FUN_18003fff0] -----------------------------------

void DlgAddDefaultRow(HWND list)
{
    RowRef row;
    row.model = nullptr;
    row.subset = -1;

    LVITEMA item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = 0;
    item.pszText = const_cast<LPSTR>("(default)");
    item.lParam = (LPARAM)g_rows.size();
    int index = (int)SendMessageA(list, LVM_INSERTITEMA, 0, (LPARAM)&item);
    if (index < 0) {
        return;
    }
    g_rows.push_back(row);
    const std::string& defaultEffect = MmeEmmDefaultEffect();
    DlgSetRowText(list, index, kColEffect,
                  defaultEffect.empty() ? "(none)" : defaultEffect.c_str());
    // FUN_18003fff0 sets the state image of row 0 to 0 - no checkbox.
    DlgSetRowCheck(list, index, 0);
}

std::string DlgSubsetRowLabel(ModelData* model, int subset, int matCount)
{
    // FUN_18003fff0's subset row text: "  |-- <name>:" and, for the last
    // material, "  `-- <name>:"; unnamed materials fall back to
    // "材質%d" (Japanese UI) / "subset%d".
    const wchar_t* materialName = GetMaterialName(model->objectId(),
                                                  (unsigned long)subset, 0);
    std::string name;
    if (materialName != nullptr) {
        name = MmeWideToAnsi(materialName);
    }
    if (name.empty()) {
        name = MmeFormat(MmeIsEnglishUiMode() ? "subset%d" : "\xE6\x9D\x90\xE8\xB3\xAA%d",
                         subset);
    }
    return MmeFormat(subset == matCount - 1 ? "  `-- %s:" : "  |-- %s:",
                     name.c_str());
}

void DlgAddObjectRow(HWND list, ModelData* model)
{
    RowRef row;
    row.model = model;
    row.subset = -1;

    const char* filename = model->filename();
    const char* base = filename;
    for (const char* q = filename; q != nullptr && *q != '\0'; ++q) {
        if (*q == '\\' || *q == '/') {
            base = q + 1;
        }
    }

    LVITEMA item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = (int)g_rows.size();
    item.pszText = const_cast<LPSTR>(base);
    item.lParam = (LPARAM)g_rows.size();
    int index = (int)SendMessageA(list, LVM_INSERTITEMA, 0, (LPARAM)&item);
    if (index < 0) {
        return;
    }
    g_rows.push_back(row);
    DlgSetRowText(list, index, kColEffect, EffectTextForModel(model));
    // LVS_EX_CHECKBOXES re-initializes the state image at insert time; apply
    // the checked state after the insert (checked = shown).
    DlgSetRowCheck(list, index, model->shown() ? 2 : 1);

    // The expanded-object rows (DAT_1800d9cc8): per-material entries follow.
    if (g_expanded.find(model) != g_expanded.end()) {
        int matCount = model->materialCount();
        for (int m = 0; m < matCount; ++m) {
            RowRef sub;
            sub.model = model;
            sub.subset = m;
            std::string label = DlgSubsetRowLabel(model, m, matCount);
            LVITEMA subItem;
            memset(&subItem, 0, sizeof(subItem));
            subItem.mask = LVIF_TEXT | LVIF_PARAM;
            subItem.iItem = (int)g_rows.size();
            subItem.pszText = const_cast<LPSTR>(label.c_str());
            subItem.lParam = (LPARAM)g_rows.size();
            int subIndex = (int)SendMessageA(list, LVM_INSERTITEMA, 0,
                                             (LPARAM)&subItem);
            if (subIndex < 0) {
                break;
            }
            g_rows.push_back(sub);
            DlgSetRowText(list, subIndex, kColEffect, EffectTextForModel(model));
            DlgSetRowCheck(list, subIndex, model->shown() ? 2 : 1);
        }
    }
}

void DlgRebuildList()
{
    HWND list = DlgList();
    if (list == nullptr) {
        return;
    }
    g_rebuilding = true;   // DAT_1800d99db
    SendMessageA(list, WM_SETREDRAW, FALSE, 0);
    SendMessageA(list, LVM_DELETEALLITEMS, 0, 0);
    g_rows.clear();

    // The "(default)" row is always row 0 (the original lists it first).
    DlgAddDefaultRow(list);
    MmeContext* ctx = MmeGetContext();
    if (ctx != nullptr) {
        for (size_t i = 0; i < ctx->models.size(); ++i) {
            ModelData* model = ctx->models[i];
            if (model != nullptr) {
                DlgAddObjectRow(list, model);
            }
        }
        g_lastModelCount = ctx->models.size();
    }
    SendMessageA(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, FALSE);
    g_rebuilding = false;
}

// [FUN_18003fff0-lite] refresh the visible Effect File texts and the
// checkbox images from the working state. Guarded by DAT_1800d99db so the
// state writes cannot re-enter the checkbox handler (the hide/show flicker).
void DlgRefreshTexts()
{
    HWND list = DlgList();
    if (list == nullptr) {
        return;
    }
    g_rebuilding = true;
    for (size_t i = 0; i < g_rows.size(); ++i) {
        ModelData* model = g_rows[i].model;
        if (model == nullptr) {
            const std::string& defaultEffect = MmeEmmDefaultEffect();
            DlgSetRowText(list, (int)i, kColEffect,
                          defaultEffect.empty() ? "(none)" : defaultEffect.c_str());
            DlgSetRowCheck(list, (int)i, 0);
            continue;
        }
        DlgSetRowText(list, (int)i, kColEffect, EffectTextForModel(model));
        DlgSetRowCheck(list, (int)i, model->shown() ? 2 : 1);
    }
    g_rebuilding = false;
}

// [FUN_180044080 -101] the selection-driven menu/button sync. The dialog
// menu resource pre-marks the Edit items INACTIVE; the runtime
// EnableMenuItem(MF_ENABLED) overrides the preset (resource audit: menus
// 109/110 mark 40007/40012/40013/40008/40006 INACTIVE and 40016/40017
// INACTIVE; 40014/40028 start enabled).
void DlgSyncSelectionState()
{
    if (g_dlgWindow == nullptr) {
        return;
    }
    bool hasObject = DlgHasSelection(true);
    bool hasAny = DlgHasSelection(false);
    g_selectedObject = hasObject;
    g_selectedAny = hasAny;

    HMENU menu = GetMenu(g_dlgWindow);
    if (menu != nullptr) {
        UINT objectState = hasObject ? MF_ENABLED : MF_DISABLED;
        EnableMenuItem(menu, 40006, objectState);    // Subset-Extract
        EnableMenuItem(menu, 40007, objectState);    // Set Effect File
        EnableMenuItem(menu, 40012, objectState);    // Remove Effect
        EnableMenuItem(menu, 40008, objectState);    // Reset with default
        UINT anyState = hasAny ? MF_ENABLED : MF_DISABLED;
        EnableMenuItem(menu, 40013, anyState);       // Hide/Show
        // [-101 tail] 40016/40017 count the selected whole-object normal
        // rows (0x364 == 0 && subset < 0): 40016 when any, 40017 when one.
        std::vector<SelectedRef> wholeRows;
        DlgCollectSelected(true, true, wholeRows);
        EnableMenuItem(menu, 40016, !wholeRows.empty() ? MF_ENABLED : MF_DISABLED);
        EnableMenuItem(menu, 40017, wholeRows.size() == 1 ? MF_ENABLED : MF_DISABLED);
        // [FUN_1800429a0] the Subset-Extract checkmark follows the expanded
        // state of the selection (latched in DAT_1800d9f4a).
        g_subsetCheck = DlgExpandedProbe();
        CheckMenuItem(menu, 40006,
                      g_subsetCheck ? MF_CHECKED : MF_UNCHECKED);
        // [FUN_180043fd0] Disassemble is enabled only when the FIRST
        // selected row carries an effect path (the "(default)" row -> off).
        std::string path = DlgSelectedEffectPath();
        EnableMenuItem(menu, 40014, path.empty() ? MF_DISABLED : MF_ENABLED);
    }
    HWND button = GetDlgItem(g_dlgWindow, kBtnSetFile);
    if (button != nullptr) EnableWindow(button, hasObject);
    button = GetDlgItem(g_dlgWindow, kBtnRemove);
    if (button != nullptr) EnableWindow(button, hasObject);
    button = GetDlgItem(g_dlgWindow, kBtnHideShow);
    if (button != nullptr) EnableWindow(button, hasAny);
}

// --- file dialogs -----------------------------------------------------------

bool DlgOpenFileDialog(const char* filter, const char* title, char* out, DWORD outLen)
{
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    out[0] = '\0';
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_dlgWindow;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = out;
    ofn.nMaxFile = outLen;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) != FALSE;
}

bool DlgSaveFileDialog(const char* filter, const char* title, const char* defExt,
                       char* out, DWORD outLen)
{
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    out[0] = '\0';
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_dlgWindow;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = out;
    ofn.nMaxFile = outLen;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    return GetSaveFileNameA(&ofn) != FALSE;
}

// [FUN_18001e4e0 + FUN_180041d10] load the effect through the engine (the
// SkipValidation gate lives inside the engine); a failure reports and the
// assignment is not written.
bool DlgValidateEffectPath(const char* path)
{
    MmeContext* ctx = MmeGetContext();
    IDirect3DDevice9* device = (ctx != nullptr) ? ctx->device : nullptr;
    std::shared_ptr<LoadedEffect> loaded = MmeEngineLoadEffectFile(device, path);
    if (loaded != nullptr && loaded->effect != nullptr) {
        return true;
    }
    const std::string& errors =
        (loaded != nullptr) ? loaded->errorText : std::string();
    std::string message = "Failed to load effect file:\n";
    message += path;
    message += "\n\n";
    message += errors;
    MmeLogWrite(message.c_str(), 1);
    return false;
}

// --- the Set Effect flow [FUN_180041d10] ------------------------------------

void DlgSetEffectFlow()
{
    std::vector<SelectedRef> refs;
    DlgCollectSelected(true, false, refs);
    if (refs.empty()) {
        return;   // the original's empty-collection no-op
    }

    // The initial path: the first selected object's current assignment (the
    // original seeds the dialog with the row's text), else the last dir.
    char path[MAX_PATH];
    path[0] = '\0';
    if (!refs.empty() && refs[0].model != nullptr) {
        const std::string& current = refs[0].model->effectFile();
        if (!current.empty()) {
            strncpy_s(path, current.c_str(), _TRUNCATE);
        }
    }

    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_dlgWindow;
    ofn.lpstrFilter = "fx files(*.fx)\0*.fx\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!g_lastFxDir.empty()) {
        ofn.lpstrInitialDir = g_lastFxDir.c_str();
    }
    if (GetOpenFileNameA(&ofn) == FALSE) {
        return;
    }
    // remember the directory part (the original keeps DAT_1800da130)
    {
        char drive[3] = { 0 };
        char dir[0x100] = { 0 };
        if (_splitpath_s(path, drive, sizeof(drive), dir, sizeof(dir),
                         nullptr, 0, nullptr, 0) == 0) {
            char full[0x104];
            if (_makepath_s(full, sizeof(full), drive, dir, nullptr, nullptr) == 0) {
                g_lastFxDir = full;
            }
        }
    }

    if (!DlgValidateEffectPath(path)) {
        EnableWindow(GetDlgItem(g_dlgWindow, kBtnRefresh), TRUE);  // [54542]
        return;
    }

    // Write the assignment (objects + the default row through the gate).
    DlgWriteEffectForSelection(path);
    DlgAfterEdit();
}

// [WM_DROPFILES .fx/.fxm/.fxsub] validate + assign without the file dialog.
void DlgAssignEffectFileDirect(const char* path)
{
    if (!DlgValidateEffectPath(path)) {
        return;
    }
    DlgWriteEffectForSelection(path);
    DlgAfterEdit();
}

// [40012 Remove / FUN_180041b00 with an empty path]
void DlgRemoveEffectFlow()
{
    DlgWriteEffectForSelection("");
    DlgAfterEdit();
}

// [40013 Hide/Show / FUN_180042610 via 0x9c4d] uniform target across the
// selection: any shown object -> hide all; otherwise show all.
void DlgToggleHideShowFlow()
{
    std::vector<SelectedRef> refs;
    DlgCollectSelected(false, false, refs);
    if (refs.empty()) {
        return;
    }
    bool anyShown = false;
    for (size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].model != nullptr && refs[i].model->shown()) {
            anyShown = true;
            break;
        }
    }
    DlgApplyShown(!anyShown, -1);
    DlgAfterEdit();
}

// [40006 Subset-Extract / FUN_180042a80] the toggle over the selected rows'
// objects: unchecked -> expand into per-material rows, checked -> collapse
// back. The checkmark state (FUN_1800429a0) drives the direction; a subset
// row selection toggles its whole object.
void DlgSubsetExtractFlow()
{
    bool collapse = g_subsetCheck;
    std::vector<SelectedRef> refs;
    DlgCollectSelected(true, false, refs);
    std::set<ModelData*> targets;
    for (size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].model != nullptr) {
            targets.insert(refs[i].model);
        }
    }
    bool changed = false;
    for (std::set<ModelData*>::const_iterator it = targets.begin();
         it != targets.end(); ++it) {
        ModelData* model = *it;
        bool isExpanded = g_expanded.find(model) != g_expanded.end();
        if (!collapse && !isExpanded) {
            g_expanded.insert(model);
            changed = true;
        } else if (collapse && isExpanded) {
            g_expanded.erase(model);
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    // Both directions rebuild the rows through FUN_18003fff0 and repaint.
    DlgRebuildList();
    DlgAfterEdit();
}

// [40008 Reset with default / FUN_1800424a0]
void DlgResetWithDefaultFlow()
{
    std::vector<SelectedRef> refs;
    DlgCollectSelected(true, false, refs);
    for (size_t i = 0; i < refs.size(); ++i) {
        ModelData* model = refs[i].model;
        if (model == nullptr) {
            continue;
        }
        MmeAssignEffect(model->objectId(), -1, "");
        model->setEffectFile("");
    }
    DlgAfterEdit();
}

// [FUN_180063730] the disassemble result window. The original normalizes the
// text's line breaks to "\r\n" (EDIT control format), builds the parameter
// block {text, title} and shows a MODAL dialog reusing the log template
// (DialogBoxParamA(g_hInst, 0x69, owner, proc, (LPARAM)&strings[0])). The
// proc fills edit 1001 and captions the window with the title; the Close
// button (IDOK/IDCANCEL) ends the dialog.
void DlgDisassembleNormalizeLineEnds(const std::string& input, std::string& out)
{
    out.clear();
    std::string line;
    for (std::string::const_iterator it = input.begin(); it != input.end(); ++it) {
        char c = *it;
        if (c == '\n' || c == '\r') {
            out += line;
            out += "\r\n";
            line.clear();
            if (c == '\r' && (it + 1) != input.end() && *(it + 1) == '\n') {
                ++it;
            }
        } else if (c != '\0') {
            line += c;
        }
    }
    if (!line.empty()) {
        out += line;
        out += "\r\n";
    }
}

struct DisassembleDialogParams {
    std::string text;    // [0]
    std::string title;   // [1]
};

INT_PTR CALLBACK MmeDisassembleDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DisassembleDialogParams* params =
        reinterpret_cast<DisassembleDialogParams*>(
            GetWindowLongPtrA(dlg, GWLP_USERDATA));
    switch (msg) {
    case WM_INITDIALOG:
        params = reinterpret_cast<DisassembleDialogParams*>(lParam);
        SetWindowLongPtrA(dlg, GWLP_USERDATA, (LONG_PTR)params);
        if (params != nullptr) {
            SetWindowTextA(dlg, params->title.c_str());
            SetDlgItemTextA(dlg, 1001, params->text.c_str());
        }
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(dlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// [40014 Disassemble Effect / the 0x9c4e case + FUN_180063730 presentation]
void DlgDisassembleFlow()
{
    std::string path = DlgSelectedEffectPath();
    // [0x9c4e] the dialog is shown even when the disassembly produced no
    // text (the original calls FUN_180063730 unconditionally after probing
    // the selection); only the selection probe gates the flow.
    std::string text;
    if (!path.empty()) {
        MmeContext* ctx = MmeGetContext();
        IDirect3DDevice9* device = (ctx != nullptr) ? ctx->device : nullptr;
        std::shared_ptr<LoadedEffect> loaded = MmeEngineLoadEffectFile(device, path);
        if (loaded != nullptr && loaded->effect != nullptr) {
            ID3DXBuffer* disassembly = nullptr;
            if (SUCCEEDED(D3DXDisassembleEffect(loaded->effect, FALSE,
                                                &disassembly)) &&
                disassembly != nullptr) {
                std::string raw((const char*)disassembly->GetBufferPointer(),
                                disassembly->GetBufferSize());
                disassembly->Release();
                DlgDisassembleNormalizeLineEnds(raw, text);
            }
        }
    }
    DisassembleDialogParams params;
    params.text = text;
    params.title = "Disassemble Effect";
    DialogBoxParamA(g_hInst, MAKEINTRESOURCEA(105), g_dlgWindow,
                    MmeDisassembleDlgProc, (LPARAM)&params);
}

// [40028 Skip Device Validation / the 0x9c5c case]
void DlgToggleSkipValidation(HWND dlg)
{
    HMENU menu = GetMenu(dlg);
    UINT state = GetMenuState(menu, 40028, MF_BYCOMMAND);
    g_skipValidation = (state & MF_CHECKED) == 0;
    CheckMenuItem(menu, 40028, g_skipValidation ? MF_CHECKED : MF_UNCHECKED);
    if (!g_iniPath.empty()) {
        WritePrivateProfileStringA("System", "SkipValidation",
                                   g_skipValidation ? "true" : "false",
                                   g_iniPath.c_str());
    }
}

// --- the WM_SIZE anchor table [DAT_1800d7788] -------------------------------

void DlgBuildAnchors(HWND dlg)
{
    RECT client;
    GetClientRect(dlg, &client);
    for (size_t i = 0; i < sizeof(kAnchors) / sizeof(kAnchors[0]); ++i) {
        HWND control = GetDlgItem(dlg, kAnchors[i].id);
        RECT rect;
        rect.left = 0;
        rect.top = 0;
        rect.right = 0;
        rect.bottom = 0;
        if (control != nullptr) {
            GetWindowRect(control, &rect);
            MapWindowPoints(nullptr, dlg, (LPPOINT)&rect, 2);
        }
        // Store the rect relative to the initial client size (the original
        // subtracts the client extents per the anchor mode).
        switch (kAnchors[i].mode) {
        case kAnchorGrowBoth:
            rect.right -= client.right;
            rect.bottom -= client.bottom;
            break;
        case kAnchorGrowRight:
            rect.right -= client.right;
            break;
        case kAnchorStickBottom:
            rect.top -= client.bottom;
            rect.bottom -= client.bottom;
            break;
        case kAnchorStickBottomRight:
            rect.left -= client.right;
            rect.top -= client.bottom;
            rect.right -= client.right;
            rect.bottom -= client.bottom;
            break;
        }
        g_anchorRects[i] = rect;
    }
}

void DlgApplyAnchors(HWND dlg)
{
    RECT client;
    GetClientRect(dlg, &client);
    HWND list = DlgList();
    LONG oldListWidth = 0;
    if (list != nullptr) {
        RECT listRect;
        GetClientRect(list, &listRect);
        oldListWidth = listRect.right - listRect.left;
    }
    for (size_t i = 0; i < sizeof(kAnchors) / sizeof(kAnchors[0]); ++i) {
        HWND control = GetDlgItem(dlg, kAnchors[i].id);
        if (control == nullptr) {
            continue;
        }
        RECT rect = g_anchorRects[i];
        switch (kAnchors[i].mode) {
        case kAnchorGrowBoth:
            rect.right += client.right;
            rect.bottom += client.bottom;
            break;
        case kAnchorGrowRight:
            rect.right += client.right;
            break;
        case kAnchorStickBottom:
            rect.top += client.bottom;
            rect.bottom += client.bottom;
            break;
        case kAnchorStickBottomRight:
            rect.left += client.right;
            rect.top += client.bottom;
            rect.right += client.right;
            rect.bottom += client.bottom;
            break;
        }
        MoveWindow(control, rect.left, rect.top,
                   rect.right - rect.left, rect.bottom - rect.top, FALSE);
        // [WM_SIZE 0x3eb case] scale the two list columns with the list
        // width, keeping the fractional accumulators so rounding does not
        // drift (DAT_1800da1a0/1a8).
        if (kAnchors[i].id == kDlgListView && list != nullptr &&
            oldListWidth > 0) {
            LONG newListWidth = rect.right - rect.left;
            if (newListWidth > 0 && newListWidth != oldListWidth) {
                for (int col = 0; col < 2; ++col) {
                    LRESULT width = SendMessageA(list, LVM_GETCOLUMNWIDTH,
                                                 (WPARAM)col, 0);
                    double exact = (double)width;
                    if (g_colFraction[col] != 0.0 &&
                        (LONG)width == (LONG)(g_colFraction[col] + 0.5)) {
                        exact = g_colFraction[col];
                    }
                    double scaled = exact * (double)newListWidth /
                                    (double)oldListWidth;
                    g_colFraction[col] = scaled;
                    SendMessageA(list, LVM_SETCOLUMNWIDTH, (WPARAM)col,
                                 MAKELPARAM((short)(int)scaled, 0));
                }
            }
        }
    }
    InvalidateRect(dlg, nullptr, TRUE);
    if (IsIconic(dlg)) {
        return;
    }
    if (IsZoomed(dlg)) {
        return;
    }
    GetWindowRect(dlg, &g_savedWindowRect);
    g_hasSavedWindowRect = true;
}

// --- the WH_GETMESSAGE keyboard router [FUN_18003fb70] ----------------------

LRESULT CALLBACK MmeAssignmentGetMessageProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0 && wParam == 1 /*PM_REMOVE*/ && g_hookDialogHwnd != nullptr &&
        g_sizingFlag == 0) {
        MSG* message = reinterpret_cast<MSG*>(lParam);
        if (message != nullptr && message->message >= 0x100 &&
            message->message <= 0x109) {
            if (IsDialogMessageA(g_hookDialogHwnd, message)) {
                message->message = 0;
                message->lParam = 0;
                message->wParam = 0;
            }
        }
    }
    return CallNextHookEx(g_getMessageHook, code, wParam, lParam);
}

// --- WndProc [FUN_180044080] ------------------------------------------------

INT_PTR CALLBACK MmeAssignmentDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        g_dlgWindow = dlg;
        g_hookDialogHwnd = dlg;
        g_getMessageHook = SetWindowsHookExA(WH_GETMESSAGE,
                                             MmeAssignmentGetMessageProc,
                                             nullptr, GetCurrentThreadId());
        DragAcceptFiles(dlg, TRUE);
        bool japanese = !MmeIsEnglishUiMode();
        if (!japanese) {
            SetWindowTextA(dlg, "Map Effect File");
            SetWindowTextA(GetDlgItem(dlg, 1), "OK");          // IDOK：资源模板为日文侧文案
            SetWindowTextA(GetDlgItem(dlg, 2), "Cancel");
            SetWindowTextA(GetDlgItem(dlg, kBtnRefresh), "Apply");
            SetWindowTextA(GetDlgItem(dlg, kBtnGroup), "Selected Items");
            SetWindowTextA(GetDlgItem(dlg, kBtnSetFile), "Set Effect");
            SetWindowTextA(GetDlgItem(dlg, kBtnRemove), "Remove Effect");
            SetWindowTextA(GetDlgItem(dlg, kBtnHideShow), "Hide/Show");
        }
        HWND tab = GetDlgItem(dlg, kDlgTab);
        if (tab != nullptr) {
            TCITEMA tabItem;
            memset(&tabItem, 0, sizeof(tabItem));
            tabItem.mask = TCIF_TEXT;
            tabItem.pszText = const_cast<LPSTR>("Main");
            SendMessageA(tab, TCM_INSERTITEMA, 0, (LPARAM)&tabItem);
        }
        HWND list = DlgList();
        if (list != nullptr) {
            LRESULT old = SendMessageA(list, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0);
            SendMessageA(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                         (LPARAM)(((DWORD)old & 0xffffffff) | 0x25));
        }
        DlgInsertColumn(list, kColObject, "Object", 150);
        DlgInsertColumn(list, kColEffect, "Effect File", 345);
        HMENU menu = LoadMenuA(g_hInst, MAKEINTRESOURCEA(japanese ? 0x6d : 0x6e));
        if (menu != nullptr) {
            CheckMenuItem(menu, 40028, g_skipValidation ? MF_CHECKED : MF_UNCHECKED);
            SetMenu(dlg, menu);
        }
        HICON icon = LoadIconA(g_hInst, MAKEINTRESOURCEA(0x68));
        if (icon != nullptr) {
            SendMessageA(dlg, WM_SETICON, 0, (LPARAM)icon);
            SendMessageA(dlg, WM_SETICON, 1, (LPARAM)icon);
        }
        // [FUN_180022ab0(0x1800d9cc8)] the expanded-subset set is cleared at
        // every dialog open (each open starts collapsed), and the WM_SIZE
        // column-fraction accumulators restart from the fresh layout.
        g_expanded.clear();
        g_colFraction[0] = 0.0;
        g_colFraction[1] = 0.0;
        MmeLogWrite("Open the effect mapping dialog\n", 0);
        // The DAT_1800d7788 anchor table is built from the template layout.
        DlgBuildAnchors(dlg);
        if (g_hasSavedWindowRect &&
            (g_savedWindowRect.right != g_savedWindowRect.left ||
             g_savedWindowRect.bottom != g_savedWindowRect.top)) {
            MoveWindow(dlg, g_savedWindowRect.left, g_savedWindowRect.top,
                       g_savedWindowRect.right - g_savedWindowRect.left,
                       g_savedWindowRect.bottom - g_savedWindowRect.top, FALSE);
        }
        // The working copy starts as a snapshot of the live state; the
        // default-effect snapshot (DAT_1800d74d0/74d4/74d8) feeds Cancel.
        g_defaultSnapshot = MmeEmmDefaultEffect();
        g_selectedObject = false;
        g_selectedAny = false;
        g_subsetCheck = false;
        g_dirty = false;
        g_clickLatch = false;
        g_rebuilding = false;
        DlgRebuildList();
        DlgSyncSelectionState();
        // Nothing pending at open: the Apply button starts disabled (the
        // original re-disables it in the 1005 handler after committing).
        HWND apply = GetDlgItem(dlg, kBtnRefresh);
        if (apply != nullptr) {
            EnableWindow(apply, FALSE);
        }
        return TRUE;
    }
    case WM_ENTERSIZEMOVE:   // 0x211: DAT_1800d99dc = 1
        g_sizingFlag = 1;
        return TRUE;
    case WM_EXITSIZEMOVE:    // 0x212: DAT_1800d99dc = 0
        g_sizingFlag = 0;
        return TRUE;
    case WM_SIZE: {
        DlgApplyAnchors(dlg);
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
        if (header != nullptr && header->idFrom == kDlgTab &&
            header->code == TCN_SELCHANGE) {
            // [-551 from the 0x3ea tab control] DAT_1800d9f44 = cursel + the
            // selection-state refresh (the original dispatches WM_NOTIFY by
            // notification code, so the tab's TCN_SELCHANGE lands here even
            // though idFrom is the tab).
            DlgSyncSelectionState();
            return TRUE;
        }
        if (header != nullptr && header->idFrom == kDlgListView) {
            switch (header->code) {
            case LVN_ITEMCHANGED: {
                // [FUN_180044080 -101] the rebuild guard (DAT_1800d99db)
                // drops everything while FUN_18003fff0 runs.
                if (g_rebuilding) {
                    return TRUE;
                }
                NMLISTVIEW* info = reinterpret_cast<NMLISTVIEW*>(lParam);
                unsigned int changed = info->uNewState ^ info->uOldState;
                if ((changed & LVIS_STATEIMAGEMASK) != 0) {
                    // The checkbox toggle runs the shown write
                    // (FUN_180042610) and latches the click latch.
                    unsigned int newState = info->uNewState & LVIS_STATEIMAGEMASK;
                    if (newState != 0 && info->iItem >= 0 &&
                        info->iItem < (int)g_rows.size() &&
                        g_rows[info->iItem].model != nullptr) {
                        bool shown = (newState >> 12) == 2;
                        g_clickLatch = true;   // DAT_1800d9f4b
                        DlgApplyShown(shown, info->iItem);
                        DlgAfterEdit();
                    }
                }
                if ((changed & 3) != 0) {
                    // Selection/focus bits changed: the menu/button sync.
                    DlgSyncSelectionState();
                }
                return TRUE;
            }
            case NM_CLICK:            // -2: clear the checkbox latch
                g_clickLatch = false;
                return TRUE;
            case NM_DBLCLK: {         // -3: the Set Effect flow
                if (!g_clickLatch) {
                    DlgSetEffectFlow();
                }
                g_clickLatch = false;
                return TRUE;
            }
            case NM_RCLICK: {         // -5: the Edit submenu (GetSubMenu 1)
                POINT cursor;
                GetCursorPos(&cursor);
                HMENU menu = GetMenu(dlg);
                if (menu != nullptr) {
                    HMENU popup = GetSubMenu(menu, 1);
                    if (popup != nullptr) {
                        TrackPopupMenu(popup, 0, cursor.x, cursor.y, 0, dlg, nullptr);
                    }
                }
                return TRUE;
            }
            case NM_CUSTOMDRAW: {     // -12: the hidden-row gray paint
                NMLVCUSTOMDRAW* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    SetWindowLongPtrA(dlg, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                    return TRUE;
                }
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    int item = (int)draw->nmcd.dwItemSpec;
                    if (item == 0) {
                        // The "(default)" row is always gray (informational).
                        draw->clrText = GetSysColor(COLOR_GRAYTEXT);
                        draw->clrTextBk = GetSysColor(COLOR_BTNFACE);
                        SetWindowLongPtrA(dlg, DWLP_MSGRESULT, CDRF_NEWFONT);
                        return TRUE;
                    }
                    if (item >= 0 && item < (int)g_rows.size() &&
                        g_rows[item].model != nullptr &&
                        g_rows[item].model->effectFile().empty()) {
                        SetWindowLongPtrA(dlg, DWLP_MSGRESULT,
                                          CDRF_NOTIFYITEMDRAW);
                        return TRUE;
                    }
                    SetWindowLongPtrA(dlg, DWLP_MSGRESULT, CDRF_DODEFAULT);
                    return TRUE;
                }
                if (draw->nmcd.dwDrawStage ==
                    (CDDS_SUBITEM | CDDS_ITEMPREPAINT)) {
                    int item = (int)draw->nmcd.dwItemSpec;
                    if (draw->iSubItem == kColEffect && item > 0 &&
                        item < (int)g_rows.size() &&
                        g_rows[item].model != nullptr &&
                        g_rows[item].model->effectFile().empty()) {
                        // "(none)" rows gray the Effect File text.
                        draw->clrText = GetSysColor(COLOR_GRAYTEXT);
                        SetWindowLongPtrA(dlg, DWLP_MSGRESULT, CDRF_NEWFONT);
                        return TRUE;
                    }
                    SetWindowLongPtrA(dlg, DWLP_MSGRESULT, CDRF_DODEFAULT);
                    return TRUE;
                }
                SetWindowLongPtrA(dlg, DWLP_MSGRESULT, CDRF_DODEFAULT);
                return TRUE;
            }
            default:
                break;
            }
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        switch (id) {
        case 40006: DlgSubsetExtractFlow(); return TRUE;
        case 40007:
        case kBtnSetFile:
            DlgSetEffectFlow();
            return TRUE;
        case 40012:
        case kBtnRemove:
            DlgRemoveEffectFlow();
            return TRUE;
        case 40013:
        case kBtnHideShow:
            DlgToggleHideShowFlow();
            return TRUE;
        case 40008:
            DlgResetWithDefaultFlow();
            return TRUE;
        case 40009: {   // [FUN_180042f60]
            char path[MAX_PATH];
            if (DlgOpenFileDialog("EMM Files (*.emm)\0*.emm\0All Files (*.*)\0*.*\0",
                                  "Open", path, MAX_PATH)) {
                MmeEmmLoad(path);
                DlgRebuildList();
                DlgAfterEdit();
            }
            return TRUE;
        }
        case 40010: {   // [FUN_1800431e0]
            char path[MAX_PATH];
            if (DlgSaveFileDialog("EMM Files (*.emm)\0*.emm\0All Files (*.*)\0*.*\0",
                                  "Save", "emm", path, MAX_PATH)) {
                MmeEmmSave(path);
            }
            return TRUE;
        }
        case 40016: {   // [FUN_180043420]
            char path[MAX_PATH];
            if (DlgOpenFileDialog("EMD Files (*.emd)\0*.emd\0All Files (*.*)\0*.*\0",
                                  "Open by Model", path, MAX_PATH)) {
                MmeEmmLoad(path);
                DlgRebuildList();
                DlgAfterEdit();
            }
            return TRUE;
        }
        case 40017: {   // [FUN_180043b00]
            char path[MAX_PATH];
            if (DlgSaveFileDialog("EMD Files (*.emd)\0*.emd\0All Files (*.*)\0*.*\0",
                                  "Save by Model", "emd", path, MAX_PATH)) {
                MmeEmmSave(path);
            }
            return TRUE;
        }
        case 40011:
        case 2:
            if (id == 2) {
                // [FUN_180041550] Cancel restores the default-effect
                // snapshot taken at dialog open / last Apply.
                MmeEmmSetDefaultEffect(g_defaultSnapshot);
            }
            MmeDlgCloseAssignmentDialog();
            return TRUE;
        case 1:
            // [OK] the assignments are already live; close without the
            // default-effect revert (the original 1 case falls straight to
            // the working-copy clear + DestroyWindow).
            MmeDlgCloseAssignmentDialog();
            return TRUE;
        case 40014:
            DlgDisassembleFlow();
            return TRUE;
        case 40028:
            DlgToggleSkipValidation(dlg);
            return TRUE;
        case kBtnRefresh:
            // [0x3ed] commit: refresh the default-effect snapshot (the
            // DAT_1800d74d0 copy Cancel would restore) and disable the
            // button until the next edit. The EMM auto-save itself is the
            // per-frame PMM hook (OnBeginScene), not a dialog action.
            g_defaultSnapshot = MmeEmmDefaultEffect();
            g_dirty = false;
            {
                HWND apply = GetDlgItem(dlg, kBtnRefresh);
                if (apply != nullptr) EnableWindow(apply, FALSE);
            }
            return TRUE;
        default:
            break;
        }
        return TRUE;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wParam;
        UINT count = DragQueryFileA(drop, 0xFFFFFFFF, nullptr, 0);
        if (count != 1) {
            DragFinish(drop);
            MessageBoxA(dlg, "Two or more files cannot be specified.",
                        "MikuMikuEffect", MB_ICONERROR);
            return TRUE;
        }
        char path[MAX_PATH];
        path[0] = '\0';
        DragQueryFileA(drop, 0, path, MAX_PATH);
        DragFinish(drop);
        size_t len = strlen(path);
        const char* ext = (len >= 4) ? path + len - 4 : "";
        if (_stricmp(ext, ".emm") == 0) {
            MmeEmmLoad(path);
            DlgRebuildList();
            DlgAfterEdit();
        } else if (_stricmp(ext, ".fx") == 0 || _stricmp(ext, ".fxm") == 0 ||
                   _stricmp(ext, ".fxsub") == 0) {
            // [FUN_180041d10 with a resolved path] validate, then assign.
            char drive[3] = { 0 };
            char dir[0x100] = { 0 };
            char fname[0x100] = { 0 };
            char resolved[0x104];
            if (_splitpath_s(path, drive, sizeof(drive), dir, sizeof(dir),
                             fname, sizeof(fname), nullptr, 0) == 0 &&
                _makepath_s(resolved, sizeof(resolved), drive, dir, fname,
                            nullptr) == 0) {
                DlgAssignEffectFileDirect(resolved);
            } else {
                DlgAssignEffectFileDirect(path);
            }
        } else {
            MessageBoxA(dlg, "Please specify .fx file or .emm file.",
                        "MikuMikuEffect", MB_ICONERROR);
        }
        return TRUE;
    }
    case WM_DESTROY: {
        DragAcceptFiles(dlg, FALSE);
        if (g_getMessageHook != nullptr) {
            UnhookWindowsHookEx(g_getMessageHook);
            g_getMessageHook = nullptr;
        }
        g_hookDialogHwnd = nullptr;
        if (g_dlgWindow == dlg) {
            g_dlgWindow = nullptr;
        }
        g_rows.clear();
        return 0;
    }
    default:
        break;
    }
    // Unhandled messages return FALSE so the dialog manager performs its
    // default processing (nonclient hit-testing, the menu bar, navigation).
    return 0;
}

} // namespace

void MmeDlgOpenAssignmentDialog()
{
    // [FUN_180055b10 0x9c45] existing -> SetFocus; new -> CreateDialogParamA
    // + ShowWindow(SW_SHOW) + UpdateWindow + SetFocus.
    if (g_dlgWindow != nullptr) {
        SetFocus(g_dlgWindow);
        return;
    }
    HWND owner = GetMMDMainWindow();
    g_dlgWindow = CreateDialogParamA(g_hInst, MAKEINTRESOURCEA(108), owner,
                                     MmeAssignmentDlgProc, 0);
    if (g_dlgWindow != nullptr) {
        ShowWindow(g_dlgWindow, SW_SHOW);
        UpdateWindow(g_dlgWindow);
        SetFocus(g_dlgWindow);
    }
}

void MmeDlgCloseAssignmentDialog()
{
    if (g_dlgWindow != nullptr) {
        HWND dlg = g_dlgWindow;
        g_dlgWindow = nullptr;
        g_hookDialogHwnd = nullptr;
        DestroyWindow(dlg);
    }
}

// [OK / Apply] commit the live mapping: when EMMAutoSave is enabled and a
// PMM project is loaded, write the mapping file next to it.
void MmeDlgCommitAssignment()
{
    if (g_emmAutoSave == 0) {
        return;
    }
    const wchar_t* pmm = SavedPMMFile();
    while (pmm != nullptr) {
        MmeAutoSaveEmmForPmm(pmm);
        pmm = SavedPMMFile();
    }
}

// [per-frame FUN_18003fff0(dlg,0) trigger] pick up models registered while
// the dialog is open (the Hatsune-model-missing-from-the-list case); called
// from the frame tick. The rebuild keeps the expanded set.
void MmeDlgRefreshIfModelCountChanged()
{
    if (g_dlgWindow == nullptr) {
        return;
    }
    MmeContext* ctx = MmeGetContext();
    size_t count = (ctx != nullptr) ? ctx->models.size() : 0;
    if (count != g_lastModelCount) {
        g_lastModelCount = count;
        DlgRebuildList();
    }
}

} // namespace mme
