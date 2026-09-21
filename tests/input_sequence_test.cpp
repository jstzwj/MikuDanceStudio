#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include "../src/app/pump_input.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace mikudancestudio;

namespace {
struct Command {
    int id;
    ViewportEditMode mode;
    bool playing;
    LRESULT playChecked;
    int frame;
    int undoCursor;
};
MMDApp* editor;
std::vector<Command> commands;
HWND edit;
bool changeFocusOnCommand;

LRESULT CALLBACK CommandSink(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_COMMAND && HIWORD(w) == 0 && editor) {
        const int id = LOWORD(w);
        commands.push_back({id, editor->EditMode(), editor->PlaybackActive() != 0,
            SendMessageW(GetDlgItem(window, 0x198), BM_GETCHECK, 0, 0),
            editor->CurrentFrame(), editor->SelectedModel()
                ? static_cast<int>(mdl::Mdl(editor->SelectedModel())->undoState[0]) : -1});
        if (id == 0x198) {
            editor->PlaybackActive() = !editor->PlaybackActive();
        }
        if (changeFocusOnCommand)
            SetFocus(edit);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

bool expect(bool value, const char* description) {
    if (!value)
        std::fprintf(stderr, "%s\n", description);
    return value;
}
}

int main() {
    WNDCLASSW cls{};
    cls.lpfnWndProc = CommandSink;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"MDSInputSequenceTest";
    if (!RegisterClassW(&cls)) return 1;
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"",
        WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, cls.hInstance, nullptr);
    if (!window) return 1;
    auto app = std::make_unique<MMDApp>();
    editor = app.get();
    const auto reset = [&] {
        std::memset(&app->state, 0, sizeof(app->state));
        app->MainWindow() = window;
        commands.clear();
        changeFocusOnCommand = false;
    };
    const auto child = [&](int id, const wchar_t* type, DWORD style) {
        return CreateWindowExW(0, type, L"", WS_CHILD | style, 0, 0, 1, 1,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), cls.hInstance, nullptr);
    };
    if (!child(0x198, L"BUTTON", BS_CHECKBOX)) return 1;
    edit = child(0x1DE, L"EDIT", 0);
    if (!edit) return 1;
    // Inject a captured input context without activating or synthesizing input
    // in the user's desktop. Dispatch still executes real product code and
    // synchronous Win32 messages; only the window command handler is a sink.
    const KeyboardInputContext input{window, true};
    bool ok = true;

    reset();
    app->state.dialogFlags[0] = 1; // X changes edit mode before Enter registers.
    app->state.enterKeyState = 1;
    ConsumeKeyboardInput(app.get(), input);
    ok &= expect(commands.size() == 1 && commands[0].id == 0x1F4 &&
        commands[0].mode == ViewportEditMode::Camera,
        "X + Enter registered before changing the edit mode");

    reset();
    auto model = std::make_unique<mdl::ModelRecord>();
    auto renderer = std::make_unique<D3DRenderer>();
    app->Renderer() = renderer.get();
    app->ModelSlot(0) = reinterpret_cast<unsigned char*>(model.get());
    model->hwnd = window;
    model->undoDirty = 1;
    model->undoRings[0].slots[0].operation = 2;
    model->undoRings[0].slots[0].frame = 47;
    app->CurrentFrame() = 99;
    app->state.ctrlModifierState = 3;
    app->state.dialogFlags[1] = 1;
    app->state.enterKeyState = 1;
    ConsumeKeyboardInput(app.get(), input);
    ok &= expect(commands.size() == 1 && commands[0].id == 0x1F4 &&
        commands[0].frame == 47 && commands[0].undoCursor == 29 &&
        model->redoDirty == 1 && model->undoDirty == 0,
        "Ctrl+Z + Enter did not register after restoring the real undo ring/frame");
    ::operator delete(model->undoRings[1].slots[0].bonePose);
    ::operator delete(model->undoRings[1].slots[0].auxiliaryPose);

    reset();
    app->state.dialogFlags[12] = 1; // P starts playback; Enter must see it.
    app->state.enterKeyState = 1;
    ConsumeKeyboardInput(app.get(), input);
    ok &= expect(commands.size() == 1 && commands[0].id == 0x198 &&
        commands[0].playChecked == BST_CHECKED && app->PlaybackActive(),
        "P + Enter used stale playback state or registered before starting playback");

    reset();
    app->PlaybackActive() = 1;
    app->state.dialogFlags[12] = 1;
    app->state.enterKeyState = 1;
    ConsumeKeyboardInput(app.get(), input);
    ok &= expect(commands.size() == 2 && commands[0].id == 0x198 &&
        commands[0].playChecked == BST_UNCHECKED && commands[1].id == 0x1F4,
        "P failed to stop playback before the following Enter");

    // Every control omitted by the old 12-item focus approximation suppresses
    // F and model Enter, even with a stale viewport-active byte.
    for (int id : {0x199, 0x19A, 0x1A1, 0x1A8, 0x1A9, 0x1AA, 0x1CD,
                   0x1D2, 0x1DE, 0x1E5, 0x1FA, 0x1FF, 0x204, 0x209, 0x231}) {
        HWND control = GetDlgItem(window, id);
        if (!control) control = child(id, L"EDIT", 0);
        if (!control) return 1;
        reset();
        app->ViewportInputActive() = 1;
        app->state.dialogFlags[15] = 1;
        app->state.enterKeyState = 1;
        KeyboardInputContext in{control, !PumpPanelFocusChain(app.get(), control)};
        ConsumeKeyboardInput(app.get(), in);
        ok &= expect(!in.focusNotInPanelEdit && commands.empty(),
                     "A panel edit failed to suppress frame commands");
    }

    reset();
    app->state.dialogFlags[3] = 1;
    app->state.ctrlModifierState = 3;
    app->state.dialogFlags[15] = 1;
    changeFocusOnCommand = true;
    ConsumeKeyboardInput(app.get(), input);
    ok &= expect(commands.size() == 2 && commands[0].id == 0x1A5 &&
        commands[1].id == 0xFA,
        "F re-read focus after Ctrl+V instead of retaining the pump snapshot");

    reset();
    app->state.dialogFlags[4] = app->state.dialogFlags[5] = app->state.dialogFlags[8] = 1;
    app->state.optflag[0] = 1;
    ConsumeKeyboardInput(app.get(), input);
    ok &= expect(commands.empty(), "A/S/D escaped the shared model-edit gate");

    editor = nullptr;
    DestroyWindow(window);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
    return ok ? 0 : 1;
}
