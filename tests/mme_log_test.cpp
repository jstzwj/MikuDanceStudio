// Production logging with only its modal Win32 boundary replaced. All file
// assertions use a fresh temporary directory, never a user's MMEffect.txt.
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
static std::vector<std::string> messages;
static std::vector<std::string> mirrored;
static int WINAPI CaptureMessageBox(HWND, LPCSTR text, LPCSTR, UINT) {
    messages.emplace_back(text);
    return IDOK;
}
#define MessageBoxA CaptureMessageBox
#include "../third_party/mmeffect/src/mmeffect/mme_log.cpp"
#undef MessageBoxA
namespace mme { HWND g_mainWindow = nullptr; }
static int failures;
static void Check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}
int main(int argc, char**) {
    char tempRoot[MAX_PATH], directory[MAX_PATH], previous[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tempRoot) ||
        !GetTempFileNameA(tempRoot, "mml", 0, directory)) return 2;
    DeleteFileA(directory);
    if (!CreateDirectoryA(directory, nullptr) ||
        !GetCurrentDirectoryA(MAX_PATH, previous) || !SetCurrentDirectoryA(directory)) return 2;
    const std::string path = std::string(directory)+"\\MMEffect.txt";
    const bool sentinel = argc>1;
    const std::string contents = "Existing documentation must survive.\r\n";
    if (sentinel) { std::ofstream file(path, std::ios::binary); file << contents; }

    // Logging needs no directory or file initialization.
    mme::MmeSetLogMirror([](const char* text) { mirrored.emplace_back(text); });
    mme::MmeLogWrite("first\r\nsecond\nthird\r\nfourth", 0);
    const std::string normalized = "first\r\nsecond\r\nthird\r\nfourth\r\n";
    Check(mirrored.size()==1 && mirrored.back()==normalized, "dialog mirror normalizes line endings");
    Check(mme::MmeGetLogDialogText()==normalized, "full history matches first write");
    mme::MmeLogFlush(1);
    mme::MmeLogWrite("error\n", 1); mme::MmeLogWrite("error\n", 1);
    Check(messages.size()==1, "same-phase modal text is suppressed");
    Check(!mme::MmeLogShouldShowMessageBox("error\r\n"), "direct dialogs share suppression set");
    mme::MmeLogFlush(1);
    Check(!mme::MmeLogShouldShowMessageBox("error\r\n"), "same phase does not clear suppression");
    mme::MmeLogFlush(0);
    mme::MmeLogWrite("error\n", 1); mme::MmeLogWrite("error\n", 1);
    Check(messages.size()==3, "outside phase every modal is displayed");
    mme::MmeLogFlush(1);
    mme::MmeLogWrite("error\n", 1);
    Check(messages.size()==4, "new phase displays again");
    std::string expected = normalized;
    for (int i=0;i<5;++i) expected += "error\r\n";
    Check(mme::MmeGetLogDialogText()==expected, "phase flips retain history including suppressed errors");
    Check(mirrored.size()==6, "each write reaches dialog mirror");
    mme::MmeSetLogMirror(nullptr);
    mme::MmeLogWrite("last", 0);
    mme::MmeLogFlush(0);
    Check(mirrored.size()==6, "detached mirror remains detached");
    Check(mme::MmeGetLogDialogText()==expected+"last\r\n", "detached dialog can backfill full history");

    // Observe real filesystem state after all logging and phase changes.
    if (sentinel) {
        std::ifstream file(path, std::ios::binary);
        const std::string actual((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        Check(actual==contents, "existing MMEffect.txt is not truncated or appended");
    } else {
        Check(GetFileAttributesA(path.c_str())==INVALID_FILE_ATTRIBUTES,
            "logging does not create MMEffect.txt");
    }
    SetCurrentDirectoryA(previous);
    DeleteFileA(path.c_str());
    RemoveDirectoryA(directory);
    std::printf("log %s: %d failures\n", sentinel ? "sentinel" : "absent", failures);
    return failures ? 1 : 0;
}
