#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

namespace mds = mikudancestudio;
namespace mdl = mikudancestudio::mdl;
namespace panel = mikudancestudio::panel;
namespace {
int failures = 0;
int comboRefreshes = 0;
void Check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "%s\n", message);
        ++failures;
    }
}
HWND Child(HWND parent, const char* type, int id, DWORD style = 0) {
    HWND result = CreateWindowExA(0, type, "", WS_CHILD | style, 0, 0, 150, 100, parent,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  GetModuleHandleW(nullptr), nullptr);
    Check(result != nullptr, "Create hidden test control");
    return result;
}
struct Window {
    HWND handle = CreateWindowExA(0, "STATIC", "", WS_OVERLAPPED, 0, 0, 200, 200, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    ~Window() { DestroyWindow(handle); }
};
}

namespace mikudancestudio {
MMDApp::MMDApp() : state{} {
}
MMDApp* g_Block = nullptr;
// Keep the actual dialog procedures and Win32 controls; observe UI refresh tails.
void ApplyModelComboSelection(MMDApp*) {
    ++comboRefreshes;
}
void SelectTimelineTrack(GlobalTimelineTrack track) {
    Check(track == GlobalTimelineTrack::Gravity, "Gravity channel selects gravity track");
}
void PanelPaint(MMDApp*) {
}
void ApplyGravityTrack(MMDApp*) {
}
}

namespace {
void TestModelOrder(bool english, bool calculation) {
    auto app = std::make_unique<mds::MMDApp>();
    mds::g_Block = app.get();
    app->EnglishUI() = english ? 1 : 0;
    Window main, dialog;
    app->state.hwnd = main.handle;
    HWND combo = Child(main.handle, "COMBOBOX", panel::kMainComboModel, CBS_DROPDOWNLIST);
    Child(main.handle, "COMBOBOX", panel::kMainComboGround, CBS_DROPDOWNLIST);
    Child(main.handle, "COMBOBOX", panel::kMainComboNormal, CBS_DROPDOWNLIST);
    HWND list = Child(dialog.handle, "LISTBOX", panel::kOrderListBox, LBS_HASSTRINGS);
    for (const char* label : {"camera/light/accessory", "draw-first", "draw-second"})
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    std::array<std::unique_ptr<mdl::ModelRecord>, 2> records{std::make_unique<mdl::ModelRecord>(),
                                                             std::make_unique<mdl::ModelRecord>()};
    constexpr int firstSlot = 7, secondSlot = mds::kModelSlotCount - 1;
    app->ModelSlot(firstSlot) = records[0].get();
    app->ModelSlot(secondSlot) = records[1].get();
    records[0]->comboSelIndex = 1;
    records[1]->comboSelIndex = 2;
    records[0]->comboSelIndex2 = 1;
    records[1]->comboSelIndex2 = 0;
    strcpy_s(records[0]->name, "first-jp");
    strcpy_s(records[0]->nameEn, "first-en");
    strcpy_s(records[1]->name, "second-jp");
    strcpy_s(records[1]->nameEn, "second-en");
    const auto proc = calculation ? mds::ModelCalculateOrderDlgProc : mds::ModelDisplayOrderDlgProc;
    proc(dialog.handle, WM_INITDIALOG, 0, 0);
    Check(SendMessageA(list, LB_GETCOUNT, 0, 0) == 2, "Order dialog includes both models");
    char text[100]{};
    SendMessageA(list, LB_GETTEXT, 0, reinterpret_cast<LPARAM>(text));
    auto* slots = app->DialogOrders().modelIndices.get();
    if (calculation) {
        Check(std::strcmp(text, english ? "second-en" : "second-jp") == 0,
              "Calculation list uses calculation order and model language");
        Check(slots && slots[0] == secondSlot && slots[1] == firstSlot,
              "Calculation rows hold raw slots, including the final model slot");
        if (!slots || slots[0] != secondSlot || slots[1] != firstSlot) {
            proc(dialog.handle, WM_COMMAND, IDCANCEL, 0);
            return;
        }
    } else {
        Check(std::strcmp(text, "draw-first") == 0 && slots && slots[1] == firstSlot + 1 &&
                  slots[2] == secondSlot + 1,
              "Draw list retains combo labels and slot+1 workspace");
    }
    SendMessageA(list, LB_SETCURSEL, 1, 0);
    proc(dialog.handle, WM_COMMAND, panel::kOrderUpButton, 0);
    SendMessageA(list, LB_GETTEXT, 0, reinterpret_cast<LPARAM>(text));
    Check(std::strcmp(text, calculation ? (english ? "first-en" : "first-jp") : "draw-second") == 0,
          "Move-up changes the visible row with its workspace slot");
    proc(dialog.handle, WM_COMMAND, panel::kOrderDownButton, 0);
    proc(dialog.handle, WM_COMMAND, panel::kOrderUpButton, 0);
    proc(dialog.handle, WM_COMMAND, panel::kDialogOkButton, 0);
    if (calculation) {
        Check(records[0]->comboSelIndex2 == 0 && records[1]->comboSelIndex2 == 1 &&
                  records[0]->comboSelIndex == 1 && records[1]->comboSelIndex == 2,
              "Calculation apply updates its own byte, preserving draw order");
    } else {
        Check(records[0]->comboSelIndex == 2 && records[1]->comboSelIndex == 1 &&
                  records[0]->comboSelIndex2 == 1 && records[1]->comboSelIndex2 == 0,
              "Draw apply preserves calculation order");
    }
    Check(!app->DialogOrders().modelIndices, "OK releases the model-order workspace");
    SendMessageA(list, LB_RESETCONTENT, 0, 0);
    proc(dialog.handle, WM_INITDIALOG, 0, 0);
    SendMessageA(list, LB_SETCURSEL, 1, 0);
    proc(dialog.handle, WM_COMMAND, panel::kOrderUpButton, 0);
    proc(dialog.handle, WM_COMMAND, IDCANCEL, 0);
    Check(!app->DialogOrders().modelIndices && records[0]->comboSelIndex == (calculation ? 1 : 2) &&
              records[0]->comboSelIndex2 == (calculation ? 0 : 1),
          "Cancel releases workspace without applying the reorder");
}

void TestGravity() {
    auto app = std::make_unique<mds::MMDApp>();
    mds::g_Block = app.get();
    Window dialog;
    app->GravitySettingDialog() = dialog.handle;
    std::array<HWND, 5> edits{};
    std::array<HWND, 3> sliders{};
    for (int i = 0; i < 5; ++i)
        edits[i] = Child(dialog.handle, "EDIT", panel::kGravityAccelEdit + i);
    for (int i = 0; i < 3; ++i)
        sliders[i] = Child(dialog.handle, TRACKBAR_CLASSA, panel::kGravityDirXSlider + i);
    Child(dialog.handle, "BUTTON", panel::kGravityNoiseCheckbox, BS_AUTOCHECKBOX);
    const auto originalProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrA(edits[0], GWLP_WNDPROC));
    app->state.gravityMagnitude = 9.8f;
    app->state.gravityY = -1;
    mds::GravitySettingDlgProc(dialog.handle, WM_INITDIALOG, 0, 0);
    Check(app->GravitySettingEditProc() == originalProc,
          "Gravity installation and forwarding must share the saved edit procedure");
    // Avoid calling an invalid callback when running against the old installation.
    if (app->GravitySettingEditProc() != originalProc) {
        for (HWND edit : edits)
            SetWindowLongPtrA(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalProc));
        return;
    }
    for (int axis = 0; axis < 3; ++axis) {
        for (HWND slider : sliders)
            SendMessageA(slider, TBM_SETPOS, TRUE, 0);
        SetWindowTextA(edits[axis + 1], "0.25");
        SendMessageA(edits[axis + 1], WM_KEYDOWN, VK_RETURN, 0);
        for (int other = 0; other < 3; ++other)
            Check(SendMessageA(sliders[other], TBM_GETPOS, 0, 0) == (axis == other ? 25 : 0),
                  "Enter updates only the matching X/Y/Z slider (637/638/639)");
        const float values[3] = {app->state.gravityX, app->state.gravityY, app->state.gravityZ};
        Check(values[axis] == 0.25f, "Enter applies the matching gravity direction channel");
        SendMessageA(sliders[axis], TBM_SETPOS, TRUE, -50);
        mds::GravitySettingDlgProc(dialog.handle, WM_HSCROLL, TB_THUMBPOSITION,
                                   reinterpret_cast<LPARAM>(sliders[axis]));
        char text[30]{};
        GetWindowTextA(edits[axis + 1], text, sizeof text);
        Check(std::strcmp(text, "-0.50") == 0, "Slider echoes its direction value into the edit");
    }
}
}

int main() {
    INITCOMMONCONTROLSEX controls{sizeof controls, ICC_BAR_CLASSES};
    Check(InitCommonControlsEx(&controls) != FALSE, "Initialize real trackbar controls");
    TestModelOrder(false, true);
    TestModelOrder(true, true);
    TestModelOrder(false, false);
    TestModelOrder(true, false);
    Check(comboRefreshes == 2, "Only draw-order OK refreshes the model combo selection");
    TestGravity();
    return failures == 0 ? 0 : 1;
}
