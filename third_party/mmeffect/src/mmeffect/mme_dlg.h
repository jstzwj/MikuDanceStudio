// mme_dlg.h - the effect-assignment dialog (dialog template 108 + menus
// 109/110, the FUN_180044080 WndProc port).
#pragma once

#include <windows.h>

namespace mme {

// [40005 / FUN_180055b10] open the modeless assignment dialog
// (CreateDialogParamA, dialog 108). When it already exists, bring it up.
void MmeDlgOpenAssignmentDialog();

// [Cleanup] destroy the dialog when open (its WM_DESTROY unhooks the
// WH_GETMESSAGE hook the original installs in WM_INITDIALOG).
void MmeDlgCloseAssignmentDialog();

// [OK / Apply] the EMM auto-save commit (writes the mapping beside the
// saved PMM when EMMAutoSave is on; assignments are live in memory).
void MmeDlgCommitAssignment();

// [per-frame] rebuild the list when models were registered while the
// dialog is open (driven from the frame tick).
void MmeDlgRefreshIfModelCountChanged();

} // namespace mme
