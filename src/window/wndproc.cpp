// ===========================================================================
// VA 0x004C3A10 - MainWndProc  (original: sub_4C3A10, 0x8A6 bytes)
// ===========================================================================
// Full port of the main window message procedure.  Every entry first sets
// the message-seen flag (this+658796 = 1), then dispatches:
//   WM_CREATE      -> CreateUIControls (0x466D20); failure exits process
//   WM_DESTROY     -> save Data\mmconfig.ini (mirror of the 0x47A5B0 load)
//                     then PostQuitMessage
//   WM_SIZE        -> flag 672812 toggles around sub_443300 [stubbed]
//   WM_PAINT       -> sub_47C0A0 [stubbed]
//   WM_CLOSE       -> dirty-flag confirm dialogs (EN + JP texts; JP bytes
//                     backfilled from x64 0x7FF7CB552700/0x7FF7CB552798)
//   WM_ERASEBKGND  -> suppress (D3D owns the client area)
//   WM_NOTIFY      -> sub_4398B0 [stubbed]
//   WM_COMMAND     -> command dispatcher 0x47E8A0 (68 KB) [stubbed]
//   WM_DROPFILES   -> sub_461300 [stubbed]
//   0x318 (792)    -> palette: sub_42CEB0 + sub_42C140 [stubbed]
//   WM_TIMER       -> timer 100: sub_429770; timer 101: the Kinect
//                     auto-frame-record stage byte (app+658792 = 0xA0D68,
//                     state.autoRepeat / x64 0xA1E14), walked 1->2->3->4
//                     at 1500 ms intervals
//   WM_H/VSCROLL   -> sub_44AEE0 / sub_44BB30 [stubbed]
//   WM_CTLCOLORSTATIC(0x138) -> sub_40E0E0 [stubbed]
//   WM_MOUSE*      -> capture handling + sub_446A70/44A9A0/44AAA0; the
//                     panel-activation sub_4632F0 rides WM_RBUTTONDBLCLK
//                     (x64 0x7FF7CB4FC2C9 case 518), NOT WM_MOUSEACTIVATE
//                     (0x21 falls through to DefWindowProc)
//   WM_MOUSEWHEEL  -> sub_44BD70 [stubbed]
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>
#include <stdio.h>
#include <cstdlib>

#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// Japanese WM_CLOSE confirmation text, filled from SJIS bytes read directly from x64 (referenced by MainWndProc
// 0x7FF7CB4FBBF0; x86 addresses 0x531810/0x531794/0x531864 are no longer used):
static const char kMsgQuitEnhancedJp[] = // x64 0x7FF7CB552700
    "\x27\x8A\x67\x92\xA3\x83\x82\x83\x66\x83\x8B\x95\xDB\x91\xB6\x27"
    "\x82\xB5\x82\xC4\x82\xA2\x82\xC8\x82\xA2\x8A\x67\x92\xA3\x95\xD2\x8F\x57"
    "\x82\xB5\x82\xBD\x83\x82\x83\x66\x83\x8B\x82\xAA\x82\xA0\x82\xE8\x82\xDC"
    "\x82\xB7\n\n"
    "\x8F\x49\x97\xB9\x82\xB5\x82\xC4\x82\xE6\x82\xEB\x82\xB5\x82\xA2\x82\xC5"
    "\x82\xB7\x82\xA9\x81\x48";
// "An edited extended model has not been saved with 'Save extended model'.\n\nAre you sure you want to exit?"
static const char kMsgQuitModifiedJp[] = // x64 0x7FF7CB552798
    "\x95\xDB\x91\xB6\x82\xB5\x82\xC4\x82\xA2\x82\xC8\x82\xA2\x95\xCF\x8D\x58"
    "\x93\x5F\x82\xAA\x82\xA0\x82\xE8\x82\xDC\x82\xB7\n\n"
    "\x8F\x49\x97\xB9\x82\xB5\x82\xC4\x82\xE6\x82\xEB\x82\xB5\x82\xA2\x82\xC5"
    "\x82\xB7\x82\xA9\x81\x48";
// "There are unsaved changes.\n\nAre you sure you want to exit?"
static const char kCaptionQuitJp[] = "\x8F\x49\x97\xB9"; // x64 0x7FF7CB5526F8 "Exit"

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MMDApp* app = g_Block;
    auto& s = *app;
    s.MessageSeen() = 1; // 658796

    if (msg <= WM_COMMAND /*0x111*/) {
        if (msg == WM_COMMAND) {
            CommandDispatch(reinterpret_cast<HWND>(lParam),
                            wParam); // 0x47E8A0
            return 0;
        }
        switch (msg) {
        case WM_CREATE:
            if (!CreateUIControls(app, hwnd)) // 0x466D20
                exit(1);
            return 0;

        case WM_DESTROY: {
            WINDOWPLACEMENT wndpl;
            wndpl.length = sizeof(wndpl);
            GetWindowPlacement(hwnd, &wndpl);
            SetCurrentDirectoryW(app->ExeDir()); // +657102
            FILE* f = nullptr;
            if (fopen_s(&f, "Data\\mmconfig.ini", "wt") == 0) {
                fprintf(f, "%d\n", wndpl.rcNormalPosition.left);
                fprintf(f, "%d\n", wndpl.rcNormalPosition.top);
                fprintf(f, "%d\n", wndpl.rcNormalPosition.right - wndpl.rcNormalPosition.left);
                fprintf(f, "%d\n", wndpl.rcNormalPosition.bottom - wndpl.rcNormalPosition.top);
                fprintf(f, wndpl.showCmd == 3 ? "1\n" : "0\n");
                fprintf(f, "%d\n", s.RenderWidth());
                fprintf(f, "%d\n", s.RenderHeight());
                // slot-visibility variant: 658744 ? 658748 : sidebar
                if (s.FloatingWindow() != nullptr)
                    fprintf(f, "%d\n", s.SeparateWindowSidebarWidth());
                else
                    fprintf(f, "%d\n", s.SidebarWidth());
                fprintf(f, s.EnglishUI() ? "1\n" : "0\n");
                if (s.FloatingWindow() != nullptr) {
                    fprintf(f, "1\n");
                    SaveFlagSubsystem(app); // 0x461FA0
                } else {
                    fprintf(f, "0\n");
                }
                fprintf(f, "%d\n", s.SeparateWindowMaximized());
                fprintf(f, "%d\n", s.SeparateWindowX());
                fprintf(f, "%d\n", s.SeparateWindowY());
                fprintf(f, "%d\n", s.SeparateWindowWidth());
                fprintf(f, "%d\n", s.SeparateWindowHeight());
                HWND dlg530 = GetDlgItem(hwnd, panel::kPhysicsFrameCheckbox);
                fprintf(f, SendMessageA(dlg530, BM_GETCHECK, 0, 0) == 1 ? "1\n" : "0\n");
                fprintf(f, "1\n");
                fwprintf(f, L"%s\n", app->DirModel());
                fwprintf(f, L"%s\n", app->DirAccs());
                fwprintf(f, L"%s\n", app->DirWave());
                fwprintf(f, L"%s\n", app->DirPose());
                fwprintf(f, L"%s\n", app->DirMotion());
                fwprintf(f, L"%s\n", app->DirUser());
                fprintf(f, "1\n");
                fprintf(f, s.FrameVolumeControlEnabled() ? "1\n" : "0\n");
                fprintf(f, "%d\n", s.FrameNormalization());
                fprintf(f, "1\n");
                fprintf(f, "%f\n", s.SidebarRatio());
                fprintf(f, "1\n");
                fwprintf(f, L"%s\n", app->DirBg());
                fprintf(f, "1\n");
                HMENU menu = GetMenu(hwnd);
                fprintf(f,
                        (GetMenuState(menu, MDS_CMD_TOGGLE_ALWAYS_ON_TOP, 0) & 8) ? "1\n" : "0\n");
                fprintf(f, "1\n");
                menu = GetMenu(hwnd);
                fprintf(f, (GetMenuState(menu, MDS_CMD_TOGGLE_PREVIOUS_FOLDER, 0) & 8) ? "1\n"
                                                                                       : "0\n");
                fclose(f);
            }
            PostQuitMessage(0);
            return 0;
        }

        case WM_SIZE:
            s.WindowLayoutReady() = 0; // 672812
            HandleWindowSize(app);     // 0x443300
            s.WindowLayoutReady() = 1;
            return 0;

        case WM_PAINT:
            HandleWindowPaint(app); // 0x47C0A0
            return 0;

        case WM_CLOSE:
            if (s.EnhancedModelDirty() != 0) { // 658276
                // enhanced-model dirty flag
                int r =
                    s.EnglishUI()
                        ? MessageBoxA(
                              hwnd,
                              "There is a enhanced model not preserved by 'save enhanced model'.\n\nDo you realy quit?",
                              "quit", 0x40001)
                        : MessageBoxA(
                              hwnd, kMsgQuitEnhancedJp, kCaptionQuitJp,
                              (MB_OKCANCEL |
                               MB_TOPMOST)); // x64 body 0x7FF7CB4FBCE7/caption 0x7FF7CB4FBCEE
                if (r == IDOK)               // original: == 1
                    return DefWindowProcA(hwnd, msg, wParam, lParam);
            } else {
                if (s.SceneModified() == 0) // 658189
                    return DefWindowProcA(hwnd, msg, wParam, lParam);
                int r =
                    s.EnglishUI()
                        ? MessageBoxA(
                              hwnd, "There is a change point not preserved.\n\nDo you realy quit?",
                              "quit", 0x40001)
                        : MessageBoxA(
                              hwnd, kMsgQuitModifiedJp, kCaptionQuitJp,
                              (MB_OKCANCEL | MB_TOPMOST)); // x64 body 0x7FF7CB4FBD44/shared caption
                if (r == IDOK)
                    return DefWindowProcA(hwnd, msg, wParam, lParam);
            }
            return 0;

        case WM_ERASEBKGND:
            return 0;

        case WM_NOTIFY:
            return HandleNotify(hwnd, msg, wParam, lParam); // 0x4398B0

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        }
    }

    if (msg > 0x233) {
        if (msg != 792 /*0x318*/)
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        HandlePaletteChanged(reinterpret_cast<HDC>(wParam));  // 0x42CEB0
        HandlePaletteChanged2(reinterpret_cast<HDC>(wParam)); // 0x42C140
        return 0;
    }

    if (msg == WM_DROPFILES /*0x233=563*/) {
        HandleDropFiles(reinterpret_cast<HDROP>(wParam)); // 0x461300
        return 0;
    }

    switch (msg) {
    case WM_TIMER:
        if (wParam == 100) {
            KillTimer(hwnd, 100);
            HandleTimer100(app); // 0x429770
            return 0;
        }
        if (wParam != 101)
            return 0;
        KillTimer(hwnd, 101);
        ++s.AutoRepeatCount(); // 658792
        if (s.AutoRepeatCount() >= 4 || s.AutoRepeatCount() <= 0)
            return 0;
        SetTimer(hwnd, 101, 0x5DC, nullptr); // 1500 ms
        return 0;

    case WM_HSCROLL:
        HandleHScroll(lParam, wParam); // 0x44AEE0
        return 0;
    case WM_VSCROLL:
        HandleVScroll(lParam, wParam); // 0x44BB30
        return 0;
    case 0x138: // WM_CTLCOLORSTATIC
        return HandleCtlColor(reinterpret_cast<HWND>(lParam),
                              reinterpret_cast<HDC>(wParam)); // 0x40E0E0
    case WM_MOUSEMOVE:
        HandleMouseMove(static_cast<std::uint32_t>(lParam),
                        HIWORD(lParam)); // 0x444CC0
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(static_cast<HWND>(s.Hwnd()));
        HandleLButtonDown(app); // 0x446A70
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        HandleLButtonUp(app); // 0x44A9A0
        return 0;
    case WM_LBUTTONDBLCLK:
        HandleLButtonDblClk(app); // 0x44AAA0
        return 0;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        SetCapture(static_cast<HWND>(s.Hwnd()));
        return 0;
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        ReleaseCapture();
        return 0;
    case WM_RBUTTONDBLCLK:
        // x64 sub_7FF7CB4FBBF0 jumptable case 518 @0x7FF7CB4FC2C9: call
        // sub_7FF7CB45DCD0 (0x4632F0 twin), then the common epilogue
        // (xor eax,eax / return 0).  WM_MOUSEACTIVATE (0x21) is NOT a case
        // in either window procedure - default branch -> DefWindowProc.
        HandleMouseActivate(app); // 0x4632F0
        return 0;
    case WM_MOUSEWHEEL:
        HandleMouseWheel(HIWORD(wParam)); // 0x44BD70
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

} // namespace mikudancestudio
