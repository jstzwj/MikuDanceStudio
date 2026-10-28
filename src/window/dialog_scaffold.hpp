// ===========================================================================
// Dialog-procedure scaffold helpers (command_file_menu.cpp /
// command_view_menu.cpp)
// ===========================================================================
// The modal/modeless dialog procedures of the two menu families repeat the
// same WM_INITDIALOG / WM_COMMAND scaffolding line for line: the
// "owned-mode top" SetWindowPos dance gated on the floating render window
// (app+0xA0D38), the EM_REPLACESEL prefill of the edit controls, the
// SetFocus + EM_SETSEL(0, len) select-all pair, and the GetWindowTextA +
// atol/atof read-back of the OK handler.  The helpers below capture exactly
// those sequences - message order and parameters preserved verbatim (each
// dialog's original-VA comment carries the provenance).
//
// A 2026-09 audit re-checked the "top" dance against the x64 build: every
// dialog family raises the dialog to HWND_TOPMOST, never HWND_TOP - the
// x86-only HWND_TOP readings were sign-extension artifacts.  Anchors:
// FrameRange 0x7FF7CB475FE5, camera frame transform 0x7FF7CB4788E3,
// morph frame transform 0x7FF7CB478BC3, model order 0x7FF7CB478383
// (or ebx,-1), physics editor 0x7FF7CB4AE84E.  One helper, one constant.
// =========================================================================//
#ifndef MIKUDANCESTUDIO_WINDOW_DIALOG_SCAFFOLD_HPP
#define MIKUDANCESTUDIO_WINDOW_DIALOG_SCAFFOLD_HPP

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdlib>

#include "mikudancestudio/mmd_app.hpp"

namespace mikudancestudio {

// EM_REPLACESEL prefill of one edit control (the dialogs fill their empty
// edits by replacing the empty selection, not by SetWindowText).
inline void PrefillEdit(HWND hDlg, int controlId, const char* text) {
    SendMessageA(GetDlgItem(hDlg, controlId), EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(text));
}

// SetFocus on the edit, then EM_SETSEL 0..GetWindowTextLength - the
// focus-and-select-all pair every prefilling dialog ends WM_INITDIALOG
// with (message order preserved).
inline void SelectAllEdit(HWND hDlg, int controlId) {
    SetFocus(GetDlgItem(hDlg, controlId));
    SendMessageA(GetDlgItem(hDlg, controlId), EM_SETSEL, 0,
                 GetWindowTextLengthA(GetDlgItem(hDlg, controlId)));
}

// GetWindowTextA + atol / atof read-back of one edit control.  maxChars is
// the original's GetWindowTextA cap (20 at every read site except the
// accessory-order count edit, which reads 256).
inline int ReadIntFromEdit(HWND hDlg, int controlId, int maxChars = 20) {
    char text[0x100];
    GetWindowTextA(GetDlgItem(hDlg, controlId), text, maxChars);
    return static_cast<int>(atol(text));
}

inline float ReadFloatFromEdit(HWND hDlg, int controlId, int maxChars = 20) {
    char text[0x100];
    GetWindowTextA(GetDlgItem(hDlg, controlId), text, maxChars);
    return static_cast<float>(atof(text));
}

// Owned-dialog top dance, shared by every menu family: raise the dialog to
// HWND_TOPMOST (flags 3) when the floating render window exists.  The x64
// insert-after register is always -1 (see header note for the anchors).
inline void MakeDialogTopmostIfRequested(MMDApp* app, HWND hDlg) {
    if (app->FloatingWindow() != nullptr) {
        SetWindowPos(hDlg, HWND_TOPMOST, 0, 0, 0, 0, 3u);
    }
}

} // namespace mikudancestudio

#endif // MIKUDANCESTUDIO_WINDOW_DIALOG_SCAFFOLD_HPP
