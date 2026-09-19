// mme_log.h - MMEffect log writer (port of the FUN_180009080 family).
//
// Evidence:
//   - big-C 8701-8903 (FUN_180009080): splits the input text on '\n'/'\r',
//     accumulates whole lines into the never-cleared history list
//     (DAT_1800d9c00/DAT_1800d9c08), appends the "\r\n"-joined text to the log
//     dialog edit control (control id 0x3e9) when the dialog exists, and when
//     showMessageBox is set dedups the joined text against a
//     std::set<std::string> (DAT_1800d9c18 tree) before MessageBoxA(main,
//     text, "MikuMikuEffect", MB_ICONERROR).
//   - big-C 8929-8959 (FUN_1800094e0): flush gate keyed on a phase byte
//     (DAT_1800d99d8). Exports call it with 1 at scene/model boundaries and
//     with 0 at OnEndScene; when the phase byte changes ONLY the MessageBox
//     dedup list/set (DAT_1800d9c20/DAT_1800d9c18) is drained and the byte is
//     updated - the line history (DAT_1800d9c00) is never cleared at runtime
//     (its clear routine sub_1800099b0 is only referenced by CRT static
//     teardown), so the log dialog backfills every line of the session.
//   - DialogFunc 0x180008e57-0x180008eeb: WM_INITDIALOG joins the full
//     DAT_1800d9c00 history with "\r\n" into control 1001.
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
// write all lines logged since the last flip to MMEffect.txt, clear that
// per-phase file-sink buffer, clear the MessageBox dedup set (the only state
// the original's 0x1800094e0 drains), and update the phase byte. The dialog
// history list is NOT cleared - like the original's DAT_1800d9c00 list it
// survives phase flips for the whole process lifetime.
void MmeLogFlush(unsigned char phase);

// [0x180058b4a-0x180058bf7 / sub_180009480's gate] the shown-message dedup
// for the direct MessageBoxA call sites that do NOT log the text
// (OnResetDevice's failure box, MME_SasApplyScriptCommand's box). While the
// flush phase is active (DAT_1800d99d8 != 0, inside a scene) the text is
// matched against the shared shown-message set (sub_180009C20 equal_range +
// count) and suppressed when present, inserted (sub_180009750) when not;
// outside the scene it reports true unconditionally and leaves the set
// untouched. Shares g_shownMessages/g_flushPhase with MmeLogWrite, like the
// original's single global set/phase.
bool MmeLogShouldShowMessageBox(const char* text);

// Opens/creates "<directory>\MMEffect.txt" (truncating - one file per process
// lifetime, like the original's per-session dialog text). Called by Initialize
// right after the module paths are computed.
void MmeLogInit(const char* directory);

// Phase 2 seam: the real log dialog (mme_dlg) registers a mirror that receives
// the "\r\n"-joined text of every MmeLogWrite call (what the original sends to
// the 0x3e9 edit control). nullptr disables mirroring.
typedef void (*MmeLogMirrorFn)(const char* joinedText);
// The "\r\n"-joined text of EVERY line logged this process run - the log
// dialog's WM_INITDIALOG backfill source (original: DialogFunc walks the
// never-cleared DAT_1800d9c00 list, 0x180008e57-0x180008eeb).
const char* MmeGetLogDialogText();
void MmeSetLogMirror(MmeLogMirrorFn fn);

} // namespace mme
