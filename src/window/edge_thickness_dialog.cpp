// Edge-thickness dialog: original edit subclass, slider and commit behavior.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>

#include <cstdio>
#include <cstdlib>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "dialog_scaffold.hpp"

namespace mikudancestudio {
namespace {

void SetEdgeThickness(MMDApp* app, float thickness); // VA 0x0041E980

int EdgeThicknessSliderPosition(float thickness) {
#if defined(_M_X64)
    // Original x64 RVAs 0x5712F / 0x57243: mulss, then cvttss2si.
    return static_cast<int>(thickness * 100.0f);
#else
    return static_cast<int>(thickness * 100.0);
#endif
}
// Original x64 RVA 0x57190 / x86 VA 0x42E270: only WM_KEYDOWN+Enter
// commits the text, reading at most seven characters. Other messages use
// the saved EDIT procedure, including initialization and destruction.
LRESULT CALLBACK EdgeThicknessEditSubclassProc( // VA 0x0042E270
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MMDApp* app = g_Block;
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) { // 0x100 / 0x0D
        const HWND dlg = app->EdgeThicknessDialog();
        if (hwnd == GetDlgItem(dlg, panel::kEdgeThicknessEdit /*646*/)) {
            char buf[8];
            GetWindowTextA(hwnd, buf, 8);
            const float v = static_cast<float>(atof(buf));
            SetEdgeThickness(app, v); // 0x41E980
            SendMessageA(GetDlgItem(dlg, panel::kEdgeThicknessSlider /*647*/), TBM_SETPOS, 1,
                         EdgeThicknessSliderPosition(v));
            return 0;
        }
    }
    return CallWindowProcA(app->EdgeThicknessEditProc(), hwnd, msg, wParam, lParam);
}
// Original x86 VA 0x41E950: camera mode reports 1.0; model mode reads
// the currently selected model's thickness.
float GetEdgeThickness(MMDApp* app) { // VA 0x0041E950
    if (app->CameraMode() != 0)
        return 1.0f;
    unsigned char* model = mdl::ModelBytes(app->SelectedModel());
    return mdl::Mdl(model)->edgeScale;
}
// Original x86 VA 0x41E980: only model mode changes thickness and flags.
void SetEdgeThickness(MMDApp* app, float thickness) { // VA 0x0041E980
    if (app->CameraMode() != 0)
        return;
    app->state.messageSeen = 1;
    app->SceneModified() = 1;
    unsigned char* model = mdl::ModelBytes(app->SelectedModel());
    mdl::Mdl(model)->edgeScale = thickness;
}
} // namespace

// Original x64 RVA 0x56E50 / x86 VA 0x44C7F0, modeless dialog.
// Preserve initialization order and commit behavior: slider changes apply
// immediately; closing discards any edit text not committed with Enter.
INT_PTR CALLBACK EdgeThicknessDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                      LPARAM lParam) { // VA 0x0044C7F0
    (void)lParam;
    MMDApp* app = g_Block;
    switch (msg) {
    case WM_INITDIALOG: {
        MakeDialogTopmostIfRequested(app, hDlg);
        app->EdgeThicknessEditProc() = reinterpret_cast<WNDPROC>(
            GetWindowLongPtrA(GetDlgItem(hDlg, panel::kEdgeThicknessEdit), GWLP_WNDPROC));
        // Original x64 RVA 0x57031 passes the full callback address in R8.
        SetWindowLongPtrA(GetDlgItem(hDlg, panel::kEdgeThicknessEdit), GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(EdgeThicknessEditSubclassProc));
        const float thickness = GetEdgeThickness(app); // 0x41E950
        char buf[0x100];
        sprintf_s(buf, 0x100, "%3.2f", static_cast<double>(thickness));
        PrefillEdit(hDlg, panel::kEdgeThicknessEdit, buf);
        SendMessageA(GetDlgItem(hDlg, panel::kEdgeThicknessSlider), TBM_SETRANGEMIN, 0, 0);
        SendMessageA(GetDlgItem(hDlg, panel::kEdgeThicknessSlider), TBM_SETRANGEMAX, 0, 200);
        SendMessageA(GetDlgItem(hDlg, panel::kEdgeThicknessSlider), TBM_SETTICFREQ, 0x64, 0);
        SendMessageA(GetDlgItem(hDlg, panel::kEdgeThicknessSlider), TBM_SETPOS, 1,
                     EdgeThicknessSliderPosition(thickness));
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hDlg);
            app->EdgeThicknessDialog() = nullptr;
            return 1;
        }
        break;
    case WM_HSCROLL: {
#if defined(_M_X64)
        // Original x64 RVA 0x56EED: divss before storage and text formatting.
        const float v = static_cast<float>(static_cast<int>(SendMessageA(
                            GetDlgItem(hDlg, panel::kEdgeThicknessSlider), TBM_GETPOS, 0, 0))) /
                        100.0f;
#else
        const double v = static_cast<double>(SendMessageA(
                             GetDlgItem(hDlg, panel::kEdgeThicknessSlider), TBM_GETPOS, 0, 0)) /
                         100.0;
#endif
        SetEdgeThickness(app, static_cast<float>(v)); // 0x41E980
        char buf[0x100];
        sprintf_s(buf, 0x100, "%3.2f", v);
        SendMessageA(GetDlgItem(hDlg, panel::kEdgeThicknessEdit), EM_SETSEL, 0,
                     GetWindowTextLengthA(GetDlgItem(hDlg, panel::kEdgeThicknessEdit)));
        SendMessageA(GetDlgItem(hDlg, panel::kEdgeThicknessEdit), EM_REPLACESEL, 0, (LPARAM)buf);
        return 1;
    }
    default:
        break;
    }
    return 0;
}

} // namespace mikudancestudio
