#include "mikudancestudio/dialog_resources.h"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <commctrl.h>
#include <cstdio>
#include <cstring>
#include <memory>

namespace mds = mikudancestudio;
namespace panel = mikudancestudio::panel;
namespace {
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        ++failures;
    }
}
void CheckText(HWND edit, const char* expected) {
    char text[256]{};
    GetWindowTextA(edit, text, sizeof text);
    if (std::strcmp(text, expected) != 0) {
        std::fprintf(stderr, "Edit text: expected [%s], actual [%s]\n", expected, text);
        ++failures;
    }
}
void CheckVisibleText(HWND edit, const char* expected) {
    char text[256]{};
    GetWindowTextA(edit, text, sizeof text);
    Check(text[0] != '\0' && std::strlen(text) <= std::strlen(expected) &&
              std::strncmp(text, expected, std::strlen(text)) == 0,
          "Initialization displays the formatted value within the original edit width");
}

// Real dialog templates, EDIT and trackbar controls; no GPU or external model.
// Expected behavior follows original x64 RVAs 0x56E50 / 0x57190 and x86
// 0x44C7F0 / 0x42E270, including the seven-character Enter read limit.
void TestDialog(HMODULE resources, int templateId) {
    auto app = std::make_unique<mds::MMDApp>();
    app->state = {};
    auto first = std::make_unique<mds::mdl::ModelRecord>();
    auto second = std::make_unique<mds::mdl::ModelRecord>();
    mds::g_Block = app.get();
    app->CameraMode() = 0;
    app->ModelSlot(3) = reinterpret_cast<unsigned char*>(first.get());
    app->ModelSlot(7) = reinterpret_cast<unsigned char*>(second.get());
    app->SetSelectedModelSlot(3);
    first->edgeScale = 0.57f;
    second->edgeScale = 1.25f;
    app->MessageSeen() = 0;
    app->SceneModified() = 0;

    // Like CreateDialogParamA in the menu handler, the stored handle is still
    // null while WM_INITDIALOG installs and immediately calls the edit proc.
    HWND dialog = CreateDialogParamA(resources, MAKEINTRESOURCEA(templateId), nullptr,
                                     mds::EdgeThicknessDlgProc, 0);
    Check(dialog != nullptr, "Create the production edge-thickness dialog");
    if (!dialog) {
        mds::g_Block = nullptr;
        return;
    }
    app->EdgeThicknessDialog() = dialog;
    HWND edit = GetDlgItem(dialog, panel::kEdgeThicknessEdit);
    HWND slider = GetDlgItem(dialog, panel::kEdgeThicknessSlider);
    // Without ES_AUTOHSCROLL the original narrow edit truncates EM_REPLACESEL
    // to the visible width. Widen only the test control for later long inputs.
    CheckVisibleText(edit, "0.57");
    SetWindowPos(edit, nullptr, 0, 0, 200, 20, SWP_NOMOVE | SWP_NOZORDER);
    Check(app->EdgeThicknessEditProc() != nullptr, "Original edit procedure is saved");
    Check(app->MessageSeen() == 0 && app->SceneModified() == 0,
          "Opening the dialog does not modify the scene");
    Check(SendMessageA(slider, TBM_GETRANGEMIN, 0, 0) == 0 &&
              SendMessageA(slider, TBM_GETRANGEMAX, 0, 0) == 200,
          "Slider retains the original 0..200 range");
#if defined(_M_X64)
    constexpr int initialPosition = 57; // mulss before cvttss2si
#else
    constexpr int initialPosition = 56; // x87 double multiplication of float 0.57
#endif
    Check(SendMessageA(slider, TBM_GETPOS, 0, 0) == initialPosition,
          "Initialization preserves the architecture's original float rounding");
    SetWindowTextA(edit, "0.75");
    SendMessageA(edit, WM_CHAR, VK_RETURN, 0);
    SendMessageA(edit, WM_KILLFOCUS, 0, 0);
    Check(first->edgeScale == 0.57f && app->SceneModified() == 0,
          "Text changes, WM_CHAR and focus loss do not commit");
    SendMessageA(edit, WM_KEYDOWN, VK_RETURN, 0);
    Check(first->edgeScale == 0.75f && app->MessageSeen() == 1 && app->SceneModified() == 1,
          "WM_KEYDOWN Enter commits the selected model and both dirty flags");
    Check(SendMessageA(slider, TBM_GETPOS, 0, 0) == 75, "Enter synchronizes the slider");
    SendMessageA(slider, TBM_SETPOS, TRUE, 137);
    SendMessageA(dialog, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(slider));
    Check(first->edgeScale == 1.37f, "Slider commits its position divided by 100");
    CheckText(edit, "1.37");

    struct Input {
        const char* text;
        float value;
        int position;
    };
    const Input inputs[] = {{"0.57", 0.57f, initialPosition},
                            {"1.23456789", 1.23456f, 123},
                            {"-0.25", -0.25f, 0},
                            {"2.50", 2.5f, 200},
                            {"abc", 0.0f, 0}};
    for (const auto& input : inputs) {
        SetWindowTextA(edit, input.text);
        SendMessageA(edit, WM_KEYDOWN, VK_RETURN, 0);
        Check(first->edgeScale == input.value, "Enter keeps original parsing and value bounds");
        Check(SendMessageA(slider, TBM_GETPOS, 0, 0) == input.position,
              "Slider rounds and clamps independently of the model value");
        CheckText(edit, input.text);
    }
    app->SetSelectedModelSlot(7);
    SetWindowTextA(edit, "0.90");
    SendMessageA(edit, WM_KEYDOWN, VK_RETURN, 0);
    Check(first->edgeScale == 0.0f && second->edgeScale == 0.9f,
          "An open dialog applies to the currently selected model");
    app->CameraMode() = 1;
    app->MessageSeen() = 0;
    app->SceneModified() = 0;
    SetWindowTextA(edit, "0.25");
    SendMessageA(edit, WM_KEYDOWN, VK_RETURN, 0);
    Check(second->edgeScale == 0.9f && app->MessageSeen() == 0 && app->SceneModified() == 0,
          "Camera mode suppresses model writes and dirty flags");
    app->CameraMode() = 0;
    SetWindowTextA(edit, "1.50");
    SendMessageA(dialog, WM_COMMAND, IDOK, 0);
    Check(IsWindow(dialog) && second->edgeScale == 0.9f, "IDOK does not commit or close");
    SendMessageA(dialog, WM_COMMAND, IDCANCEL, 0);
    Check(!IsWindow(dialog) && app->EdgeThicknessDialog() == nullptr,
          "Cancel destroys the dialog and clears its handle");
    Check(second->edgeScale == 0.9f && app->SceneModified() == 0,
          "Closing does not implicitly commit the edit");

    dialog = CreateDialogParamA(resources, MAKEINTRESOURCEA(templateId), nullptr,
                                mds::EdgeThicknessDlgProc, 0);
    Check(dialog != nullptr, "The dialog can reopen after destruction");
    if (dialog) {
        app->EdgeThicknessDialog() = dialog;
        CheckVisibleText(GetDlgItem(dialog, panel::kEdgeThicknessEdit), "0.90");
        SendMessageA(dialog, WM_CLOSE, 0, 0);
        MSG message{};
        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        Check(!IsWindow(dialog) && app->EdgeThicknessDialog() == nullptr,
              "Title-bar close follows the same non-committing cancel path");
    }
    mds::g_Block = nullptr;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 2)
        return 2;
    INITCOMMONCONTROLSEX controls{sizeof controls, ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&controls))
        return 3;
    HMODULE resources =
        LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!resources)
        return 4;
    TestDialog(resources, MDS_DLG_EDGE_THICKNESS_EN);
    TestDialog(resources, MDS_DLG_EDGE_THICKNESS_JP);
    FreeLibrary(resources);
    if (!failures)
        std::puts("Edge-thickness dialogs: initialization, input, slider and lifetime pass");
    return failures ? 1 : 0;
}
