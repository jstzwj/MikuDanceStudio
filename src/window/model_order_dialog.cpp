// Model calculation/draw-order dialogs and their private workspace helpers.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "dialog_scaffold.hpp"

namespace mikudancestudio {
namespace {

// 0x52D370 / 0x52D368 / 0x52D360: JP wide labels "camera/lighting/accessory" / "screen" /
// "なし" (half-width katakana mixed with kanji; sent via SendMessageW).
static const wchar_t kJpCamLightAcc[] = {0xFF76, 0xFF92, 0xFF97, 0xFF65, 0x7167, 0x660E, 0xFF65,
                                         0xFF71, 0xFF78, 0xFF7E, 0xFF7B, 0xFF98, 0x0000};
static const wchar_t kJpScreen[] = {0x5730, 0x9762, 0x0000};
static const wchar_t kJpNone[] = {0x306A, 0x3057, 0x0000};

// Original data-segment globals dword_545930 / dword_545938 (not in Block).
int g_calculateOrderDialogCount = 0; // was g_dword545930, VA 0x00545930 -
                                     // model calculate-order dialog working
                                     // count (main combo count - 1)
int g_displayOrderDialogCount = 0;   // was g_dword545938, VA 0x00545938 -
                                     // model display-order dialog combo
                                     // count

// Calculation order has its own byte (+0x3109 on x64), list rows and raw
// slot indices. x64 init 0x7FF7CB4787E3 and builder 0x7FF7CB4A9950.
void InitModelCalculateOrderDialog(int count, HWND hDlg) {
    MMDApp* app = g_Block;
    app->DialogOrders().modelIndices.reset(new std::int32_t[count]);
    std::int32_t* slots = app->DialogOrders().modelIndices.get();
    int row = 0;
    for (int calculationOrder = 0; calculationOrder < kModelSlotCount; ++calculationOrder) {
        for (int slot = 0; slot < kModelSlotCount; ++slot) {
            mdl::ModelRecord* model = app->ModelSlot(slot);
            if (model == nullptr || model->comboSelIndex2 != calculationOrder)
                continue;
            const auto& record = *model;
            const char* name = app->EnglishUI() != 0 ? record.nameEn : record.name;
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(name));
            slots[row++] = slot;
            break; // Original takes the first model with this order byte.
        }
    }
}

// x64 0x7FF7CB478717..0x7FF7CB478763: row 0..count-1 stores the
// calculation-order byte; raw slots are resolved from the real slot-array base.
void ApplyModelCalculateOrderDialog(MMDApp* app, int count, HWND hDlg) {
    (void)hDlg;
    const std::int32_t* slots = app->DialogOrders().modelIndices.get();
    for (int row = 0; row < count; ++row) {
        mdl::ModelRecord* model = app->ModelSlot(slots[row]);
        model->comboSelIndex2 = static_cast<unsigned char>(row);
    }
}

// VA 0x0041E7B0, x64 0x7FF7CB4A98C0: draw-order array builder. For each
// order 1..count-1, scan the model slots for that combo/draw-order byte and
// store slotIndex+1 into the workspace. Element 0 is unused.
void BuildModelDrawOrderIndices(MMDApp* app, int count) { // VA 0x0041E7B0
    std::int32_t* order = app->DialogOrders().modelIndices.get();
    for (int ord = 1; ord < count; ++ord) {
        // Original x64 0x7FF7CB4A991A scans all 255 model slots.
        for (int i = 0; i < kModelSlotCount; ++i) {
            mdl::ModelRecord* model = app->ModelSlot(i);
            if (model != nullptr && model->comboSelIndex == ord) {
                order[ord] = i + 1;
                break;
            }
        }
    }
}

// VA 0x0045EC80 - draw-order apply (separate from calculation order): for
// i = 1..count-1 store i into model+0x2D7C.  The original addresses the
// slot via base 0x77C (= 0x780 - 4), cancelling the +1 stored by 0x41E7B0;
// the second argument (dialog) is never read.
void ApplyModelDisplayOrderDialog(MMDApp* app, int count, HWND hDlg) { // VA 0x0045EC80
    (void)hDlg;
    std::int32_t* order = app->DialogOrders().modelIndices.get();
    for (int i = 1; i < count; ++i) {
        mdl::ModelRecord* model = app->ModelSlot(order[i] - 1);
        model->comboSelIndex = static_cast<unsigned char>(i);
    }
    ApplyModelComboSelection(app); // 0x44D940
}

} // namespace

// ===========================================================================
// 0x42E370 - case 289 model calculate-order dialog (template caption "model
// calculate order" / model calculation order settings; 0x32B EN / 0x32A JP), modal.
// WM_INITDIALOG: top-most gate; g_calculateOrderDialogCount =
// CB_GETCOUNT(main combo 0x1B4) - 1; sub_41E810 fills listbox 628 from the
// raw slot-index array at app+0xA0B1C, sorted by calculation order.
// WM_COMMAND ids 630/631 move the selected listbox entry up/
// down (LB_GETTEXT/DELETESTRING/INSERTSTRING/SETCURSEL) and swap the two
// adjacent dwords of the app+0xA0B1C array; id 632 (OK) applies via
// sub_41E910 + EndDialog(1), id 2 -> EndDialog(2); both free the array.
// Reference: x64 sub_7FF7CB478490 (x86 0x0042E370).
// ===========================================================================
INT_PTR CALLBACK ModelCalculateOrderDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                            LPARAM lParam) { // VA 0x0042E370
    (void)lParam;
    MMDApp* app = g_Block;
    if (msg == WM_INITDIALOG) {
        MakeDialogTopmostIfRequested(app, hDlg);
        g_calculateOrderDialogCount =
            static_cast<int>(SendMessageA(GetDlgItem(app->state.hwnd, panel::kMainComboModel),
                                          CB_GETCOUNT, 0, 0)) -
            1;
        InitModelCalculateOrderDialog(g_calculateOrderDialogCount, hDlg); // 0x41E810
        return 0;
    }
    if (msg != WM_COMMAND) {
        return 0;
    }
    const WORD id = LOWORD(wParam);
    if (id == panel::kOrderUpButton) { // move up (0x42E3A6)
        const int sel = static_cast<int>(
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_GETCURSEL, 0, 0));
        if (sel >= 1) {
            char buf[100];
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_GETTEXT, sel, (LPARAM)buf);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_DELETESTRING, sel, 0);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_INSERTSTRING, sel - 1,
                         (LPARAM)buf);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_SETCURSEL, sel - 1, 0);
            std::int32_t* arr = app->DialogOrders().modelIndices.get();
            const std::int32_t tmp = arr[sel - 1];
            arr[sel - 1] = arr[sel];
            arr[sel] = tmp;
        }
        return 0;
    }
    if (id == panel::kOrderDownButton) { // move down (0x42E451)
        const int sel = static_cast<int>(
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_GETCURSEL, 0, 0));
        if (sel != -1 && sel < g_calculateOrderDialogCount - 1) {
            char buf[100];
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_GETTEXT, sel, (LPARAM)buf);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_DELETESTRING, sel, 0);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_INSERTSTRING, sel + 1,
                         (LPARAM)buf);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_SETCURSEL, sel + 1, 0);
            std::int32_t* arr = app->DialogOrders().modelIndices.get();
            const std::int32_t tmp = arr[sel + 1];
            arr[sel + 1] = arr[sel];
            arr[sel] = tmp;
        }
        return 0;
    }
    if (id == panel::kDialogOkButton) {                                         // OK (0x42E52E)
        ApplyModelCalculateOrderDialog(app, g_calculateOrderDialogCount, hDlg); // 0x41E910
        EndDialog(hDlg, IDOK);
        if (app->DialogOrders().modelIndices == nullptr) {
            return 0;
        }
        app->DialogOrders().modelIndices.reset();
        return 0;
    }
    if (id == IDCANCEL) { // Cancel (0x42E57E)
        EndDialog(hDlg, IDCANCEL);
        if (app->DialogOrders().modelIndices == nullptr) {
            return 0;
        }
        app->DialogOrders().modelIndices.reset();
        return 0;
    }
    return 0;
}

// ===========================================================================
// 0x4641F0 - case 288 model display-order dialog (template caption "model
// disply order" / model drawing order settings; 0x325 EN / 0x324 JP), modal.
// WM_INITDIALOG: top-most gate; g_displayOrderDialogCount = CB_GETCOUNT
// (main combo 0x1B4); allocate the int array app+0xA0B1C; CB_GETLBTEXT(main
// combo 0x1B4,
// i) 1..count-1 into listbox 0x274; sub_41E7B0 init.
// WM_COMMAND ids 630/631: move the listbox 0x274 entry up/down and swap the
// adjacent dwords of the app+0xA0B1C array.  id 632 (OK): rebuild the main
// combos 0x1B4/0x1DA/0x1C1 (EN: "camera/light/accessory" / "ground" / "non",
// JP wide: "camera/lighting/accessory" / "screen" / "none") from the listbox contents
// (count-1 entries), CB_SETCURSEL(0x1B4, 0), sub_45EC80 apply,
// EndDialog(1).  id 2: EndDialog(2).  Both free app+0xA0B1C.
// Reference: MikuMikuDance.exe sub_4641F0 (decompiled by hand from the
// disassembly; Hex-Rays fails on this routine).
// ===========================================================================
INT_PTR CALLBACK ModelDisplayOrderDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                          LPARAM lParam) { // VA 0x004641F0
    (void)lParam;
    MMDApp* app = g_Block;
    if (msg == WM_INITDIALOG) {
        MakeDialogTopmostIfRequested(app, hDlg);
        const int count = static_cast<int>(
            SendMessageA(GetDlgItem(app->state.hwnd, panel::kMainComboModel), CB_GETCOUNT, 0, 0));
        g_displayOrderDialogCount = count;
        app->DialogOrders().modelIndices.reset(new std::int32_t[count]);
        char buf[0x100];
        for (int i = 1; i < count; ++i) {
            SendMessageA(GetDlgItem(app->state.hwnd, panel::kMainComboModel), CB_GETLBTEXT, i,
                         (LPARAM)buf);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_ADDSTRING, 0, (LPARAM)buf);
        }
        BuildModelDrawOrderIndices(app, count); // 0x41E7B0
        return 0;
    }
    if (msg != WM_COMMAND) {
        return 0;
    }
    const WORD id = LOWORD(wParam);
    if (id == panel::kOrderUpButton) { // move up (0x464225)
        const int sel = static_cast<int>(
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_GETCURSEL, 0, 0));
        if (sel >= 1) {
            char buf[0x100];
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_GETTEXT, sel, (LPARAM)buf);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_DELETESTRING, sel, 0);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_INSERTSTRING, sel - 1,
                         (LPARAM)buf);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_SETCURSEL, sel - 1, 0);
            std::int32_t* arr = app->DialogOrders().modelIndices.get();
            const std::int32_t tmp = arr[sel + 1];
            arr[sel + 1] = arr[sel];
            arr[sel] = tmp;
        }
        return 0;
    }
    if (id == panel::kOrderDownButton) { // move down (0x4642DE)
        const int sel = static_cast<int>(
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_GETCURSEL, 0, 0));
        if (sel != -1 && sel < g_displayOrderDialogCount - 1) {
            char buf[0x100];
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_GETTEXT, sel, (LPARAM)buf);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_DELETESTRING, sel, 0);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_INSERTSTRING, sel + 1,
                         (LPARAM)buf);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_SETCURSEL, sel + 1, 0);
            std::int32_t* arr = app->DialogOrders().modelIndices.get();
            const std::int32_t tmp = arr[sel + 2];
            arr[sel + 2] = arr[sel + 1];
            arr[sel + 1] = tmp;
        }
        return 0;
    }
    if (id == panel::kDialogOkButton) { // OK (0x4643AD)
        HWND mainWnd = app->state.hwnd;
        SendMessageA(GetDlgItem(mainWnd, panel::kMainComboModel), CB_RESETCONTENT, 0, 0);
        SendMessageA(GetDlgItem(mainWnd, panel::kMainComboGround), CB_RESETCONTENT, 0, 0);
        SendMessageA(GetDlgItem(mainWnd, panel::kMainComboNormal), CB_RESETCONTENT, 0, 0);
        if (app->state.englishUI != 0) {
            SendMessageA(GetDlgItem(mainWnd, panel::kMainComboModel), CB_ADDSTRING, 0,
                         (LPARAM) "camera/light/accessory");
            SendMessageA(GetDlgItem(mainWnd, panel::kMainComboGround), CB_ADDSTRING, 0,
                         (LPARAM) "ground");
            SendMessageA(GetDlgItem(mainWnd, panel::kMainComboNormal), CB_ADDSTRING, 0,
                         (LPARAM) "non");
        } else {
            SendMessageW(GetDlgItem(mainWnd, panel::kMainComboModel), CB_ADDSTRING, 0,
                         (LPARAM)kJpCamLightAcc);
            SendMessageW(GetDlgItem(mainWnd, panel::kMainComboGround), CB_ADDSTRING, 0,
                         (LPARAM)kJpScreen);
            SendMessageW(GetDlgItem(mainWnd, panel::kMainComboNormal), CB_ADDSTRING, 0,
                         (LPARAM)kJpNone);
        }
        char buf[0x100];
        for (int i = 0; i < g_displayOrderDialogCount - 1; ++i) {
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_GETTEXT, i, (LPARAM)buf);
            SendMessageA(GetDlgItem(mainWnd, panel::kMainComboModel), CB_ADDSTRING, 0, (LPARAM)buf);
            SendMessageA(GetDlgItem(mainWnd, panel::kMainComboGround), CB_ADDSTRING, 0,
                         (LPARAM)buf);
            SendMessageA(GetDlgItem(mainWnd, panel::kMainComboNormal), CB_ADDSTRING, 0,
                         (LPARAM)buf);
        }
        SendMessageA(GetDlgItem(mainWnd, panel::kMainComboModel), CB_SETCURSEL, 0, 0);
        ApplyModelDisplayOrderDialog(app, g_displayOrderDialogCount, hDlg); // 0x45EC80
        EndDialog(hDlg, IDOK);
        if (app->DialogOrders().modelIndices == nullptr) {
            return 0;
        }
        app->DialogOrders().modelIndices.reset();
        return 0;
    }
    if (id == IDCANCEL) { // Cancel (0x464613)
        EndDialog(hDlg, IDCANCEL);
        if (app->DialogOrders().modelIndices == nullptr) {
            return 0;
        }
        app->DialogOrders().modelIndices.reset();
        return 0;
    }
    return 0;
}

} // namespace mikudancestudio
