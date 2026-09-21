#include "mikudancestudio/mmd_app.hpp"
#include "../src/app/pump_input.hpp"
#include <cstdio>
#include <cstring>
#include <memory>


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
    // Original x64 uses (focus == main || viewportActive), then the panel
    // and model gates. It does not add an IsIconic/GetForegroundWindow guard.
    // Inject snapshots without moving focus or synthesizing physical keys.
    for (int minimized = 0; minimized != 2; ++minimized) {
        if (minimized) ShowWindow(window, SW_SHOWMINNOACTIVE);
        if (GetForegroundWindow() == window || (minimized && !IsIconic(window)))
            return 1;
        for (int slot : {4, 5, 8}) {
            std::memset(app->state.dialogFlags, 0, sizeof(app->state.dialogFlags));
            app->state.dialogFlags[slot] = 1;
            for (int focusMain = 0; focusMain != 2; ++focusMain) {
                for (int active = 0; active != 2; ++active) {
                    for (int panelEdit = 0; panelEdit != 2; ++panelEdit) {
                        app->ViewportInputActive() = active;
                        const mikudancestudio::KeyboardInputContext input{
                            focusMain ? window : nullptr, panelEdit == 0};
                        const int before = commands;
                        mikudancestudio::ConsumeKeyboardInput(app.get(), input);
                        const int expected = (focusMain || active) && !panelEdit;
                        if (commands - before != expected) {
                            std::fprintf(stderr, "Background key gate mismatch: slot=%d focus=%d active=%d edit=%d\n",
                                         slot, focusMain, active, panelEdit);
                            return 1;
                        }
                    }
                }
            }
        }
    }
    DestroyWindow(window);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
    return 0;
}
