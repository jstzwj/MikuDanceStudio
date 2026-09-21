// Exercise the production error formatter and phase gate; replace only the
// modal Win32 boundary so the regression can run unattended.
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

struct Message {
    HWND owner;
    std::string text;
    std::string title;
    UINT flags;
};
static std::vector<Message> messages;
static bool englishUiMode = true;
static int WINAPI CaptureMessageBox(HWND owner, LPCSTR text, LPCSTR title, UINT flags)
{
    messages.push_back({owner, text, title, flags});
    return IDOK;
}
#define MessageBoxA CaptureMessageBox
// Keep language and DXErr lookup deterministic without changing production APIs.
#define MmeIsEnglishUiMode TestEnglishUiMode
#define MmeDxErrDescription TestDxErrDescription
#define g_mainWindow testMainWindow
#include "../third_party/mmeffect/src/mmeffect/pass_planner.cpp"
#include "../third_party/mmeffect/src/mmeffect/mme_log.cpp"
#undef MessageBoxA

namespace mme {
HWND g_mainWindow = reinterpret_cast<HWND>(static_cast<UINT_PTR>(123));
bool MmeIsEnglishUiMode() { return englishUiMode; }
const char* MmeDxErrDescription(unsigned long) { return "test failure"; }
}

static int failures = 0;
static void Check(bool condition, const char* label)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++failures;
    }
}

int main()
{
    using namespace mme;
    const unsigned long first = 0x8876086C;
    const unsigned long second = 0x80004005;
    MmeReportPostEffectFailure(first);
    MmeReportPostEffectFailure(first);
    Check(messages.size() == 2, "outside phase: every failure displays");
    MmeLogFlush(1);
    MmeReportPostEffectFailure(first);
    MmeReportPostEffectFailure(first);
    Check(messages.size() == 3, "active phase: repeated failure displays once");
    MmeReportPostEffectFailure(second);
    Check(messages.size() == 4, "active phase: distinct HRESULT displays");
    MmeLogFlush(1);
    MmeReportPostEffectFailure(first);
    Check(messages.size() == 4, "same phase flush retains suppression");
    MmeLogFlush(0);
    MmeReportPostEffectFailure(first);
    MmeReportPostEffectFailure(first);
    Check(messages.size() == 6, "leaving phase restores unconditional display");
    MmeLogFlush(1);
    MmeReportPostEffectFailure(first);
    MmeReportPostEffectFailure(first);
    Check(messages.size() == 7, "new active phase allows one display again");
    Check(!MmeLogShouldShowMessageBox(messages.front().text.c_str()),
          "post-effect failures share the existing message gate");
    for (const Message& message : messages) {
        Check(message.owner == g_mainWindow && message.title == "MikuMikuEffect" &&
              message.flags == MB_ICONERROR, "modal metadata preserved");
    }
    Check(messages.front().text ==
          "Failed to process post effect:\nDirectX Error: test failure [8876086C]\n",
          "complete localized message is the dedup key");
    const std::string history = MmeGetLogDialogText();
    size_t count = 0;
    for (size_t pos = 0; (pos = history.find("DirectX Error:", pos)) != std::string::npos; ++pos)
        ++count;
    Check(count == 10, "suppressed dialogs still append every error to history");

    // Byte fixture read directly from the original MMEffect.dll RVA 0xB5958.
    // Keep the ANSI payload independent of the test machine's code page.
    const unsigned char japanesePrefix[] = {
        0x83, 0x7c, 0x83, 0x58, 0x83, 0x67, 0x83, 0x47,
        0x83, 0x74, 0x83, 0x46, 0x83, 0x4e, 0x83, 0x67,
        0x8f, 0x88, 0x97, 0x9d, 0x92, 0x86, 0x82, 0xc9,
        0x83, 0x47, 0x83, 0x89, 0x81, 0x5b, 0x82, 0xaa,
        0x94, 0xad, 0x90, 0xb6, 0x82, 0xb5, 0x82, 0xdc,
        0x82, 0xb5, 0x82, 0xbd, 0x3a, 0x0a
    };
    std::string expectedJapanese(reinterpret_cast<const char*>(japanesePrefix),
                                 sizeof(japanesePrefix));
    expectedJapanese += "DirectX Error: test failure [8876086C]\n";
    englishUiMode = false;
    MmeReportPostEffectFailure(first);
    MmeReportPostEffectFailure(first);
    Check(messages.size() == 8, "language switch gives a distinct complete-message key");
    Check(messages.back().text == expectedJapanese,
          "non-English message preserves original Shift-JIS bytes and newline");
    englishUiMode = true;
    MmeReportPostEffectFailure(first);
    Check(messages.size() == 8, "switching back retains English phase suppression");
    MmeLogFlush(0);
    englishUiMode = false;
    MmeReportPostEffectFailure(first);
    Check(messages.size() == 9 && messages.back().text == expectedJapanese,
          "non-English message remains exact outside a phase");
    return failures == 0 ? 0 : 1;
}
