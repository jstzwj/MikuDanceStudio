#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "pmm_playback_options.hpp"

#include <cstdio>
#include <cstring>
#include <io.h>
#include <memory>

namespace mikudancestudio {
MMDApp::MMDApp() : state{} {}
}

namespace {
using namespace mikudancestudio;
struct Observation {
    MMDApp* app;
    WNDPROC original;
    unsigned char expected = 0;
    int messages = 0;
    bool consistent = true;
};

LRESULT CALLBACK ObserveCheckbox(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* observation = reinterpret_cast<Observation*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == BM_SETCHECK) {
        ++observation->messages;
        observation->consistent &=
            observation->app->state.cameraReferenceMode == observation->expected;
    }
    return CallWindowProcW(observation->original, window, message, w, l);
}
}  // namespace

int main() {
    using namespace mikudancestudio;
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "%s\n", message);
            ++failures;
        }
    };
    auto app = std::make_unique<MMDApp>();
    HWND main = CreateWindowExW(0, L"STATIC", L"PMM test", WS_OVERLAPPED,
                                0, 0, 200, 200, nullptr, nullptr, nullptr, nullptr);
    if (!main) return 1;
    const int checkboxIds[] = {
        panel::kCameraRefModelCheckbox,
        panel::kCameraRefBoneCheckbox,
        panel::kPlaybackLoopCheckbox,
        panel::kPlaybackReturnCheckbox,
        panel::kPlaybackFromCurrentCheckbox
    };
    for (int id : checkboxIds) {
        check(CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_AUTOCHECKBOX,
                              0, 0, 20, 20, main,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                              nullptr, nullptr) != nullptr, "create hidden checkbox");
    }
    for (int id : {panel::kPlayStartFrameEdit, panel::kPlayStopFrameEdit}) {
        check(CreateWindowExW(0, L"EDIT", L"", WS_CHILD,
                              0, 0, 20, 20, main,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                              nullptr, nullptr) != nullptr, "create hidden frame edit");
    }
    if (failures != 0) {
        DestroyWindow(main);
        return 1;
    }
    Observation observations[2] = {{app.get()}, {app.get()}};
    for (int i = 0; i < 2; ++i) {
        HWND button = GetDlgItem(main, checkboxIds[i]);
        SetWindowLongPtrW(button, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(&observations[i]));
        observations[i].original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            button, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ObserveCheckbox)));
    }
    FILE* file = nullptr;
    if (tmpfile_s(&file) != 0 || !file) {
        DestroyWindow(main);
        return 1;
    }
    const int fd = _fileno(file);
    char text[256]{};
    const auto restore = [&](unsigned char expected, unsigned char& playbackByte) {
        for (auto& observation : observations) {
            observation.expected = expected;
            observation.messages = 0;
            observation.consistent = true;
        }
        RestorePmmPlaybackOptions(app.get(), main, fd, text, playbackByte);
        check(app->state.cameraReferenceMode == expected, "restore raw reference mode");
        for (const auto& observation : observations)
            check(observation.messages == 1 && observation.consistent,
                  "reference state is restored before either checkbox message");
        check(SendMessageW(GetDlgItem(main, checkboxIds[0]), BM_GETCHECK, 0, 0) ==
                  (expected == 1), "model reference checkbox matches state");
        check(SendMessageW(GetDlgItem(main, checkboxIds[1]), BM_GETCHECK, 0, 0) ==
                  (expected == 2), "bone reference checkbox matches state");
    };
    for (unsigned char mode : {0, 1, 2, 255}) {
        const unsigned char input[] = {mode, 1, 0, 1, 12, 0, 0, 0, 34, 0, 0, 0};
        _chsize(fd, 0);
        _lseek(fd, 0, SEEK_SET);
        check(_write(fd, input, sizeof(input)) == sizeof(input), "write configuration fixture");
        _lseek(fd, 0, SEEK_SET);
        app->state.cameraReferenceMode = mode == 0 ? 2 : 0;
        unsigned char playbackByte = 7;
        restore(mode, playbackByte);
        check(_tell(fd) == sizeof(input), "consume exactly the playback configuration");
        check(app->state.playbackLoopEnabled == 1 &&
                  app->state.playbackReturnsToStartFrame == 0 &&
                  app->state.playbackStartsAtCurrentFrame == 1 && playbackByte == 1,
              "subsequent options keep their stream positions");
        char value[16]{};
        GetWindowTextA(GetDlgItem(main, panel::kPlayStartFrameEdit), value, sizeof(value));
        check(std::strcmp(value, "12") == 0, "restore playback start frame");
        GetWindowTextA(GetDlgItem(main, panel::kPlayStopFrameEdit), value, sizeof(value));
        check(std::strcmp(value, "34") == 0, "restore playback stop frame");
    }

    // EOF must leave the member and the separate playback scratch byte intact.
    _chsize(fd, 0);
    _lseek(fd, 0, SEEK_SET);
    app->state.cameraReferenceMode = 2;
    unsigned char playbackByte = 0x7F;
    restore(2, playbackByte);
    check(playbackByte == 0x7F, "empty input preserves playback scratch byte");
    const unsigned char mode = 1;
    _write(fd, &mode, 1);
    _lseek(fd, 0, SEEK_SET);
    restore(1, playbackByte);
    check(playbackByte == 0x7F, "reference byte does not overwrite playback scratch");
    std::fclose(file);
    DestroyWindow(main);
    return failures == 0 ? 0 : 1;
}
