// mme_log.cpp - see mme_log.h
#include "mme_log.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "mme_globals.h"

namespace mme {

namespace {

CRITICAL_SECTION g_lock;
bool             g_lockInit = false;

// [big-C 8757-8831] whole-line history (original: std::list at DAT_1800d9c00,
// count DAT_1800d9c08, filled by FUN_180009080 via FUN_180009a40 list inserts).
// The original NEVER drains this list at runtime: the only clear routine
// (sub_1800099b0) is referenced solely by CRT static teardown (sub_1800a3390
// + the .rdata teardown tables), and the phase-flip flush (sub_1800094e0)
// clears only the MessageBox dedup list/set (DAT_1800d9c20/DAT_1800d9c18).
// It is the full-history store the log dialog re-joins ("\r\n") on
// WM_INITDIALOG (DialogFunc 0x180008e57-0x180008eeb, SetDlgItemTextA 1001).
std::vector<std::string> g_historyLines;

// Per-phase buffer for the Phase 1 file sink (MMEffect.txt) - a documented
// divergence: the original has no file sink and needs no per-phase drain, so
// this list is separate from the never-cleared history above.
std::vector<std::string> g_pendingLines;

// [big-C 8777-8782] the "\r\n"-joined dialog text (original: local_a8 string).
std::string g_dialogText;

// [big-C 9033-9056] MessageBox dedup set (original: std::set<std::string> at
// DAT_1800d9c18/DAT_1800d9c20; insert = FUN_180009750, find = FUN_180009c20).
std::set<std::string> g_shownMessages;

// [big-C 8936] the flush phase byte (DAT_1800d99d8).
unsigned char g_flushPhase = 0;

// File sink (Phase 1 divergence - see mme_log.h).
FILE* g_logFile = nullptr;
std::string g_logFilePath;

// Phase 2 seam: log-dialog mirror.
MmeLogMirrorFn g_mirror = nullptr;

void EnsureLock()
{
    if (!g_lockInit) {
        InitializeCriticalSection(&g_lock);
        g_lockInit = true;
    }
}

// [big-C 8941-8956] the original's flush drains the pending state; the Phase 1
// port additionally writes the drained lines into MMEffect.txt (append).
void WritePendingToFile()
{
    if (g_logFile == nullptr || g_pendingLines.empty()) {
        return;
    }
    for (size_t i = 0; i < g_pendingLines.size(); ++i) {
        fputs(g_pendingLines[i].c_str(), g_logFile);
        fputc('\r', g_logFile);
        fputc('\n', g_logFile);
    }
    fflush(g_logFile);
}

} // namespace

void MmeLogInit(const char* directory)
{
    EnsureLock();

    if (directory != nullptr) {
        std::string path = directory;
        if (!path.empty() && path[path.size() - 1] != '\\' && path[path.size() - 1] != '/') {
            path += '\\';
        }
        path += "MMEffect.txt";
        g_logFilePath = path;
        // "wt" = truncate + text mode, matching one file per process run.
        if (fopen_s(&g_logFile, path.c_str(), "wt") != 0) {
            g_logFile = nullptr;
        }
    }
}

const char* MmeGetLogDialogText()
{
    // The log dialog's WM_INITDIALOG backfill source: the "\r\n"-joined text
    // of EVERY line ever logged (the original's DialogFunc walks the whole
    // DAT_1800d9c00 history list - which is never cleared at runtime - not
    // just the lines of the current frame/phase).
    static std::string joined;
    joined.clear();
    for (size_t i = 0; i < g_historyLines.size(); ++i) {
        joined += g_historyLines[i];
        joined += "\r\n";
    }
    return joined.c_str();
}

void MmeSetLogMirror(MmeLogMirrorFn fn)
{
    EnsureLock();
    g_mirror = fn;
}

void MmeLogWrite(const char* text, int showMessageBox)
{
    EnsureLock();
    if (text == nullptr) {
        return;
    }

    // [big-C 8750-8840] split into lines; on every '\n'/'\r' push the
    // accumulated line into the history list (original: single never-cleared
    // DAT_1800d9c00 list; the port doubles as the file-sink buffer below) and
    // append "<line>\r\n" to the dialog text; a '\n' directly after '\r' is
    // swallowed [big-C 8791-8800].
    g_dialogText.clear();
    std::string line;
    for (const char* p = text; *p != '\0'; ++p) {
        char c = *p;
        if (c == '\n' || c == '\r') {
            g_historyLines.push_back(line);
            g_pendingLines.push_back(line);
            g_dialogText += line;
            g_dialogText += "\r\n";
            line.clear();
            if (c == '\r' && p[1] == '\n') {
                ++p;
            }
        } else {
            line += c;
        }
    }
    if (!line.empty()) {
        g_historyLines.push_back(line);
        g_pendingLines.push_back(line);
        g_dialogText += line;
        g_dialogText += "\r\n";
    }

    // [big-C 8841-8850] mirror into the log dialog (original appends to the
    // 0x3e9 edit control via EM_SETSEL/EM_REPLACESEL; Phase 2 installs the
    // real dialog through MmeSetLogMirror).
    if (!g_dialogText.empty() && g_mirror != nullptr) {
        g_mirror(g_dialogText.c_str());
    }

    // [big-C 8851-8890] showMessageBox path. The original dedupes against the
    // shown-message list ONLY while the flush phase is active (DAT_1800d99d8
    // != 0, i.e. between OnBeginScene and OnEndScene); outside the scene the
    // MessageBox shows every time. The list itself is drained on a phase flip
    // (FUN_1800094e0).
    if (showMessageBox != 0) {
        bool show = true;
        if (g_flushPhase != 0) {
            show = g_shownMessages.insert(g_dialogText).second;
        }
        if (show) {
            MessageBoxA(g_mainWindow, g_dialogText.c_str(), "MikuMikuEffect", MB_ICONERROR);
        }
    }
}

void MmeLogFlush(unsigned char phase)
{
    EnsureLock();
    // [big-C 8929-8959] flush only on a phase change; the exports alternate
    // 1 (OnBeginScene/OnCreateModel/OnDeleteModel) and 0 (OnEndScene).
    if (g_flushPhase == phase) {
        return;
    }
    g_flushPhase = phase;

    WritePendingToFile();

    // [big-C 8940-8956 / 0x1800094ea-0x18000958e] the original phase flip
    // drains ONLY the MessageBox dedup list/set (DAT_1800d9c20/DAT_1800d9c18)
    // and its size (DAT_1800d9c28); the line history (DAT_1800d9c00) is left
    // intact so a log dialog opened later still sees every earlier line.
    // g_pendingLines is the Phase 1 file-sink buffer (documented divergence),
    // so it is drained here to write MMEffect.txt per phase.
    g_pendingLines.clear();
    g_dialogText.clear();
    g_shownMessages.clear();
}

bool MmeLogShouldShowMessageBox(const char* text)
{
    EnsureLock();
    if (text == nullptr) {
        return true;
    }
    // [0x180058b4a] byte_1800D99D8 == 0 (outside a scene): the box shows
    // unconditionally and the set is untouched. [0x180058b58-0x180058bd0]
    // otherwise: equal_range + count over the shared set - a hit skips the
    // box, a miss inserts (sub_180009750) so every later repeat is
    // suppressed until the set drains at the next scene boundary.
    if (g_flushPhase == 0) {
        return true;
    }
    return g_shownMessages.insert(std::string(text)).second;
}

} // namespace mme
