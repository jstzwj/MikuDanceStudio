#include "mikudancestudio/mmd_app.hpp"
#include <cstdio>
#include <cstring>
#include <memory>

namespace mikudancestudio { void ConsumeLetterHotkeys(MMDApp*); }

namespace {
int commands = 0;
LRESULT CALLBACK CommandSink(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_COMMAND) {
        ++commands;
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
}

int main() {
    WNDCLASSW cls{};
    cls.lpfnWndProc = CommandSink;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"MDSBackgroundHotkeysTest";
    if (!RegisterClassW(&cls)) return 1;
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"",
        WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, cls.hInstance, nullptr);
    if (!window) return 1;
    auto app = std::make_unique<mikudancestudio::MMDApp>();
    std::memset(&app->state, 0, sizeof(app->state));
    app->MainWindow() = window;
    // Exercise actual dispatch, with stale viewport activation and a queued
    // press. A hidden editor cannot own the foreground, even if its previous
    // input snapshot still says it is active. No keyboard input is synthesized.
    for (int minimized = 0; minimized != 2; ++minimized) {
        if (minimized) ShowWindow(window, SW_SHOWMINNOACTIVE);
        if (GetForegroundWindow() == window || (minimized && !IsIconic(window)))
            return 1;
        for (int slot : {4, 5, 8}) { // D: center dialog; A/S: bone selection
            std::memset(app->state.dialogFlags, 0, sizeof(app->state.dialogFlags));
            app->ViewportInputActive() = 1;
            app->state.dialogFlags[slot] = 1;
            mikudancestudio::ConsumeLetterHotkeys(app.get());
        }
    }
    DestroyWindow(window);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
    if (commands) {
        std::fprintf(stderr, "Inactive/minimized editor dispatched %d commands\n", commands);
        return 1;
    }
    return 0;
}
