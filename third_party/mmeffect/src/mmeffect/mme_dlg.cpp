#include "mme_resources.h"
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
// Faithful behaviors (decompile-derived; re-audited 2026-09):
//   - The list keeps a parallel row table (the original DAT_1800d9ce8):
//     {object pointer (null = the "(default)" row), subset index (-1 = whole
//     object)}. The "(default)" row is always row 0 and carries NO checkbox
//     (the original sets its state image to 0 in FUN_18003fff0).
//   - The tab control 1002 is the render-target selector, and FUN_180041620's
//     gate (*param_2) is the CURRENT SELECTED tab item's lParam, read via
//     TCM_GETCURSEL + TCM_GETITEM(TCIF_PARAM) (0x130B + 0x1305 - the binary
//     never sends TCM_GETITEMRECT there). WM_INITDIALOG inserts the sole
//     fixed "Main" item with lParam 0, FUN_18003fff0 inserts one item per
//     offscreen object (lParam = the object pointer, mask 9) and rebuilds
//     them when the object set changes. With Main selected (gate 0) the
//     "(default)" row JOINS every collection as {null, -1} whenever
//     onlyWholeObjectRows (param_5) is false - the param_4 render-class
//     filter never applies to row 0 - so it is selectable, enables the
//     object-row commands, and FUN_180041b00 writes the picked path to the
//     EMM default effect for it (gate != 0 skips row 0 entirely).
//   - [2026-09 offscreen tabs] The port now carries the offscreen tab set
//     (one tab per queued 0x2E resource, carried by the renderPassList item
//     itself in renderPassList order - the original's manager
//     +0x138 vector rebuilt by sub_18002BAA0): tab text = the resource NAME
//     (the original wrapper +0x8), the 1004 caption = "Main Render Target"
//     (0x1800b4e68) / the Description annotation (inner record +0x50), the
//     per-tab effect column / checkbox read the resource's DefaultEffect
//     rows using the render resolver (ordered wildcards and carrier "self"),
//     the "(default)" row shows the "key=value; "-joined
//     rows or "*=none;". Manual edits write independent per-object/subset
//     path and visibility overrides shared with the render-time resolver.
//     Annotation defaults remain unchanged. Tab identity is a stable id
//     in lParam (the original's wrapper pointer); the set is re-synced when
//     the plan's offscreen associations change (the original: every plan
//     rebuild with planDirty set, sub_18002BAA0's tail; the port: a
//     per-frame signature poll).
//   - LVN_ITEMCHANGED (-101), guarded by DAT_1800d99db while the list is
//     being rebuilt: a state-image change runs the FUN_180042610 port (write
//     shown, latch DAT_1800d9f4b); a selection/focus change (mask 3) runs the
//     full menu/button sync.
//   - The sync (FUN_180041aa0 probes): 40006/40007/40012/40008 + buttons
//     1007/1008 <- FUN_180041aa0(1) "any selected normal-object row" (the
//     "(default)" row counts toward this probe - see the gate note above);
//     40013 + button 1011 <- FUN_180041aa0(0) "any selected row"; 40016 <-
//     count(whole-object normal rows) != 0; 40017 <- count == 1; the 40006
//     checkmark <- FUN_1800429a0 (every selected row expanded; null entries
//     are skipped - only the lone "(default)" row keeps it off); 40014 <-
//     FUN_180043fd0 (the FIRST selected row carries an effect path; the
//     "(default)" row disables it). The dialog
//     resource pre-marks the Edit items INACTIVE; the runtime MF_ENABLED
//     overrides the preset.
//   - NM_DBLCLK on a row runs the Set Effect flow (the original -3 handler;
//     double-clicking the "(default)" row picks + writes the default
//     effect).
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
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "effect_engine.h"
#include "emm_manager.h"
#include "material_bind.h"   // MmeActiveModelBinding (offscreen tab rebuild)
#include "mme_context.h"
#include "mme_globals.h"
#include "mme_log.h"
#include "mme_ui.h"
#include "mme_util.h"
#include "model_data.h"
#include "sas_exec.h"
#include "sas_interpreter.h"

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

// --- [sub_18003FFF0 0x1800400ce-0x18004043f] offscreen 渲染目标标签页集 ---
// 原版为每个 offscreen 维护一个 0x48 字节的包装对象（+0 = 内层 SAS 记录、
// +8 = 名字（标签页标题）、+0x38 = id、+0x50 在内层记录上 = Description 注解
// （0x18004044e 处 `mov r8,[rbx]; add r8,50h` 的标签 1004 文本））。sub_18002CA80
// 在计划构建时把它们注册进 manager 的 id 树，sub_18002BAA0 每次重建时从树里
// 重灌 manager+0x138 向量，并在 planDirty（manager+0x90）置位且对话框打开时于
// 尾部调 sub_18003FFF0(dlg,0)。
// 移植端对应物：renderPassList 队列项直接携带的 0x2E 资源记录（wrapper+0 的
// 2026-09-15 重构产物）—— 每个排队 offscreen 资源一个 {SasEffect*, 资源序号}；
// 标签页顺序跟随 ctx->renderPassList（原版按队列顺序收集 id）。
struct OffscreenTab {
    SasEffect* sas;        // 持有该 offscreen 资源的效果
    ModelData* owner;      // scene carrier: the DefaultEffect "self" identity
    int        resIndex;   // sas->resources[] 槽位（OFFSCREENRENDERTARGET 记录）
    size_t     id;         // 稳定身份（lParam），跨重建对应原版的包装对象指针
};
std::vector<OffscreenTab> g_offscreenTabs;
// (效果, 资源) -> 稳定 id 的分配表（原版：id 树中复用的包装对象）。
std::map<std::pair<SasEffect*, int>, size_t> g_offscreenIds;
size_t g_offscreenSignature = static_cast<size_t>(-1);   // per-frame 轮询签名
size_t g_nextOffscreenId = 1;

HWND g_dlgWindow = nullptr;      // the modeless dialog handle (DAT_1800d9a48)
std::vector<ModelData*> g_lastModelOrder;
bool g_dirty = false;            // pending edits -> Apply enables
bool g_skipValidation = false;   // DAT_1800d99d9 ([System] SkipValidation)
bool g_clickLatch = false;       // DAT_1800d9f4b (swallow the post-checkbox click)
bool g_rebuilding = false;       // DAT_1800d99db (suppress LVN_ITEMCHANGED while rebuilding)
bool g_selectedObject = false;   // DAT_1800d9f48
bool g_selectedAny = false;      // DAT_1800d9f49
bool g_subsetCheck = false;      // DAT_1800d9f4a (the Subset-Extract checkmark)
std::string g_lastFxDir;         // DAT_1800da130 (the open-dialog initial dir)
std::string g_lastUserFileDir;   // DAT_1800d7528 (the EMD dialogs' shared
                                 // dir; the original one-shot seeds it from
                                 // the exe-dir global D7550 + the literal
                                 // "UserFile" [a plain string concat via
                                 // sub_180005760, NOT an ini read], then
                                 // truncates to the dir after each pick;
                                 // the port keeps the documented no-seed
                                 // divergence here)
std::string g_lastEmmDir;        // DAT_1800d7500 (the EMM open/save dialogs'
                                 // InitialDir; same one-shot seed and
                                 // post-pick truncation, shared by 40009
                                 // and 40010 [FUN_180042F60/FUN_1800431E0])
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

const char* EffectTextForModel(ModelData* model, int subset = -1)
{
    static std::string text;
    if (model == nullptr) {
        text = "(default)";
        return text.c_str();
    }
    if (!MmeEmmEffectiveSubsetShown(model, subset)) {
        text = "(hide)";
        return text.c_str();
    }
    const auto found = model->subsetEffects().find(subset);
    const std::string& assigned = found != model->subsetEffects().end()
        ? found->second : model->effectFile();
    if (!assigned.empty()) {
        text = assigned;
        return text.c_str();
    }
    text = "(none)";
    return text.c_str();
}

// --- [sub_18003FFF0 标签页半 + sub_18002CA80/sub_18002BAA0 收集] offscreen 辅助 ---

// 前置声明（定义在下方选中行收集一节）。
LONG_PTR DlgTabSelParamGate();

// 取路径的 basename+扩展名（行键匹配的第二规则）。
// [sub_18002CA80 收集 + sub_18002BAA0 0x18002badc-0x18002c60 重灌] 依当前
// pass 计划重建 offscreen 标签页集：renderPassList 顺序，每条队列项携带的 0x2E
// 资源关联一个条目，按 (效果, 资源) 去重（首个出现优先），并为新见到的 (效果,
// 资源) 分配稳定 id、抓拍 DefaultEffect 行快照。
void DlgCollectOffscreenTabs(std::vector<OffscreenTab>& out)
{
    out.clear();
    MmeContext* ctx = MmeGetContext();
    if (ctx == nullptr) {
        return;
    }
    for (size_t i = 0; i < ctx->renderPassList.size(); ++i) {
        const MmeRenderPassItem& item = ctx->renderPassList[i];
        // [2026-09-15 per-resource queue rework] 队列项直接携带 0x2E 资源记
        // 录（wrapper+0 等价物）；(效果, 资源下标) 由载体绑定反推。
        SasResource* res = item.offscreen;
        MaterialBinding* binding = item.assignment;
        if (res == nullptr || binding == nullptr || binding->sas == nullptr) {
            continue;
        }
        SasEffect* sas = binding->sas;
        if (sas->resources.empty() ||
            res < &sas->resources[0] ||
            res >= &sas->resources[0] + sas->resources.size()) {
            continue;
        }
        int resIndex = (int)(res - &sas->resources[0]);
        if (sas->resources[resIndex].semanticId != 0x2E) {
            continue;   // 仅 OFFSCREENRENDERTARGET 记录
        }
        bool dup = false;
        for (size_t j = 0; j < out.size(); ++j) {
            if (out[j].sas == sas && out[j].resIndex == resIndex) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        std::pair<SasEffect*, int> key(sas, resIndex);
        size_t id;
        std::map<std::pair<SasEffect*, int>, size_t>::iterator idIt =
            g_offscreenIds.find(key);
        if (idIt != g_offscreenIds.end()) {
            id = idIt->second;
        } else {
            id = g_nextOffscreenId++;
            g_offscreenIds[key] = id;
            // 首次见到：抓拍注解行快照（恢复“显示”时还原注解原值）。
        }
        OffscreenTab tab;
        tab.sas = sas;
        tab.owner = item.carrier;
        tab.resIndex = resIndex;
        tab.id = id;
        out.push_back(tab);
    }
}

// 当前选中标签页对应的 offscreen（Main / 失效选中 -> null）。lParam 即稳定 id
// （原版为包装对象指针；0x1800400ce-0x180040104 处失效选中被折回 0 = Main）。
OffscreenTab* DlgCurrentOffscreen()
{
    LONG_PTR gate = DlgTabSelParamGate();
    if (gate <= 0) {
        return nullptr;
    }
    for (size_t i = 0; i < g_offscreenTabs.size(); ++i) {
        if ((LONG_PTR)g_offscreenTabs[i].id == gate) {
            return &g_offscreenTabs[i];
        }
    }
    return nullptr;
}

// 当前标签页的 DefaultEffect 行集（可写 - 直接落在 SasEffect 的注解行上，
// 运行期 offscreen 解析每 pass 都读它，改派即时生效）。
std::vector<std::pair<std::string, std::string>>* DlgOffscreenRows(OffscreenTab* tab)
{
    if (tab == nullptr || tab->sas == nullptr ||
        tab->resIndex < 0 || tab->resIndex >= (int)tab->sas->resources.size()) {
        return nullptr;
    }
    return &tab->sas->resources[tab->resIndex].defaultEffectMap;
}

// [sub_18002DB80 读取路径] 首个与对象文件匹配的行。
SasResource& DlgResource(OffscreenTab* tab)
{
    return tab->sas->resources[tab->resIndex];
}

void DlgUpsertOffscreenRow(OffscreenTab* tab, ModelData* model,
                         const char* value, int subset = -1)
{
    DlgResource(tab).effectOverrides[{model->objectId(), subset}] = value;
}

void DlgRemoveOffscreenRows(OffscreenTab* tab, ModelData* model)
{
    auto& resource = DlgResource(tab);
    for (auto it = resource.effectOverrides.begin(); it != resource.effectOverrides.end();)
        if (it->first.first == model->objectId()) it = resource.effectOverrides.erase(it);
        else ++it;
    for (auto it = resource.shownOverrides.begin(); it != resource.shownOverrides.end();)
        if (it->first.first == model->objectId()) it = resource.shownOverrides.erase(it);
        else ++it;
}

const char* DlgOffscreenRowText(OffscreenTab* tab, ModelData* model, int subset = -1)
{
    auto& resource = DlgResource(tab);
    if (!MmeOffscreenObjectShown(resource, model, tab->owner, subset)) return "(hide)";
    const auto* value = MmeOffscreenEffectValue(resource, model, tab->owner, subset);
    if (!value || *value == "none" || *value == "hide") return "(none)";
    if (*value == "main_default") return EffectTextForModel(model, subset);
    return value->c_str();
}

std::string DlgOffscreenDefaultRowText(OffscreenTab* tab)
{
    std::vector<std::pair<std::string, std::string>>* rows = DlgOffscreenRows(tab);
    std::string text;
    if (rows != nullptr) {
        for (size_t i = 0; i < rows->size(); ++i) {
            text += (*rows)[i].first;
            text += "=";
            text += (*rows)[i].second;
            text += "; ";
        }
    }
    if (text.empty()) {
        text = "*=none;";
    }
    return text;
}

int DlgRowCheckState(ModelData* model, int subset = -1)
{
    OffscreenTab* tab = DlgCurrentOffscreen();
    const bool shown = tab
        ? MmeOffscreenObjectShown(DlgResource(tab), model, tab->owner, subset)
        : MmeEmmEffectiveSubsetShown(model, subset);
    return shown ? 2 : 1;
}

bool DlgRowEffectIsNone(ModelData* model)
{
    OffscreenTab* off = DlgCurrentOffscreen();
    if (off != nullptr) {
        return _stricmp(DlgOffscreenRowText(off, model), "(none)") == 0;
    }
    return model->effectFile().empty();
}

// [sub_18003FFF0 0x180040445-0x18004046b] 标签页说明文字（1004 静态控件）：
// Main 恒为 "Main Render Target"（0x1800b4e68，无语言分支）；offscreen 页为
// 内层记录 +0x50 的 Description 注解。
void DlgRefreshCaption()
{
    if (g_dlgWindow == nullptr) {
        return;
    }
    OffscreenTab* tab = DlgCurrentOffscreen();
    const char* text = "Main Render Target";
    if (tab != nullptr && tab->sas != nullptr &&
        tab->resIndex >= 0 && tab->resIndex < (int)tab->sas->resources.size()) {
        text = tab->sas->resources[tab->resIndex].description.c_str();
    }
    SetDlgItemTextA(g_dlgWindow, kDlgTabCaption, text);
}

// [sub_18003FFF0 0x1800400ce-0x18004043f] 标签页重建：现存标签 lParam 集与
// 新 offscreen 集不一致时删除 1..N-1 重插（文本 = 资源名 +0x8，mask 9），并
// 恒等身份恢复选中（0x1800403e9 TCM_SETCURSEL；选中丢失回落 0 = Main，
// 0x18004043f）。随后刷新说明文字。
void DlgSyncTabs()
{
    if (g_dlgWindow == nullptr) {
        return;
    }
    HWND tab = GetDlgItem(g_dlgWindow, kDlgTab);
    if (tab == nullptr) {
        return;
    }
    DlgCollectOffscreenTabs(g_offscreenTabs);

    LRESULT count = SendMessageA(tab, TCM_GETITEMCOUNT, 0, 0);
    bool same = count == (LRESULT)(g_offscreenTabs.size() + 1);
    if (same) {
        for (size_t i = 0; i < g_offscreenTabs.size(); ++i) {
            TCITEMA item;
            memset(&item, 0, sizeof(item));
            item.mask = TCIF_PARAM;
            if (SendMessageA(tab, TCM_GETITEMA, (WPARAM)(i + 1),
                             (LPARAM)&item) == 0 ||
                item.lParam != (LPARAM)g_offscreenTabs[i].id) {
                same = false;
                break;
            }
        }
    }
    if (!same) {
        // 记住当前选中身份，重插后恢复（0x180040387-0x180040424）。
        LRESULT cursel = SendMessageA(tab, TCM_GETCURSEL, 0, 0);
        LONG_PTR wantedId = 0;
        if (cursel > 0) {
            TCITEMA item;
            memset(&item, 0, sizeof(item));
            item.mask = TCIF_PARAM;
            if (cursel < count &&
                SendMessageA(tab, TCM_GETITEMA, (WPARAM)cursel,
                             (LPARAM)&item) != 0) {
                wantedId = item.lParam;
            }
        }
        while (SendMessageA(tab, TCM_GETITEMCOUNT, 0, 0) > 1) {
            SendMessageA(tab, TCM_DELETEITEM, 1, 0);
        }
        LRESULT restore = 0;
        for (size_t i = 0; i < g_offscreenTabs.size(); ++i) {
            const SasResource& res = g_offscreenTabs[i].sas->resources[
                g_offscreenTabs[i].resIndex];
            TCITEMA item;
            memset(&item, 0, sizeof(item));
            item.mask = TCIF_TEXT | TCIF_PARAM;
            item.pszText = const_cast<LPSTR>(res.name.c_str());
            item.lParam = (LPARAM)g_offscreenTabs[i].id;
            SendMessageA(tab, TCM_INSERTITEMA, (WPARAM)(i + 1), (LPARAM)&item);
            if ((LONG_PTR)g_offscreenTabs[i].id == wantedId) {
                restore = (LRESULT)(i + 1);
            }
        }
        SendMessageA(tab, TCM_SETCURSEL, restore, 0);
    }
    // Stable tab IDs remain across dialog rebuilds. Assignment overrides live
    // on the resource, not on this dialog's lifetime.

}

// per-frame 轮询签名：offscreen 关联变化（计划重建 / 效果重载）时触发标签页
// 与行文本重建（原版由 sub_18002BAA0 尾部的 planDirty 分支触发）。
size_t DlgOffscreenSignature()
{
    MmeContext* ctx = MmeGetContext();
    if (ctx == nullptr) {
        return 0;
    }
    size_t hash = ctx->renderPassList.size() * 0x9E3779B97F4A7C15ULL;
    for (size_t i = 0; i < ctx->renderPassList.size(); ++i) {
        // [2026-09-15 per-resource queue rework] 队列项即 offscreen 关联：
        // 哈希 (载体, 资源) 对。
        const MmeRenderPassItem& item = ctx->renderPassList[i];
        hash = hash * 0x9E3779B97F4A7C15ULL ^
               (size_t)item.carrier ^ ((size_t)item.offscreen << 8);
    }
    return hash;
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
// The "(default)" row (row 0) collects as {null, -1} when the tab gate (the
// selected tab item's lParam; 0 = Main) is zero AND onlyWholeObjectRows is
// false - param_4's render-class filter NEVER applies to row 0 in the
// original (it only guards the object-row path at LABEL_8).
struct SelectedRef {
    ModelData* model;   // null = the (default) row
    int subset;
};

// [FUN_180041620 @0x180041670-0x1800416bc] the collection gate: the CURRENT
// SELECTED tab item's lParam, read via TCM_GETCURSEL + TCM_GETITEM with
// TCIF_PARAM. The original never queries TCM_GETITEMRECT here (no 0x130A
// anywhere in the message flow). WM_INITDIALOG inserts the sole fixed "Main"
// item with lParam 0, so Main leaves the gate 0; an object tab (lParam = the
// offscreen object pointer, inserted by FUN_18003fff0) gates the "(default)"
// row out of every collection. Returns 0 on any failure, matching the
// original's zeroed-then-conditionally-filled TCITEM out value.
LONG_PTR DlgTabSelParamGate()
{
    HWND tab = GetDlgItem(g_dlgWindow, kDlgTab);
    if (tab == nullptr) {
        return 0;
    }
    LRESULT cursel = SendMessageA(tab, TCM_GETCURSEL, 0, 0);
    TCITEMA item;
    memset(&item, 0, sizeof(item));
    item.mask = TCIF_PARAM;
    if (SendMessageA(tab, TCM_GETITEMA, (WPARAM)cursel, (LPARAM)&item) == 0) {
        return 0;
    }
    return item.lParam;
}

bool DlgCollectSelected(bool onlyNormalObject, bool onlyWholeObjectRows,
                        std::vector<SelectedRef>& out)
{
    out.clear();
    HWND list = DlgList();
    if (list == nullptr) {
        return false;
    }
    bool defaultRowLive = DlgTabSelParamGate() == 0;
    int index = (int)SendMessageA(list, LVM_GETNEXTITEM, (WPARAM)-1,
                                  MAKELPARAM(LVNI_SELECTED, 0));
    while (index != -1) {
        if (index >= 0 && index < (int)g_rows.size()) {
            const RowRef& row = g_rows[index];
            if (row.model == nullptr) {
                if (defaultRowLive && !onlyWholeObjectRows) {
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
// [sub_18003FFF0 0x18004149f] offscreen 页取行值：none/hide/main_default 与
// "(none)" 不算路径，其余（绝对路径）作为 Disassemble 的输入。
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
    OffscreenTab* off = DlgCurrentOffscreen();
    if (off != nullptr) {
        const char* value = DlgOffscreenRowText(off, model, g_rows[index].subset);
        if (value == nullptr || value[0] == '\0' ||
            _stricmp(value, "(none)") == 0 ||
            _stricmp(value, "none") == 0 ||
            _stricmp(value, "hide") == 0 ||
            _stricmp(value, "main_default") == 0) {
            return std::string();
        }
        return std::string(value);
    }
    return model->effectFile();
}

// [FUN_1800429a0] the Subset-Extract checkmark: every selected row's object
// is in the expanded set. A null (default-row) entry is SKIPPED by the
// original's loop - only a collection holding the lone "(default)" row
// (single entry, null object) or an unexpanded object leaves it off.
bool DlgExpandedProbe()
{
    std::vector<SelectedRef> refs;
    DlgCollectSelected(true, false, refs);
    if (refs.empty()) {
        return false;
    }
    if (refs.size() == 1 && refs[0].model == nullptr) {
        return false;
    }
    for (size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].model == nullptr) {
            continue;
        }
        if (g_expanded.find(refs[i].model) == g_expanded.end()) {
            return false;
        }
    }
    return true;
}

void DlgRebuildList(bool preserveSelection = false);
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
// Visibility is independent of the assigned path and belongs to the selected
// target/object/subset. A checkbox notification changes only the clicked row.
void DlgApplyShown(bool shown, int singleItem)
{
    OffscreenTab* off = DlgCurrentOffscreen();
    std::vector<SelectedRef> refs;
    if (singleItem >= 0 && singleItem < (int)g_rows.size()) {
        refs.push_back({g_rows[singleItem].model, g_rows[singleItem].subset});
    } else {
        DlgCollectSelected(false, false, refs);
    }
    for (size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].model == nullptr) {
            continue;
        }
        if (off != nullptr) {
            DlgResource(off).shownOverrides[{refs[i].model->objectId(), refs[i].subset}] = shown;
            continue;
        }
        if (refs[i].subset >= 0)
            MmeEmmSetSubsetShown(refs[i].model, refs[i].subset, shown);
        else
            refs[i].model->setShown(shown);
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

// [FUN_180041b00] the effect-text writer: object rows write their assignment;
// the "(default)" row writes the EMM default effect - the original only
// reaches that branch while the tab gate is ZERO (0x180041b8c: gate != 0
// skips the entry outright; collection above already enforces this), and
// selecting Main + the (default) row then sets the default through the same
// Set Effect File flow.
// Offscreen edits target an object/subset assignment, never annotation rows.
void DlgWriteEffectForSelection(const char* path)
{
    OffscreenTab* off = DlgCurrentOffscreen();
    std::vector<SelectedRef> refs;
    DlgCollectSelected(false, false, refs);
    bool touchedDefault = false;
    bool touchedOffscreen = false;
    for (size_t i = 0; i < refs.size(); ++i) {
        ModelData* model = refs[i].model;
        if (off != nullptr) {
            if (model == nullptr) {
                continue;   // offscreen 页 "(default)" 行已被收集门挡住，保险再挡一次
            }
            if (path != nullptr && path[0] != '\0') {
                DlgUpsertOffscreenRow(off, model, path, refs[i].subset);
            } else {
                DlgUpsertOffscreenRow(off, model, "none", refs[i].subset);
            }
            touchedOffscreen = true;
            continue;
        }
        if (model == nullptr) {
            MmeEmmSetDefaultEffect(path != nullptr ? path : "");
            touchedDefault = true;
            continue;
        }
        MmeAssignEffect(model->objectId(), refs[i].subset, path);
        if (path && path[0] != 0) {
            if (refs[i].subset >= 0) MmeEmmSetSubsetShown(model, refs[i].subset, true);
            else model->setShown(true);
        }
    }
    if (touchedDefault || touchedOffscreen) {
        DlgRefreshTexts();
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
    // Main displays the global default; target tabs display annotation rows.
    OffscreenTab* off = DlgCurrentOffscreen();
    if (off != nullptr) {
        std::string text = DlgOffscreenDefaultRowText(off);
        DlgSetRowText(list, index, kColEffect, text.c_str());
    } else {
        const std::string& defaultEffect = MmeEmmDefaultEffect();
        DlgSetRowText(list, index, kColEffect,
                      defaultEffect.empty() ? "(none)" : defaultEffect.c_str());
    }
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

    std::wstring display = model->displayFilename().empty()
        ? MmeAnsiToWide(model->filename()) : model->displayFilename();
    const auto slash = display.find_last_of(L"\\/");
    if (slash != std::wstring::npos) display.erase(0, slash + 1);
    LVITEMW item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = (int)g_rows.size();
    item.pszText = const_cast<LPWSTR>(display.c_str());
    item.lParam = (LPARAM)g_rows.size();
    int index = (int)SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
    if (index < 0) {
        return;
    }
    g_rows.push_back(row);
    // [sub_18003FFF0] 每个标签页的对象行集相同（sub_18002E5F0 不按目标过滤），
    // 仅效果列/复选框随目标变化（sub_18002DB80 按 targetId 查询）。
    OffscreenTab* off = DlgCurrentOffscreen();
    DlgSetRowText(list, index, kColEffect,
                  off != nullptr ? DlgOffscreenRowText(off, model)
                                 : EffectTextForModel(model));
    // LVS_EX_CHECKBOXES re-initializes the state image at insert time; apply
    // the checked state after the insert (checked = shown).
    DlgSetRowCheck(list, index, DlgRowCheckState(model));

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
            // [sub_18002DB80 0x18002dbc1 回退] 子集行显示对象级值。
            DlgSetRowText(list, subIndex, kColEffect,
                          off != nullptr ? DlgOffscreenRowText(off, model, m)
                                         : EffectTextForModel(model, m));
            DlgSetRowCheck(list, subIndex, DlgRowCheckState(model, m));
        }
    }
}

std::vector<ModelData*> DlgOrderedModels()
{
    MmeContext* ctx = MmeGetContext();
    if (ctx == nullptr) return {};

    std::map<unsigned long long, int> drawOrders;
    for (int index = 0; index < ExpGetPmdNum(); ++index) {
        drawOrders[reinterpret_cast<unsigned long long>(ExpGetPmdID(index))] =
            ExpGetPmdOrder(index);
    }
    for (int index = 0; index < ExpGetAcsNum(); ++index) {
        const int order = ExpGetAcsOrder(index);
        drawOrders[reinterpret_cast<unsigned long long>(ExpGetAcsID(index))] =
            order < 0 ? -order : order;
    }

    std::vector<ModelData*> models;
    for (ModelData* model : ctx->models) {
        if (model != nullptr) models.push_back(model);
    }
    std::stable_sort(models.begin(), models.end(), [&drawOrders](ModelData* left, ModelData* right) {
        if (left->renderClass() != right->renderClass()) {
            return left->renderClass() < right->renderClass();
        }
        const auto leftOrder = drawOrders.find(left->objectId());
        const auto rightOrder = drawOrders.find(right->objectId());
        if (leftOrder == drawOrders.end()) return false;
        if (rightOrder == drawOrders.end()) return true;
        return leftOrder->second < rightOrder->second;
    });
    return models;
}

void DlgRebuildList(bool preserveSelection)
{
    HWND list = DlgList();
    if (list == nullptr) {
        return;
    }
    std::vector<RowRef> selected;
    if (preserveSelection) {
        int index = (int)SendMessageA(list, LVM_GETNEXTITEM, (WPARAM)-1,
                                      MAKELPARAM(LVNI_SELECTED, 0));
        while (index != -1) {
            if (index >= 0 && index < (int)g_rows.size()) selected.push_back(g_rows[index]);
            index = (int)SendMessageA(list, LVM_GETNEXTITEM, (WPARAM)index,
                                      MAKELPARAM(LVNI_SELECTED, 0));
        }
    }
    g_rebuilding = true;   // DAT_1800d99db
    SendMessageA(list, WM_SETREDRAW, FALSE, 0);
    SendMessageA(list, LVM_DELETEALLITEMS, 0, 0);
    g_rows.clear();

    // The "(default)" row is always row 0 (the original lists it first).
    DlgAddDefaultRow(list);
    g_lastModelOrder = DlgOrderedModels();
    for (ModelData* model : g_lastModelOrder) DlgAddObjectRow(list, model);
    for (size_t index = 0; index < g_rows.size(); ++index) {
        const RowRef& row = g_rows[index];
        if (std::find_if(selected.begin(), selected.end(), [&row](const RowRef& old) {
                return old.model == row.model && old.subset == row.subset;
            }) != selected.end()) {
            LVITEMA item = {};
            item.stateMask = LVIS_SELECTED;
            item.state = LVIS_SELECTED;
            SendMessageA(list, LVM_SETITEMSTATE, index, (LPARAM)&item);
        }
    }
    SendMessageA(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, FALSE);
    g_rebuilding = false;
    if (preserveSelection) DlgSyncSelectionState();
}

// [FUN_18003fff0-lite] refresh the visible Effect File texts and the
// checkbox images from the working state. Guarded by DAT_1800d99db so the
// state writes cannot re-enter the checkbox handler (the hide/show flicker).
// [sub_18003FFF0 0x180040bbc] 行集不变时（换页/改派后）仅按当前目标重写每行
// 文本与状态图 —— 原版对每个既有条目重发 LVM_SETITEMTEXT(0x102E)。
void DlgRefreshTexts()
{
    HWND list = DlgList();
    if (list == nullptr) {
        return;
    }
    g_rebuilding = true;
    OffscreenTab* off = DlgCurrentOffscreen();
    for (size_t i = 0; i < g_rows.size(); ++i) {
        ModelData* model = g_rows[i].model;
        if (model == nullptr) {
            if (off != nullptr) {
                std::string text = DlgOffscreenDefaultRowText(off);
                DlgSetRowText(list, (int)i, kColEffect, text.c_str());
            } else {
                const std::string& defaultEffect = MmeEmmDefaultEffect();
                DlgSetRowText(list, (int)i, kColEffect,
                              defaultEffect.empty() ? "(none)" : defaultEffect.c_str());
            }
            DlgSetRowCheck(list, (int)i, 0);
            continue;
        }
        DlgSetRowText(list, (int)i, kColEffect,
                      off != nullptr ? DlgOffscreenRowText(off, model, g_rows[i].subset)
                                     : EffectTextForModel(model, g_rows[i].subset));
        DlgSetRowCheck(list, (int)i, DlgRowCheckState(model, g_rows[i].subset));
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
    // [FUN_180041aa0(1) @0x180044707] the "(default)" row counts toward the
    // same probe as the normal-object rows (FUN_180041620's param_4 filter
    // never applies to row 0), so selecting it while Main is selected
    // enables 40006/40007/40012/40008 + buttons 1007/1008 together.
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

// [FUN_180042F60 / FUN_1800431E0 seed block] both EMM handlers one-shot seed
// D7500 while its size field (D7510) is still zero: D7500 = the exe-dir
// global (DAT_1800d7550) + the literal "UserFile" - a plain std::string
// concat (sub_180005760 = append + append; there is no ini read anywhere in
// the block, and MMEffect.dll imports no profile API). The first dialog's
// InitialDir is therefore "<exedir>\UserFile", usually nonexistent, so the
// common dialog falls back to the current directory.
static const char* DlgEmmInitialDir()
{
    if (g_lastEmmDir.empty())
        g_lastEmmDir = g_exeDir + "UserFile";
    return g_lastEmmDir.c_str();
}

// [FUN_180042F60 / FUN_1800431E0 success tail] sub_180029CD0(assign the full
// picked path) + sub_180005C30(erase at ofn.nFileOffset): the picked path
// truncated to its directory, keeping the trailing backslash (the same
// drive+dir result the EMD handlers reach via _splitpath_s/_makepath_s).
static void DlgRememberPickDir(const char* path, std::string& dir)
{
    char drive[3] = { 0 };
    char dirBuf[0x100] = { 0 };
    if (_splitpath_s(path, drive, sizeof(drive), dirBuf, sizeof(dirBuf),
                     nullptr, 0, nullptr, 0) == 0) {
        char full[0x104];
        if (_makepath_s(full, sizeof(full), drive, dirBuf,
                        nullptr, nullptr) == 0) {
            dir = full;
        }
    }
}

bool DlgOpenFileDialog(const char* filter, const char* title, char* out, DWORD outLen,
                       const char* initialDir = nullptr)
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
    ofn.lpstrInitialDir = initialDir;
    // [FUN_180041D10 @0x18004212F / FUN_180042F60 @0x18004310C /
    // FUN_180043420 @0x18004382D] Flags = 0x1004 with NO OFN_NOCHANGEDIR:
    // the original lets the open dialog move the process CWD to the picked
    // folder, and that side effect feeds every later relative-path
    // resolution (MmeEmmSave's _fullpath absolutization included).
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    return GetOpenFileNameA(&ofn) != FALSE;
}

bool DlgSaveFileDialog(const char* filter, const char* title, const char* defExt,
                       char* out, DWORD outLen, const char* initialDir = nullptr)
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
    ofn.lpstrInitialDir = initialDir;
    // [FUN_1800431E0 @0x180043380 / FUN_180043B00 @0x180043F1D] Flags =
    // 0x80A = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST:
    // the original save dialogs DO carry OFN_NOCHANGEDIR (they never touch
    // the CWD) and have no OFN_HIDEREADONLY.
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
    return GetSaveFileNameA(&ofn) != FALSE;
}

// [FUN_18001e4e0 + FUN_180041d10] load the effect through the engine (the
// SkipValidation gate lives inside the engine); the failure path reports and
// the assignment is not written. A load that SUCCEEDS but whose
// ScriptOrder is not "standard" (the loader's +0x34 kind: 0 = object-
// assignable, 1 = preprocess, 2 = postprocess) is REJECTED with the
// "Pre/Post Effect cannot be specified: <path>" MessageBox (0x180041d10
// @0x180042330-0x180042415): MessageBoxA(dlg, msg, "MikuMikuEffect",
// MB_ICONERROR); the label is "Post " for kind 2 and "Pre" otherwise
// (0x1800b4f90/0x1800b4f98; Japanese "ポスト"/"プリ" 0x1800b4f60/f68 and
// "エフェクトは指定できません: " 0x1800b4f70).
bool DlgValidateEffectPath(const char* path)
{
    MmeContext* ctx = MmeGetContext();
    IDirect3DDevice9* device = (ctx != nullptr) ? ctx->device : nullptr;
    std::shared_ptr<LoadedEffect> loaded = MmeEngineLoadEffectFile(device, path);
    if (loaded != nullptr && loaded->effect != nullptr) {
        int scriptOrder = SasGetScriptOrder(loaded->sas);
        if (scriptOrder != kSasOrderStandard) {
            std::string message;
            if (MmeIsEnglishUiMode()) {
                message = (scriptOrder == kSasOrderPostprocess) ? "Post " : "Pre";
                message += "Effect cannot be specified: ";
            } else {
                // Shift-JIS: "ポスト" / "プリ" + "エフェクトは指定できません: "
                message = (scriptOrder == kSasOrderPostprocess)
                              ? "\x83\x7C\x83\x58\x83\x67"
                              : "\x83\x76\x83\x8A";
                message += "\x83\x47\x83\x74\x83\x46\x83\x4E\x83\x67\x82\xCD"
                           "\x8E\x77\x92\xE8\x82\xC5\x82\xAB\x82\xDC\x82\xB9"
                           "\x82\xF1\x3A\x20";
            }
            message += path;
            MessageBoxA(g_dlgWindow, message.c_str(), "MikuMikuEffect",
                        MB_ICONERROR);
            return false;
        }
        return true;
    }
    const std::string& errors =
        (loaded != nullptr) ? loaded->errorText : std::string();
    // [FUN_180041d10 validate tail / 0x18000baac] prefix + path are directly
    // concatenated (no newline after "Failed to load effect file:"), then
    // "\n\n" + the error text.
    std::string message = "Failed to load effect file:";
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
    DlgCollectSelected(false, false, refs);
    if (refs.empty()) {
        return;   // the original's empty-collection no-op
    }

    // The initial path: the first selected object's current assignment (the
    // original seeds the dialog with the row's text), else the last dir.
    // [sub_18003FFF0 0x18004149f] offscreen 页取首个选中行的行值。
    char path[MAX_PATH];
    path[0] = '\0';
    if (!refs.empty() && refs[0].model != nullptr) {
        const std::string current = DlgSelectedEffectPath();
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
        // [FUN_180041d10 tail] the failure/rejection outcome still enables
        // the Apply button (EnableWindow(1005, 1) runs for every result).
        HWND apply = GetDlgItem(g_dlgWindow, kBtnRefresh);
        if (apply != nullptr) {
            EnableWindow(apply, TRUE);
        }
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
// [sub_180042610] offscreen 页的“显示”判据为行值非 "hide"（含无行对象，
// 对应分配表默认可见）。
void DlgToggleHideShowFlow()
{
    std::vector<SelectedRef> refs;
    DlgCollectSelected(false, false, refs);
    if (refs.empty()) {
        return;
    }
    OffscreenTab* off = DlgCurrentOffscreen();
    bool anyShown = false;
    for (size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].model == nullptr) {
            continue;
        }
        if (off != nullptr) {
            if (DlgRowCheckState(refs[i].model, refs[i].subset) == 2) {
                anyShown = true;
                break;
            }
        } else if (MmeEmmEffectiveSubsetShown(refs[i].model, refs[i].subset)) {
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
// [sub_1800424a0 0x18004254f] offscreen 页经 sub_180033570 擦除该
// (targetId, obj) 的全部表项（含全部子集）；移植端移除全部匹配行。
void DlgResetWithDefaultFlow()
{
    std::vector<SelectedRef> refs;
    DlgCollectSelected(true, false, refs);
    OffscreenTab* off = DlgCurrentOffscreen();
    for (size_t i = 0; i < refs.size(); ++i) {
        ModelData* model = refs[i].model;
        if (model == nullptr) {
            continue;
        }
        if (off != nullptr) {
            DlgRemoveOffscreenRows(off, model);
            continue;
        }
        MmeAssignEffect(model->objectId(), -1, "");
        model->setEffectFile("");
        MmeEmmClearSubsetShows(model);
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
    DialogBoxParamA(g_hInst, MAKEINTRESOURCEA(IDD_MME_LOG), g_dlgWindow,
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
            // [FUN_180044080 @0x180044266-0x1800442ac] the sole fixed item,
            // mask TCIF_TEXT only, lParam 0 (the TCITEM is zeroed first).
            // The stored lParam 0 IS the collection gate's Main value - it is
            // what DlgTabSelParamGate reads back via TCM_GETITEM(TCIF_PARAM).
            // [sub_18003FFF0 0x180040350-0x180040424] Object tabs (mask 9 =
            // TCIF_TEXT|TCIF_PARAM, lParam = the stable offscreen id, text =
            // the resource name) are appended by the rebuild below; the port
            // now carries the live offscreen set, so they appear here.
            TCITEMA tabItem;
            memset(&tabItem, 0, sizeof(tabItem));
            tabItem.mask = TCIF_TEXT;
            tabItem.pszText = const_cast<LPSTR>("Main");
            tabItem.lParam = 0;
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
        HMENU menu = LoadMenuA(g_hInst, MAKEINTRESOURCEA(japanese ? IDR_MME_MAPPING_MENU_JP : IDR_MME_MAPPING_MENU_EN));
        if (menu != nullptr) {
            CheckMenuItem(menu, 40028, g_skipValidation ? MF_CHECKED : MF_UNCHECKED);
            SetMenu(dlg, menu);
        }
        HICON icon = LoadIconA(g_hInst, MAKEINTRESOURCEA(IDI_MME_APP));
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
        // [sub_18003FFF0 @0x180044625 后的打开路径] 打开时按当前 offscreen 集
        // 补建标签页并刷新 1004 说明文字。
        g_offscreenSignature = DlgOffscreenSignature();
        DlgSyncTabs();
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
            // [-551 from the 0x3ea tab control, 0x180044b70-0x180044b99]
            // DAT_1800d9f44 = TCM_GETCURSEL 结果 + sub_18003FFF0(dlg,0)：换页
            // 时行集不变（sub_18002E5F0 不按目标过滤），但对每个既有条目重发
            // LVM_SETITEMTEXT 刷新效果列/复选框，并重设 1004 说明文字。
            DlgRefreshCaption();
            DlgRefreshTexts();
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
                    // [sub_180042610] offscreen 页写行值（hide/快照还原）而非
                    // 对象主可见性 —— DlgApplyShown 内部分流。
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
                        DlgRowEffectIsNone(g_rows[item].model)) {
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
                        DlgRowEffectIsNone(g_rows[item].model)) {
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
            // English filter blob 0x1800b4ff8 ("emm files(*.emm)"); no
            // lpstrTitle in the original OFN; InitialDir = the D7500 global
            // (one-shot "<exedir>\UserFile" seed, then the last pick's dir).
            if (DlgOpenFileDialog("emm files(*.emm)\0*.emm\0All files(*.*)\0*.*\0",
                                  nullptr, path, MAX_PATH, DlgEmmInitialDir())) {
                DlgRememberPickDir(path, g_lastEmmDir);
                MmeEmmLoad(path);
                DlgRebuildList();
                DlgAfterEdit();
            }
            return TRUE;
        }
        case 40010: {   // [FUN_1800431e0]
            char path[MAX_PATH];
            // Same filter blob / no title / shared D7500 InitialDir as the
            // open side; lpstrDefExt "emm" (0x1800b5024), Flags 0x80A.
            if (DlgSaveFileDialog("emm files(*.emm)\0*.emm\0All files(*.*)\0*.*\0",
                                  nullptr, "emm", path, MAX_PATH,
                                  DlgEmmInitialDir())) {
                DlgRememberPickDir(path, g_lastEmmDir);
                MmeEmmSave(path);
            }
            return TRUE;
        }
        case 40016: {   // [FUN_180043420] Open by Model (.emd)
            // Gate: at least one selected whole-object normal row (the
            // original collects them up front and silently no-ops on none).
            std::vector<SelectedRef> wholeRows;
            DlgCollectSelected(true, true, wholeRows);
            if (wholeRows.empty()) {
                return TRUE;
            }
            // No lpstrTitle in the original; filter "emd files(*.emd)";
            // initial dir = the shared "UserFile" dir (DAT_1800d7528).
            char path[MAX_PATH];
            if (DlgOpenFileDialog(
                    "emd files(*.emd)\0*.emd\0All files(*.*)\0*.*\0",
                    nullptr, path, MAX_PATH, g_lastUserFileDir.c_str())) {
                {
                    char drive[3] = { 0 };
                    char dir[0x100] = { 0 };
                    if (_splitpath_s(path, drive, sizeof(drive), dir, sizeof(dir),
                                     nullptr, 0, nullptr, 0) == 0) {
                        char full[0x104];
                        if (_makepath_s(full, sizeof(full), drive, dir,
                                        nullptr, nullptr) == 0) {
                            g_lastUserFileDir = full;
                        }
                    }
                }
                // Apply the per-model mapping to every selected whole-object
                // row ([FUN_180031980] receives the collected objects).
                std::vector<ModelData*> models;
                for (size_t i = 0; i < wholeRows.size(); ++i) {
                    if (wholeRows[i].model != nullptr) {
                        models.push_back(wholeRows[i].model);
                    }
                }
                MmeEmdLoad(path, models);
                // EnableWindow(1005) + the FUN_18003FFF0 rebuild + the 1x1
                // repaint of the original tail.
                DlgRebuildList();
                DlgAfterEdit();
            }
            return TRUE;
        }
        case 40017: {   // [FUN_180043B00] Save by Model (.emd)
            // Gate: EXACTLY one selected whole-object normal row.
            std::vector<SelectedRef> wholeRows;
            DlgCollectSelected(true, true, wholeRows);
            if (wholeRows.size() != 1 || wholeRows[0].model == nullptr) {
                return TRUE;
            }
            // lpstrDefExt is "emm" in the original (0x1800b5024 - kept
            // bug-compatible); no lpstrTitle.
            char path[MAX_PATH];
            if (DlgSaveFileDialog(
                    "emd files(*.emd)\0*.emd\0All files(*.*)\0*.*\0",
                    nullptr, "emm", path, MAX_PATH, g_lastUserFileDir.c_str())) {
                {
                    char drive[3] = { 0 };
                    char dir[0x100] = { 0 };
                    if (_splitpath_s(path, drive, sizeof(drive), dir, sizeof(dir),
                                     nullptr, 0, nullptr, 0) == 0) {
                        char full[0x104];
                        if (_makepath_s(full, sizeof(full), drive, dir,
                                        nullptr, nullptr) == 0) {
                            g_lastUserFileDir = full;
                        }
                    }
                }
                MmeEmdSave(path, wholeRows[0].model);
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
            // [FUN_180041d10 with a resolved path] validate, then assign. The
            // dropped file's extension must survive the rebuild - stripping it
            // produced "dir\name" paths the loader could never open.
            char drive[3] = { 0 };
            char dir[0x100] = { 0 };
            char fname[0x100] = { 0 };
            char fext[0x40] = { 0 };
            char resolved[0x204];
            if (_splitpath_s(path, drive, sizeof(drive), dir, sizeof(dir),
                             fname, sizeof(fname), fext, sizeof(fext)) == 0 &&
                _makepath_s(resolved, sizeof(resolved), drive, dir, fname,
                            fext) == 0) {
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
        // Target-local assignments survive closing the dialog.
        g_offscreenTabs.clear();
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
    g_dlgWindow = CreateDialogParamA(g_hInst, MAKEINTRESOURCEA(IDD_MME_MAPPING), owner,
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
// [sub_18002BAA0 尾部 0x180045dxx 计划重建触发] 原版在每次 pass 计划重建且
// planDirty（manager+0x90）置位时刷新对话框（标签页集在重建里重灌）；移植端
// 以 offscreen 关联签名轮询替代 —— 计划重建/效果重载后签名变化即同步标签页
// 与行文本。
void MmeDlgRefreshIfModelCountChanged()
{
    if (g_dlgWindow == nullptr) {
        return;
    }
    if (DlgOrderedModels() != g_lastModelOrder) {
        DlgRebuildList(true);
    }
    size_t signature = DlgOffscreenSignature();
    if (signature != g_offscreenSignature) {
        g_offscreenSignature = signature;
        DlgSyncTabs();
        DlgRefreshTexts();
    }
}

} // namespace mme
