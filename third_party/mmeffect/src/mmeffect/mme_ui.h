// mme_ui.h - host menu injection + window hooks (the 0x180055890 /
// 0x180055800 / 0x180055b10 cluster and the log window FUN_180008fc0).
#pragma once

#include <windows.h>

namespace mme {

extern HINSTANCE g_hInst;   // host module containing the embedded MME resources
void MmeUiInitializeRuntime(HINSTANCE instance);
void MmeUiShutdownRuntime();

// [0x180055890] FUN_180055890 - menu install: LoadMenuA(101 JP / 107 EN) per
// the ExpGetEnglishMode resolution, insert the "MMEffect" popup at the right
// end of the host menu bar (MFT_RIGHTJUSTIFY, position 100), CheckMenuItem the
// 40001/40022 toggles, DrawMenuBar; then subclass the main window with
// 0x180055b10 and install the keyboard hook (SetWindowsHookExA(2, ...) =
// WH_KEYBOARD, FUN_180055800). `invertLanguageFlag` != 0 flips the resolved
// mode first (the param_1 toggle of the original).
bool MmeUiInstallHooks(bool invertLanguageFlag);

// The keyboard hook handle for Cleanup's UnhookWindowsHookEx (DAT_1800d9b28).
HHOOK MmeUiGetKeyboardHook();

// [Initialize L46-61 / OnBeginScene offscreen branch] the drawn (offscreen)
// window is subclassed with the SAME proc (0x180055b10, big-C 70551); the
// previous proc is restored first when one was installed.
void MmeUiInstallOffscreenSubclass(HWND drawnWindow);

// [FUN_180055ab0] the menu uninstall: DeleteMenu(0x65) + DeleteMenu(0x6b) +
// DrawMenuBar and clear the stored popup (DAT_1800d9b20 = 0). The 0x104
// language re-probe calls this before reinstalling.
void MmeUiUninstallMenu();

// [Cleanup L57-74 counterpart] destroy the modeless MME dialogs (log window +
// assignment dialog). The menu/hook/window-proc restoration stays at the
// Cleanup call site (callbacks.cpp).
void MmeUiShutdown();

// [FUN_180008fc0] the 40002 log window: CreateDialogParamA(105) modeless +
// scroll the edit to the end (EM_GETLINECOUNT/EM_LINESCROLL) + focus.
void MmeUiOpenLogWindow();

// [40005] open the assignment dialog (dialog 108; implemented in mme_dlg).
void MmeUiOpenAssignmentDialog();

// [40004] force-reload every assigned effect file (unload + reload + re-apply
// through the emm manager) and re-load the "(default)" row effect.
void MmeUiReloadAllEffects();

// [40001 watch half] the 100 ms GetTickCount-gated file-stamp poll of
// FUN_18000b880: reload an assigned effect when its stamp changes. Driven
// from the per-frame animated-texture tick (OnEndScene cadence).
void MmeUiTickAutoReload();

} // namespace mme
