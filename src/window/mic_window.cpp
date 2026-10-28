// ===========================================================================
// VA 0x00461E00 / 0x00461FA0 - separate ("Mic") window create / destroy pair
// ===========================================================================
// The View > "separate window" toggle (menu 0x118, command 280) moves dialog
// items 536..557 (0x218..0x22D, the left accessory/bone/morph panels) out of
// the main window into a fresh WS_POPUP "MicWindow" and back.  The placement
// dwords it persists (app+658768..658784) are the same ones mmconfig.ini
// restores, so the toggle state and geometry survive restarts.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>

#include <cstdint>
#include <cstdlib>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

// WM_COMMAND child dispatch (0x004620A0) - full port in wndproc_aux_windows.cpp.
void DispatchSeparateWindowCommand(MMDApp* app, unsigned short id); // VA 0x004620A0
// Viewport overlay layout (0x0040CAC0) - ported in ui_viewport_layout.cpp.
void LayoutViewportPanels(MMDApp* app); // VA 0x0040CAC0

// VA 0x00428FF0 - separate-window mouse-move filter (thiscall on app).
// Tracks the cursor inside the detached panel, clears the left/top
// "auto-hide armed" bytes once the pointer crosses the hide margins, and
// raises the moved-flag for the frame driver (drag threshold 50px).
//
int FilterSeparateWindowMouseMove(MMDApp* app, unsigned short x, unsigned short y) {
    auto& s = *app;
    if (s.state.a06B5 != 0) { // 0x428FF6
        // original: (int)(sub1D574+120048 /*viewScale, render scale*/ *
        // 200.0) via __ftol2_sse
        const int edgeBand = static_cast<int>(s.Renderer()->viewScale * 200.0f);
        if (x > s.ViewportRect().right - edgeBand) // 0x429028
            return edgeBand;
        s.state.a06B5 = 0; // 0x42902E
    }
    int result = y;
    if (s.state.a06B4 != 0) {            // 0x429034
        if (y > s.ViewportRect().bottom) // 0x42904A
            return result;
        s.state.a06B4 = 0; // 0x429050
    }
    if (s.MouseX() != x || s.MouseY() != y) { // 0x42906F
        const bool first = s.state.separateWindowMouseSeen == 0;
        s.MouseX() = x; // 0x429077
        s.MouseY() = y; // 0x42907A
        if (!first) {
            const int dy = std::abs(s.PreviousMouseY() - static_cast<int>(y));
            const int dx = std::abs(s.PreviousMouseX() - static_cast<int>(x));
            // 0x429090: OFSUB/SF/ZF flag algebra on the unsigned deltas
            // reduces to "either axis moved more than 50 px".
            if (dx > 50 || dy > 50)              // 0x429096
                s.state.mouseJumped = 1;         // 0x4290AC
            s.PreviousMouseX() = x;              // 0x4290B4
            s.PreviousMouseY() = y;              // 0x4290B7
            s.state.separateWindowMouseSeen = 0; // 0x4290BA
        }
        if (s.ViewportToolHovered() == 1) { // 0x4290C6
            SetCursor(LoadCursorA(nullptr,
                                  reinterpret_cast<LPCSTR>(0x7F89)) // IDC_SIZEALL
            );                                                      // 0x4290CE
            result = 1;
        }
    }
    return result;
}

// VA 0x00466A10 - separate ("Mic") window procedure.
LRESULT CALLBACK MicWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MMDApp* app = g_Block;
    auto& s = *app;
    switch (msg) {
    case WM_MOUSEMOVE:                                                      // 0x466A36
        s.MessageSeen() = 1;                                                // 0x466BDB
        FilterSeparateWindowMouseMove(app, LOWORD(lParam), HIWORD(lParam)); // 0x466BF6
        return 0;
    case WM_CLOSE:              // 0x466B8A
        SaveFlagSubsystem(app); // 0x466BBD
        return 0;
    case WM_COMMAND:                                        // 0x466B91
        DispatchSeparateWindowCommand(app, LOWORD(wParam)); // 0x466B9E
        return 0;
    case WM_PAINT: { // 0x466AB7
        RECT rc;
        GetClientRect(hwnd, &rc);
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        // Black out the four regions the detached panels no longer cover
        // (the auto-hide margins saved at app+0xA0D40..0xA0D4C).
        const RECT& viewport = s.ViewportRect();
        BitBlt(hdc, 0, 0, rc.right, viewport.top, 0, 0, 0, 0x42);
        BitBlt(hdc, 0, 0, viewport.left, rc.bottom, 0, 0, 0, 0x42);
        BitBlt(hdc, viewport.right, 0, rc.right, rc.bottom, 0, 0, 0, 0x42);
        BitBlt(hdc, 0, viewport.bottom, rc.right, rc.bottom, 0, 0, 0, 0x42);
        HandlePaletteChanged2(hdc); // 0x466B60
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOVE:                           // 0x466A96
        RefreshSeparateWindowViewport(app); // 0x466A96
        return 0;
    case WM_SIZE:                           // 0x466A5C
        s.MessageSeen() = 1;                // 0x466A5C
        RefreshSeparateWindowViewport(app); // 0x466A6C
        LayoutViewportPanels(app);          // 0x466A77
        return 0;
    case WM_RBUTTONDBLCLK: // 0x7FF7CB4C4B29
        // x64 twin sub_7FF7CB4C48A0 dispatches the panel-activation
        // helper on WM_RBUTTONDBLCLK (0x206) and returns 0; WM_MOUSE-
        // ACTIVATE (0x21) is not in its switch and falls to
        // DefWindowProcA.  The old "0x466C6E = WM_MOUSEACTIVATE" note
        // misread the case label.
        HandleMouseActivate(app);
        return 0;
    case WM_LBUTTONDOWN: // 0x466C4E
        SetCapture(s.FloatingWindow());
        return 0;
    case WM_LBUTTONUP: // 0x466C27
        ReleaseCapture();
        return 0;
    case WM_MOUSEWHEEL: // 0x466C8C
        // x87 double math then float store: angle += wheel * 0.05.
        s.CameraDistance() = static_cast<float>(
            static_cast<double>(static_cast<short>(HIWORD(wParam))) * 0.05000000074505806 +
            static_cast<double>(s.CameraDistance()));
        PostViewRefresh(app); // 0x466D00
        return 0;
    case 792 /*WM_PALETTECHANGED*/: // 0x466CBC
        HandlePaletteChanged2(reinterpret_cast<HDC>(wParam));
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam); // 0x466A51
    }
}

void InitFlagSubsystem(MMDApp* app) {
    auto& s = *app;
    HWND mic = CreateWindowExA( // 0x461E3C
        8 /*WS_EX_TOPMOST*/, "MicWindow", "MMD",
        0x80CF0000u, // WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME
                     // |WS_MINIMIZEBOX|WS_MAXIMIZEBOX
        s.SeparateWindowX(), s.SeparateWindowY(), s.SeparateWindowWidth(), s.SeparateWindowHeight(),
        nullptr, nullptr, static_cast<HINSTANCE>(s.HInstance()), nullptr);
    s.FloatingWindow() = mic; // 0x461E44
    if (!mic) {
        HWND main = s.MainWindow();
        if (s.EnglishUI()) // 0xA0B4C
            MessageBoxA(main, "CreateWindow failed", "create separate window", 0);
        else
            MessageBoxA(main, "CreateWindow failed", "\x95\xca\x91\x8b\x8d\xec\x90\xac",
                        0); // 0x52E640 JP caption
        return;
    }

    ShowWindow(mic, s.SeparateWindowMaximized() == 1 ? SW_MAXIMIZE : SW_SHOW); // 0x461E98
    UpdateWindow(s.FloatingWindow());                                          // 0x461EAD
    CheckMenuItem(GetMenu(s.MainWindow()), MDS_CMD_TOGGLE_SEPARATE_WINDOW,
                  8 /*MF_CHECKED*/); // 0x461ECA

    // Stash the main-window client width and lay the panels out against the
    // full client area while they are detached.
    HWND main = s.MainWindow();
    s.SeparateWindowSidebarWidth() = s.SidebarWidth();
    RECT rc; // 0x461EE8
    GetClientRect(main, &rc);
    s.SidebarWidth() = rc.right - 3;                                             // 0x461F03
    if ((GetMenuState(GetMenu(main), MDS_CMD_TOGGLE_ALWAYS_ON_TOP, 0) & 8) == 0) // 0x461F14
        SetWindowPos(s.FloatingWindow(),
                     reinterpret_cast<HWND>(static_cast<intptr_t>(-2)) /*HWND_TOPMOST*/, 0, 0, 0, 0,
                     0x43 /*SWP_* keep-size activate*/);                          // 0x461F29
    for (int id = panel::kModelEditToggle; id <= panel::kCoordAxisCheckbox; ++id) // 0x461F3B
        SetParent(GetDlgItem(main, id),
                  s.FloatingWindow()); // 0x461F52

    RelayoutSidebarControls(app);                       // 0x461F61
    PanelPaint(app);                                    // 0x461F68
    InvalidateRect(main, nullptr, FALSE);               // 0x461F7E
    InvalidateRect(s.FloatingWindow(), nullptr, FALSE); // 0x461F8B
}

void SaveFlagSubsystem(MMDApp* app) {
    auto& s = *app;
    HWND main = s.MainWindow();
    for (int id = panel::kModelEditToggle; id <= panel::kCoordAxisCheckbox; ++id) // 0x461FB5
        SetParent(GetDlgItem(s.FloatingWindow(), id),
                  main); // 0x461FD2

    WINDOWPLACEMENT wndpl; // 0x461FF3
    wndpl.length = 44;
    GetWindowPlacement(s.FloatingWindow(), &wndpl);
    s.SeparateWindowX() = wndpl.rcNormalPosition.left;
    s.SeparateWindowY() = wndpl.rcNormalPosition.top;
    s.SeparateWindowWidth() = wndpl.rcNormalPosition.right - wndpl.rcNormalPosition.left;
    s.SeparateWindowHeight() = wndpl.rcNormalPosition.bottom - wndpl.rcNormalPosition.top;
    s.SeparateWindowMaximized() = wndpl.showCmd == SW_SHOWMAXIMIZED;
    DestroyWindow(s.FloatingWindow()); // 0x46203A
    s.FloatingWindow() = nullptr;

    CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_SEPARATE_WINDOW, 0 /*MF_UNCHECKED*/); // 0x46205F
    s.SidebarWidth() = s.SeparateWindowSidebarWidth();
    PanelPaint(app);                      // 0x462073
    InvalidateRect(main, nullptr, FALSE); // 0x462083
    RelayoutSidebarControls(app);         // 0x462090
}

} // namespace mikudancestudio
