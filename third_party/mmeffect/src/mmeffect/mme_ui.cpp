#include "mme_resources.h"
// mme_ui.cpp - see mme_ui.h
//
// Evidence (big-C):
//   - FUN_180055890 (69669-69780): language resolve (ExpGetEnglishMode or the
//     host "Language" menu 0x104 "English" probe), LoadMenuA(0x65/0x6b),
//     InsertMenuItemA(position 100, MFT_RIGHTJUSTIFY 0x4000, wID 0x65/0x6b,
//     text "MMEffect", hSubMenu = the loaded menu), CheckMenuItem(0x9c41=40001,
//     DAT_1800d72e1) + CheckMenuItem(0x9c56=40022, g_bAutoSave), DrawMenuBar.
//   - Initialize (70057/70061): SetWindowLongPtrA(main, GWLP_WNDPROC=-4,
//     0x180055b10); SetWindowsHookExA(2 /*WH_KEYBOARD*/, FUN_180055800, NULL,
//     dwThreadId) -> DAT_1800d9b28.
//   - FUN_180055800 (69647-69665): nCode >= 0 && wParam == 0x45 && Ctrl down
//     && Shift down && (lParam & 0xc0000000) == 0 && main window ->
//     PostMessageA(main, WM_COMMAND 0x111, 0x9c46=40006, 0); CallNextHookEx.
//   - FUN_180055b10 (0x180055b10, body not decompiled in the big C; size 0x710
//     per functions_index.csv): the main-window subclass proc. Its string
//     references (evidence_string_refs.txt: "System" x6 / "false" x13 /
//     "EMMAutoSave" x3 / "About MikuMikuEffect" x2) and the callmap
//     ("handles the 40005-40022 menu commands") fix the dispatch reproduced
//     here; the About caption is "About MikuMikuEffect" and the 40022 toggle
//     writes [System] EMMAutoSave=true/false back to MMEffect.ini.
//   - FUN_180008fc0 (8673-8695): the log window create + scroll + focus.
//   - FUN_180009080 tail (8841-8850): the dialog mirror - GetDlgItem(0x3e9),
//     GetWindowTextLengthA, EM_SETSEL(len, len), EM_REPLACESEL(0, text).
//   - DllMain (69501-69520): GdiplusStartup on DLL_PROCESS_ATTACH /
//     GdiplusShutdown on DLL_PROCESS_DETACH, g_hInst = hinstDLL.
#include "mme_ui.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <d3dx9.h>

// GDI+ (DllMain startup token; windows.h already included via mme_ui.h).
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <gdiplus.h>

#include "mme_dlg.h"
#include "mme_globals.h"
#include "mme_log.h"
#include "mme_util.h"
#include "emm_manager.h"
#include "effect_engine.h"
#include "model_data.h"
#include "mme_context.h"   // MmeContext full definition (ctx->effectEnabled etc.)

// [Phase 4] GDI+ lifecycle (GdiplusStartup at DllMain attach per big-C 69501).
#include <gdiplus.h>
using namespace Gdiplus;

#include "mmhack_api.h"   // GetMMDMainWindow / IsEditMode (host queries)

namespace mme {

HINSTANCE g_hInst = nullptr;   // g_hInst (DAT_1800d9aa8)
ULONG_PTR g_gdiplusToken = 0;  // gdiplusToken (big-C 1897)

namespace {

HWND g_logWindow = nullptr;    // DAT_1800d99c8 (the modeless log dialog)
HHOOK g_logHook = nullptr;     // DAT_1800d99d0 (the log WH_GETMESSAGE hook)
HBRUSH g_logBrush = nullptr;   // DAT_1800da120 (CreateSolidBrush(0xffffff))

// [FUN_1800494b0 + FUN_1800490a0] the ini write-back of a [System] boolean
// ("true"/"false" values, the same literal pair the dialog's 40028 path
// writes). The original serializes through its IniFile object; the
// WritePrivateProfileStringA result is the same "[System] key=value" text the
// IniFile parser reads back (documented divergence).
void WriteIniBool(const char* key, bool value)
{
    if (g_iniPath.empty()) {
        return;
    }
    WritePrivateProfileStringA("System", key, value ? "true" : "false",
                               g_iniPath.c_str());
}

// [0x1800b4ea4 checkmark helpers] keep the two "Enable Effect" ids (40006 JP
// menu / 40020 EN menu) in sync on the installed MME menu.
void SyncEffectToggleMenu()
{
    if (g_menuState == nullptr) {
        return;
    }
    MmeContext* ctx = g_context;
    UINT check = (ctx != nullptr && ctx->effectEnabled != 0) ? MF_CHECKED : MF_UNCHECKED;
    CheckMenuItem(g_menuState, 40006, MF_BYCOMMAND | check);
    CheckMenuItem(g_menuState, 40020, MF_BYCOMMAND | check);
}

void SyncAutoReloadMenu()
{
    if (g_menuState != nullptr) {
        CheckMenuItem(g_menuState, 40001, MF_BYCOMMAND |
                      (g_autoReload != 0 ? MF_CHECKED : MF_UNCHECKED));
    }
}

void SyncAutoSaveMenu()
{
    if (g_menuState != nullptr) {
        CheckMenuItem(g_menuState, 40022, MF_BYCOMMAND |
                      (g_emmAutoSave != 0 ? MF_CHECKED : MF_UNCHECKED));
    }
}

// [FUN_180009080 tail] mirror the "\r\n"-joined log text into the log dialog.
void MmeLogMirrorCallback(const char* joinedText)
{
    if (g_logWindow == nullptr || joinedText == nullptr) {
        return;
    }
    HWND edit = GetDlgItem(g_logWindow, 0x3e9);   // control 1001
    if (edit == nullptr) {
        return;
    }
    int len = GetWindowTextLengthA(edit);
    SendMessageA(edit, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
    SendMessageA(edit, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(joinedText));
}

// The log dialog proc (DAT_180008c70; disassembly-verified): WM_INITDIALOG
// installs the WH_GETMESSAGE hook (FUN_180008bf0, DAT_1800d99d0), lays the
// edit out at (8,8,w-16,h-42) and the Close button at (w/2-38,h-29,76,23),
// sets both icons (WM_SETICON 0/1, resource 0x68) and backfills the history;
// WM_SIZE repeats the layout; WM_COMMAND 1/2 destroy; WM_DESTROY unhooks.
namespace {

LRESULT CALLBACK MmeLogGetMessageProc(int code, WPARAM wParam, LPARAM lParam)
{
    // [FUN_180008bf0]
    if (code >= 0 && wParam == 1 /*PM_REMOVE*/ && g_logWindow != nullptr) {
        MSG* message = reinterpret_cast<MSG*>(lParam);
        if (message != nullptr && message->message >= 0x100 &&
            message->message <= 0x109) {
            if (IsDialogMessageA(g_logWindow, message)) {
                message->message = 0;
                message->lParam = 0;
                message->wParam = 0;
            }
        }
    }
    return CallNextHookEx(g_logHook, code, wParam, lParam);
}

void MmeLogApplyLayout(HWND dlg, LONG width, LONG height)
{
    HWND edit = GetDlgItem(dlg, 0x3e9);   // 1001
    HWND close = GetDlgItem(dlg, 2);
    if (edit != nullptr) {
        MoveWindow(edit, 8, 8, width - 16, height - 42, TRUE);
    }
    if (close != nullptr) {
        MoveWindow(close, width / 2 - 38, height - 29, 76, 23, TRUE);
    }
}

} // namespace

INT_PTR CALLBACK MmeLogDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG: {
        g_logHook = SetWindowsHookExA(WH_GETMESSAGE, MmeLogGetMessageProc,
                                      nullptr, GetCurrentThreadId());
        MmeSetLogMirror(MmeLogMirrorCallback);
        // Backfill the text accumulated before the window opened.
        {
            HWND edit = GetDlgItem(dlg, 0x3e9);   // control 1001
            std::string existing = MmeGetLogDialogText();
            if (edit != nullptr && !existing.empty()) {
                SendMessageA(edit, EM_SETSEL, 0, 0);
                SendMessageA(edit, EM_REPLACESEL, 0,
                             reinterpret_cast<LPARAM>(existing.c_str()));
            }
        }
        HICON icon = LoadIconA(g_hInst, MAKEINTRESOURCEA(IDI_MME_APP));
        if (icon != nullptr) {
            SendMessageA(dlg, WM_SETICON, 0, (LPARAM)icon);
            SendMessageA(dlg, WM_SETICON, 1, (LPARAM)icon);
        }
        RECT rc;
        GetWindowRect(dlg, &rc);
        MmeLogApplyLayout(dlg, rc.right - rc.left, rc.bottom - rc.top);
        return TRUE;
    }
    case WM_SIZE:
        MmeLogApplyLayout(dlg, static_cast<LONG>(LOWORD(lParam)),
                          static_cast<LONG>(HIWORD(lParam)));
        return TRUE;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        // The original paints the read-only log edit on a white background
        // (CreateSolidBrush(0xffffff) cached at DAT_1800da120).
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, 0);
        SetBkColor(dc, 0xffffff);
        if (g_logBrush == nullptr) {
            g_logBrush = CreateSolidBrush(0xffffff);
        }
        return (INT_PTR)g_logBrush;
    }
    case WM_COMMAND:
        // [0x180008d34] ids 1 and 2 destroy the modeless window.
        if (LOWORD(wParam) == 1 || LOWORD(wParam) == 2) {
            DestroyWindow(dlg);
            return TRUE;
        }
        break;
    case WM_DESTROY:
        if (g_logHook != nullptr) {
            UnhookWindowsHookEx(g_logHook);
            g_logHook = nullptr;
        }
        MmeSetLogMirror(nullptr);
        if (g_logWindow == dlg) {
            g_logWindow = nullptr;
        }
        return 0;
    default:
        break;
    }
    return FALSE;
}

// [FUN_180055800] the keyboard hook: Ctrl+Shift+E posts WM_COMMAND(40006).
LRESULT CALLBACK MmeKeyboardHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0 && wParam == 0x45 /* 'E' */) {
        SHORT ctrl = GetAsyncKeyState(0x11);
        if (ctrl < 0) {
            SHORT shift = GetAsyncKeyState(0x10);
            if (shift < 0 && (lParam & 0xc0000000) == 0 && g_mainWindow != nullptr) {
                PostMessageA(g_mainWindow, WM_COMMAND, 40006, 0);
            }
        }
    }
    return CallNextHookEx(MmeUiGetKeyboardHook(), code, wParam, lParam);
}

// --- [FUN_180055b10] the mouse tail -----------------------------------------
// WM_MOUSEMOVE / button-down messages recompute the NDC cursor position from
// the BeginScene viewport (x: (px - vpX)*2/vpW - 1, y: negated), the button
// downs record {x, y, 1.0, frameTimeBase} and the ups zero the button's z
// slot. Constants byte-verified: DAT_1800b9bb8 = 2.0, DAT_1800b9bb0 = 1.0,
// DAT_1800b9ba0 = the sign mask, DAT_1800b5b28 = 1.0f.
void MmeMouseUpdateNdc(LPARAM lParam)
{
    UINT width = g_beginViewport.Width != 0 ? g_beginViewport.Width : 1;
    UINT height = g_beginViewport.Height != 0 ? g_beginViewport.Height : 1;
    int px = (int)(unsigned short)LOWORD(lParam) - (int)g_beginViewport.X;
    int py = (int)(unsigned short)HIWORD(lParam) - (int)g_beginViewport.Y;
    double x = (double)px * 2.0 / (double)width - 1.0;
    double y = (double)py * 2.0 / (double)height - 1.0;
    g_mouseX = static_cast<float>(x);
    g_mouseY = -static_cast<float>(y);   // the xorpd sign flip of 0x1800b9ba0
}

void MmeMouseTail(HWND window, UINT msg, LPARAM lParam)
{
    (void)window;
    switch (msg) {
    case WM_MOUSEMOVE:       // 0x200
    case WM_LBUTTONDOWN:     // 0x201
    case WM_RBUTTONDOWN:     // 0x204
    case WM_MBUTTONDOWN:     // 0x207
        MmeMouseUpdateNdc(lParam);
        break;
    default:
        break;               // other messages keep the stored position
    }
    switch (msg) {
    case WM_LBUTTONDOWN:     // 0x201 -> DAT_1800d9bc8/bcc/bd0/bd4
        g_mouseClickPos[0][0] = g_mouseX;
        g_mouseClickPos[0][1] = g_mouseY;
        g_mouseClickZ[0] = 1.0f;            // DAT_1800b5b28
        g_mouseClickTime[0] = g_frameTimeBase;   // DAT_1800d9904
        break;
    case WM_LBUTTONUP:       // 0x202
        g_mouseClickZ[0] = 0.0f;
        break;
    case WM_RBUTTONDOWN:     // 0x204 -> DAT_1800d9bd8/bdc/be0/be4
        g_mouseClickPos[1][0] = g_mouseX;
        g_mouseClickPos[1][1] = g_mouseY;
        g_mouseClickZ[1] = 1.0f;
        g_mouseClickTime[1] = g_frameTimeBase;
        break;
    case WM_RBUTTONUP:       // 0x205
        g_mouseClickZ[1] = 0.0f;
        break;
    case WM_MBUTTONDOWN:     // 0x207 -> DAT_1800d9be8/bec/bf0/bf4
        g_mouseClickPos[2][0] = g_mouseX;
        g_mouseClickPos[2][1] = g_mouseY;
        g_mouseClickZ[2] = 1.0f;
        g_mouseClickTime[2] = g_frameTimeBase;
        break;
    case WM_MBUTTONUP:       // 0x208
        g_mouseClickZ[2] = 0.0f;
        break;
    default:
        break;
    }
}

// --- [FUN_180055b10] the main-window subclass proc --------------------------
// Structure: the main window alone dispatches WM_COMMAND (0x104 -> menu
// reinstall, 40001/40002/40003/40004/40005/40006/40022; 0x9c47..0x9c55 -
// including 40020, the EN "Enable Effect" id, which the original dispatch
// ignores - fall through); the mouse tail runs for every subclassed window
// (the main window and the BeginScene "drawn" window); everything reaches
// CallWindowProc.
LRESULT CALLBACK MmeMainWindowProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_COMMAND && window == g_mainWindow) {
        MmeContext* ctx = g_context;
        switch (LOWORD(wParam)) {
        case 0x104:   // the host language menu item: re-probe with the flip
            MmeUiUninstallMenu();
            MmeUiInstallHooks(true);
            break;
        case 40001: { // 0x9c41 - Auto Reload (GetMenuState-driven flip)
            UINT state = g_menuState != nullptr
                             ? GetMenuState(g_menuState, 40001, MF_BYCOMMAND)
                             : 0;
            g_autoReload = (state & MF_CHECKED) != 0 ? 0u : 1u;
            SyncAutoReloadMenu();
            break;
        }
        case 40002:   // 0x9c42 - Log Window
            MmeUiOpenLogWindow();
            return 0;
        case 40003: { // 0x9c43 - About MikuMikuEffect (MessageBoxIndirectA,
                      // MB_USERICON 0x68, DAT_1800b5708/b5740)
            std::string text = "MikuMikuEffect ver0.37 in MikuMikuDance\n(by ";
            // 0x1800b5708+32: Shift-JIS author name (95 91 97 CD 89 EE 93 FC
            // 82 6F), byte-verified against the original About text.
            text += "\x95\x91\x97\xcd\x89\xee\x93\xfc\x82\x6f";
            text += ")";
            MSGBOXPARAMSA params;
            memset(&params, 0, sizeof(params));
            params.cbSize = sizeof(params);
            params.hwndOwner = g_mainWindow;
            params.hInstance = g_hInst;
            params.lpszText = text.c_str();
            params.lpszCaption = "About MikuMikuEffect";
            params.dwStyle = MB_USERICON;               // 0x80
            params.lpszIcon = MAKEINTRESOURCEA(IDI_MME_APP);
            MessageBoxIndirectA(&params);
            return 0;
        }
        case 40004:   // 0x9c44 - Reload All (FUN_18002def0)
            MmeUiReloadAllEffects();
            return 0;
        case 40005:   // 0x9c45 - open the assignment dialog
            MmeUiOpenAssignmentDialog();
            return 0;
        case 40006: { // 0x9c46 - Enable Effect (JP menu + the hotkey post)
            UINT state = g_menuState != nullptr
                             ? GetMenuState(g_menuState, 40006, MF_BYCOMMAND)
                             : 0;
            if (ctx != nullptr) {
                ctx->effectEnabled = (state & MF_CHECKED) != 0 ? 0 : 1;
            }
            SyncEffectToggleMenu();
            return 0;
        }
        case 40020:   // 0x9c54 - Enable Effect (the EN menu id): the original
                      // sub_180055b10 dispatch (0x180055bf0 switch) has no
                      // 0x9c54 case, so the command falls through to the mouse
                      // tail and the stored proc untouched. Pass it through
                      // (the menu item exists but the dispatch is a no-op).
            break;
        case 40022: { // 0x9c56 - Auto Save Mapping File + the ini write-back
            UINT state = g_menuState != nullptr
                             ? GetMenuState(g_menuState, 40022, MF_BYCOMMAND)
                             : 0;
            unsigned char previous = g_emmAutoSave;
            g_emmAutoSave = (state & MF_CHECKED) != 0 ? 0u : 1u;
            SyncAutoSaveMenu();
            if (previous != g_emmAutoSave) {
                WriteIniBool("EMMAutoSave", g_emmAutoSave != 0);
            }
            return 0;
        }
        default:      // 0x9c47..0x9c55 and everything else: pass through
            break;
        }
    }
    MmeMouseTail(window, msg, lParam);
    // [tail 0x180056136] the main window calls its stored proc; the offscreen
    // window calls DAT_1800d9b18 when present, otherwise returns 0.
    if (window == g_mainWindow) {
        return CallWindowProcA(reinterpret_cast<WNDPROC>(g_mainOriginalWndProc),
                               window, msg, wParam, lParam);
    }
    if (window == g_offscreenWindow && g_offscreenOriginalWndProc != 0) {
        return CallWindowProcA(reinterpret_cast<WNDPROC>(g_offscreenOriginalWndProc),
                               window, msg, wParam, lParam);
    }
    return 0;
}

} // namespace

// Process lifetime is explicit now that the engine is linked into the host.
void MmeUiInitializeRuntime(HINSTANCE instance)
{
    if (g_hInst != nullptr) return;
    g_hInst = instance;
    GdiplusStartupInput startupInput;
    GdiplusStartup(&g_gdiplusToken, &startupInput, nullptr);
}

void MmeUiShutdownRuntime()
{
    if (g_gdiplusToken != 0) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
    g_hInst = nullptr;
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

HHOOK MmeUiGetKeyboardHook()
{
    return g_cbtHook;
}

void MmeUiInstallOffscreenSubclass(HWND drawnWindow)
{
    // [OnBeginScene L46-61 / big-C 70544-70551] swap the subclass on the
    // drawn window (the same 0x180055b10 proc as the main window).
    if (drawnWindow == nullptr) {
        return;
    }
    if (g_offscreenWindow == drawnWindow) {
        return;
    }
    if (g_offscreenWindow != nullptr && g_offscreenOriginalWndProc != 0) {
        SetWindowLongPtrA(g_offscreenWindow, GWLP_WNDPROC, g_offscreenOriginalWndProc);
        g_offscreenOriginalWndProc = 0;
        g_offscreenWindow = nullptr;
    }
    if (drawnWindow != nullptr) {
        g_offscreenWindow = drawnWindow;
        LONG_PTR previous = GetWindowLongPtrA(drawnWindow, GWLP_WNDPROC);
        if (previous != reinterpret_cast<LONG_PTR>(MmeMainWindowProc)) {
            g_offscreenOriginalWndProc = previous;
            SetWindowLongPtrA(drawnWindow, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(MmeMainWindowProc));
        }
    }
}

bool MmeUiInstallHooks(bool invertLanguageFlag)
{
    // [FUN_180055890] the original derefs DAT_1800d9b00 unconditionally; the
    // null guard keeps a host without a main window alive (divergence guard).
    if (g_mainWindow == nullptr) {
        return true;
    }

    // [Initialize 0x180056873-0x18005687a] subclass the main window with
    // 0x180055b10 BEFORE the menu install runs (sub_180055890 is called at
    // 0x180056882, after the SetWindowLongPtrA pair; the subclass also
    // survives the menu-install early-outs, like the original). The original
    // subclasses exactly once - the 0x104 language reinstall only re-runs the
    // menu install - so the already-installed probe (the same pattern as
    // MmeUiInstallOffscreenSubclass) keeps the reinstall path from chaining
    // the proc onto itself.
    if (GetWindowLongPtrA(g_mainWindow, GWLP_WNDPROC) !=
        reinterpret_cast<LONG_PTR>(MmeMainWindowProc)) {
        g_mainOriginalWndProc = SetWindowLongPtrA(
            g_mainWindow, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(MmeMainWindowProc));
    }

    HMENU menu = GetMenu(g_mainWindow);
    if (menu == nullptr) {
        return true;   // [L69690] no menu bar: the install no-ops
    }

    // Language resolve [L69691-69735].
    bool japanese = true;
    if (g_englishModeFn != nullptr) {
        japanese = g_englishModeFn() == 0;      // DAT_1800d99dd = (english == 0)
    } else {
        char text[32];
        memset(text, 0, sizeof(text));
        // [0x180055938/0x180055960] GetMenuStringA(menu, 0x104, ..., 0x20, 0)
        // If it succeeds and strstr finds "English", v4=1=Japanese. MMD's language menu shows
        // the language to switch to: English means the current UI is Japanese, so load Japanese menus
        // 101/0x65; only a successful probe without "English" (a Japanese item, meaning English UI) loads
        // English menus 107/0x6b. This agrees with v4 (=byte_1800D99DD, 1=Japanese).
        if (GetMenuStringA(menu, 0x104, text, 0x20, MF_BYCOMMAND) != 0) {
            japanese = strstr(text, "English") != nullptr;
        }
        // Divergence guard: when the 0x104 probe is unavailable the original
        // returns 0 and skips the install entirely (0x180055940); the port
        // keeps the Japanese default and installs so a host without that menu
        // still gets the MMEffect menu.
    }
    if (invertLanguageFlag) {
        japanese = !japanese;                    // [L69733-69735]
    }
    g_uiJapaneseFlag = japanese ? 1 : 0;

    // [L69736-69745] LoadMenuA(0x65 = 101 JP / 0x6b = 107 EN).
    unsigned int menuId = japanese ? IDR_MME_MENU_JP : IDR_MME_MENU_EN;
    g_menuState = LoadMenuA(g_hInst, MAKEINTRESOURCEA(menuId));
    if (g_menuState == nullptr) {
        return true;   // keep Initialize alive; the UI stays uninstalled
    }

    // [L69740-69758] the popup record: fMask 0x146 =
    // MIIM_FTYPE|MIIM_STRING|MIIM_SUBMENU|MIIM_ID (no MIIM_STATE);
    // MFT_RIGHTJUSTIFY + wID 0x65/0x6b.
    MENUITEMINFOA info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU | MIIM_ID;  // 0x146
    info.fType = MFT_RIGHTJUSTIFY;               // 0x4000 - right end of the bar
    info.wID = japanese ? 0x65 : 0x6b;           // the Cleanup DeleteMenu ids
    info.dwTypeData = const_cast<LPSTR>("MMEffect");
    info.cch = 8;
    info.hSubMenu = g_menuState;
    if (!InsertMenuItemA(menu, 100, TRUE, &info)) {
        return true;
    }

    // [L69760-69774] the toggle checkmarks.
    SyncAutoReloadMenu();     // 40001 <- DAT_1800d72e1 (g_autoReload)
    SyncAutoSaveMenu();       // 40022 <- g_bAutoSave (g_emmAutoSave)
    DrawMenuBar(g_mainWindow);

    // [Initialize 70061] SetWindowsHookExA(2 /*WH_KEYBOARD*/, FUN_180055800,
    // NULL, current thread). (The analysis notes label it "WH_CBT"; hook id 2
    // is WH_KEYBOARD and the proc body matches a keyboard filter.)
    if (g_cbtHook == nullptr) {
        g_cbtHook = SetWindowsHookExA(WH_KEYBOARD, MmeKeyboardHookProc, nullptr,
                                      GetCurrentThreadId());
    }
    return true;
}

void MmeUiUninstallMenu()
{
    // [FUN_180055ab0] DeleteMenu(0x65) + DeleteMenu(0x6b) + DrawMenuBar.
    if (g_mainWindow == nullptr) {
        return;
    }
    HMENU menu = GetMenu(g_mainWindow);
    if (menu != nullptr) {
        DeleteMenu(menu, 0x65, MF_BYCOMMAND);
        DeleteMenu(menu, 0x6b, MF_BYCOMMAND);
    }
    g_menuState = nullptr;
    DrawMenuBar(g_mainWindow);
}

void MmeUiShutdown()
{
    // Modeless dialogs first (their WM_DESTROY unregisters the log mirror /
    // the WH_GETMESSAGE hook of the assignment dialog).
    if (g_logWindow != nullptr) {
        HWND log = g_logWindow;
        g_logWindow = nullptr;
        DestroyWindow(log);
    }
    MmeDlgCloseAssignmentDialog();
}

void MmeUiOpenLogWindow()
{
    // [FUN_180008fc0]
    if (g_logWindow != nullptr) {
        SetFocus(g_logWindow);
        return;
    }
    g_logWindow = CreateDialogParamA(g_hInst, MAKEINTRESOURCEA(IDD_MME_LOG), g_mainWindow,
                                     MmeLogDlgProc, 0);
    if (g_logWindow == nullptr) {
        return;
    }
    HWND edit = GetDlgItem(g_logWindow, 0x3e9);   // 1001
    // [L8687-8688] EM_GETLINECOUNT(0xba) -> EM_LINESCROLL(0xb6, 0, count):
    // scroll the log edit to its last line.
    LRESULT lines = SendMessageA(edit, EM_GETLINECOUNT, 0, 0);
    SendMessageA(edit, EM_LINESCROLL, 0, static_cast<LPARAM>(lines));
    ShowWindow(g_logWindow, SW_SHOW);             // [L8689] 5 = SW_SHOW
    UpdateWindow(g_logWindow);                    // [L8690]
    SetFocus(g_logWindow);                        // [L8693]
}

void MmeUiOpenAssignmentDialog()
{
    // [FUN_180055b10 40005] CreateDialogParamA(108) - mme_dlg owns the proc.
    MmeDlgOpenAssignmentDialog();
}

void MmeUiReloadAllEffects()
{
    // [40004 = FUN_18002DEF0] Flag-based "reload all" semantics (behavioral port).
    //
    // The original does only two things (@0x18002def0-0x18002df6d):
    //   1. Traverse the manager's binding tree (qword_1800D9A40+0xA0) in reverse, marking each live
    //      0x1D8 binding object with *(WORD*)(binding+56)=0x100 (@0x18002df0f):
    //      the bits mean loaded(+56)=0 / unloadRequest(+57)=1; the working map (assignment table)
    //      remains byte-for-byte unchanged;
    //   2. sub_18000A990 clears the effect/texture cache trees (headers 0x1800D9C68 /
    //      0x1800D9C88), dropping only the cache-side shared_ptr references.
    // Each (owner=0, object, subset) binding node (including all [n] subset nodes) consumes the flag on the next
    // apply via sub_18000B880 (@0x18000b8e6-0x18000b930): first
    // sub_18000B210 releases the old entry reference; then loaded=0 takes the initial-load path sub_18000BC90
    // to recompile (the cleared cache guarantees a miss), cache the new entry, and bind it.
    //
    // This port resolves bindings at assignment time rather than polling each frame, so run the same consumption chain synchronously here:
    //   MmeEngineClearCaches()            —— sub_18000A990；
    //   MmeReleaseEffectBindings("")      -- flag consumption/reference release half (B210);
    //                                         the old entry dies with its last owner reference,
    //                                         producing the "Unload effect file" log;
    //   MmeRebindEffectAssignments("")    -- flag consumption/reload half (BC90);
    //                                         rebuild bindings from the current assignment table (effectFile +
    //                                         subsetEffects).
    // Never call MmeAssignEffect: whole-object reassignment calls clearSubsetEffects(), erasing all
    // [n] material subset assignments without rebuilding them (the porting defect fixed here). Preserve subset assignments unchanged;
    // re-resolution rebuilds each (0, model, [n]) binding from its existing value.
    MmeEngineClearCaches();                     // [sub_18000A990]
    MmeReleaseEffectBindings(std::string());    // Consume the unloadRequest bit
    MmeRebindEffectAssignments(std::string());  // Consume the loaded=0 bit
}

void MmeUiTickAutoReload()
{
    // [sub_18000B880 @0x18000b93c-0x18000b9b2] Automatic reload polling. The original polls each binding node
    // separately: each (owner=0, object, subset) node has its own 100ms throttle
    // (binding+64) and stamp/grace state, driven by MME_RebuildRenderPassPlan
    // (0x18005b9e0) → sub_18002BAA0 (@0x18002be1f) → sub_18002C910 →
    // sub_18002CA80 (whole object @0x18002cede; [n] subsets @0x18002d0ef, only
    // objects with scriptOrder(+0x364)==0, one call per material (+0x38)) -> sub_18000B880.
    // This port groups polling by file (all bindings for one .fx share stamp/grace state).
    // MmeReleaseEffectBindings / MmeRebindEffectAssignments select affected bindings
    // by path: one recompilation per .fx, with multiple bindings sharing the cache entry, matching the original's
    // observable (path,stamp) cache-hit behavior (one log entry for each action).
    struct WatchState {
        unsigned long stamp;   // binding+40: file timestamp from sub_18000B7F0
        bool grace;            // binding+59: file-deletion grace flag
    };
    static DWORD s_lastCheck = 0;
    static std::map<std::string, WatchState> s_stamps;

    if (g_autoReload == 0) {                        // [0x18000b93c] byte_1800D72E1 gate
        // The original gate skips only the stamp check (jumps to LABEL_22), preserving the binding's stamp (+40) and
        // throttle time (+64). Files changed while disabled therefore reload on the
        // first poll after re-enabling. Skip here without clearing state.
        return;
    }
    DWORD now = GetTickCount();
    if (s_lastCheck != 0 && now - s_lastCheck < 100) {   // [0x18000b952] 100ms throttle
        return;
    }
    s_lastCheck = now;

    MmeContext* ctx = g_context;
    if (ctx == nullptr) {
        return;
    }

    // Watch set = .fx files held by live bindings (B880's nonempty a1+88 path gate): each model's whole-object
    // effectFile + all [n] subset assignments. EMM "Pmd<N>[k]" rows and EMD "Obj[k]" rows
    // share the subsetEffects table, and both are polled by the original (sub_18002C910 drives
    // CA80 per subset for scriptOrder==0 objects). Cache entries referenced by no binding
    // (e.g. unused (default) row values; models automatically assigned through MmeFindEffectFileForModel
    // already have effectFile and need no separate entry here) are not polled, matching the original.
    std::vector<std::string> watched;
    for (size_t i = 0; i < ctx->models.size(); ++i) {
        ModelData* model = ctx->models[i];
        if (model == nullptr) {
            continue;
        }
        const std::string& whole = model->effectFile();
        if (!whole.empty() &&
            std::find(watched.begin(), watched.end(), whole) == watched.end()) {
            watched.push_back(whole);
        }
        for (std::map<int, std::string>::const_iterator it = model->subsetEffects().begin();
             it != model->subsetEffects().end(); ++it) {
            if (!it->second.empty() &&
                std::find(watched.begin(), watched.end(), it->second) == watched.end()) {
                watched.push_back(it->second);
            }
        }
    }

    for (size_t i = 0; i < watched.size(); ++i) {
        const std::string& file = watched[i];
        unsigned long stamp = MmeEngineQueryFileStamp(file);   // [sub_18000B7F0]
        std::map<std::string, WatchState>::iterator prev = s_stamps.find(file);
        if (prev == s_stamps.end()) {
            // On first observation, only record the state (the engine already stored the binding's stamp in the entry at load time).
            WatchState state;
            state.stamp = stamp;
            state.grace = false;
            s_stamps[file] = state;
            continue;
        }
        if (stamp == prev->second.stamp) {
            continue;   // [0x18000b99a] Unchanged
        }
        if (stamp != 0) {
            // [0x18000b99c-0x18000b9b2] A stamp change (including restoration after deletion) reloads immediately:
            // unload the cache entry, release old binding references, and re-resolve current assignments (B210 +
            // BC90 chain; assignment map unchanged). Reload always passes through B210 (trailing WORD@+59=0x100),
            // resetting the grace flag.
            prev->second.stamp = stamp;
            prev->second.grace = false;
            MmeEngineUnloadEffectFile(file);
            MmeReleaseEffectBindings(file);
            MmeRebindEffectAssignments(file);
        } else if (!MmeAnyEffectBindingForPath(file)) {
            // [0x18000b972 `if (!*(a1))`] No binding referencing this file has an effect
            // (previous load failure/already unloaded): the original returns silently without advancing stamp/grace
            // state (+40 keeps its old value; restoring an identical file does not retry, matching the original).
            continue;
        } else if (prev->second.grace) {
            // [0x18000b978-0x18000b990] Still missing on the second cycle: unload (bindings lose their
            // effects, assignments remain) and zero the stamp (+40=0); the unload chain (end of B210)
            // clears the grace flag. Do not re-resolve: the file is still missing, so reloading would take sub_18000BC90's
            // silent stamp==0 early return, leaving bindings without effects until the file returns.
            prev->second.stamp = 0;
            prev->second.grace = false;
            MmeEngineUnloadEffectFile(file);
            MmeReleaseEffectBindings(file);
        } else {
            // [0x18000b97e] First missing cycle: set only the grace flag (preserve the old stamp;
            // restoration before the next cycle takes the change branch above without unloading).
            prev->second.grace = true;
        }
    }
    // Cleanup after assignment removal/model deletion: watch entries disappear with the original working-map nodes.
    for (std::map<std::string, WatchState>::iterator it = s_stamps.begin();
         it != s_stamps.end(); ) {
        if (std::find(watched.begin(), watched.end(), it->first) == watched.end()) {
            it = s_stamps.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace mme
