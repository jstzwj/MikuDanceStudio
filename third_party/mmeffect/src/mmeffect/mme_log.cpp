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

// [big-C 8757-8831] pending whole-line list (original: std::list at DAT_1800d9c00,
// count DAT_1800d9c08, filled by FUN_180009080 via FUN_180009a40 list inserts).
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
    // of every line still pending (the original's FUN_180008c70 walks the
    // DAT_1800d9c00 line list, not just the last message).
    static std::string joined;
    joined.clear();
    for (size_t i = 0; i < g_pendingLines.size(); ++i) {
        joined += g_pendingLines[i];
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
    // accumulated line into the pending list and append "<line>\r\n" to the
    // dialog text; a '\n' directly after '\r' is swallowed [big-C 8791-8800].
    g_dialogText.clear();
    std::string line;
    for (const char* p = text; *p != '\0'; ++p) {
        char c = *p;
        if (c == '\n' || c == '\r') {
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

    // [big-C 8940-8956] drain pending state and reset the dedup set.
    g_pendingLines.clear();
    g_dialogText.clear();
    g_shownMessages.clear();
}

} // namespace mme
