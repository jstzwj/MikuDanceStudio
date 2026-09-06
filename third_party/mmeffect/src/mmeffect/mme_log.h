// mme_log.h - MMEffect log writer (port of the FUN_180009080 family).
//
// Evidence:
//   - big-C 8701-8903 (FUN_180009080): splits the input text on '\n'/'\r',
//     accumulates whole lines, appends the "\r\n"-joined text to the log dialog
//     edit control (control id 0x3e9) when the dialog exists, and when
//     showMessageBox is set dedups the joined text against a std::set<std::string>
//     (DAT_1800d9c18 tree) before MessageBoxA(main, text, "MikuMikuEffect",
//     MB_ICONERROR).
//   - big-C 8929-8959 (FUN_1800094e0): flush gate keyed on a phase byte
//     (DAT_1800d99d8). Exports call it with 1 at scene/model boundaries and
//     with 0 at OnEndScene; when the phase byte changes the accumulated state
//     is drained and the byte is updated.
//   - strings_evidence.md section 8 for the message inventory.
//
// Divergence (documented in PHASE1_IMPLEMENTATION_NOTES.md): the recovered
// decompile of the FUN_180009080 family contains no file I/O and no
// "MMEffect.txt" string - in the original the log surface is the log dialog.
// Phase 1 adds a file sink (MMEffect.txt in the host exe directory, which is
// also where MMEffect.ini lives) written at the FUN_1800094e0 flush points,
// because the Phase 1 acceptance checks require the banner/lifecycle lines in
// MMEffect.txt. The dialog mirror below is the seam where the real log dialog
// (Phase 2 mme_dlg) will attach.
#pragma once

namespace mme {

// [0x180009080] Write `text` into the log.
//   showMessageBox != 0 -> MessageBoxA(g_mainWindow, joinedLines,
//   "MikuMikuEffect", MB_ICONERROR). While the flush phase is active
//   (DAT_1800d99d8 != 0, between OnBeginScene and OnEndScene) the message is
//   deduped against the shown-message list first; outside the scene it shows
//   every time (big-C 8851-8889).
//   Lines are split on '\n' and '\r' ("\r\n" collapses to one break).
void MmeLogWrite(const char* text, int showMessageBox);

// [0x1800094e0] Flush gate. When `phase` differs from the current phase byte:
// write all pending lines to MMEffect.txt, clear the pending list, clear the
// MessageBox dedup set, and update the phase byte.
void MmeLogFlush(unsigned char phase);

// Opens/creates "<directory>\MMEffect.txt" (truncating - one file per process
// lifetime, like the original's per-session dialog text). Called by Initialize
// right after the module paths are computed.
void MmeLogInit(const char* directory);

// Phase 2 seam: the real log dialog (mme_dlg) registers a mirror that receives
// the "\r\n"-joined text of every MmeLogWrite call (what the original sends to
// the 0x3e9 edit control). nullptr disables mirroring.
typedef void (*MmeLogMirrorFn)(const char* joinedText);
const char* MmeGetLogDialogText();
void MmeSetLogMirror(MmeLogMirrorFn fn);

} // namespace mme
