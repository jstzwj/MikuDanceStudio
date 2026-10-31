#include "scene_fixture.hpp"
#include "mikudancestudio/menu_commands.h"

namespace {
// Original x64 sub_7FF7CB492F90: the 17 BM_SETCHECK calls, in execution order.
// The first call (model visibility) only runs when leaving model mode.
struct ButtonDefault {
    int id;
    WPARAM checked;
};
constexpr ButtonDefault kDefaults[] = {
    {panel::kModelVisibleCheckbox, BST_UNCHECKED},
    {panel::kShadowCheckbox, BST_UNCHECKED},
    {panel::kAddBlendCheckbox, BST_UNCHECKED},
    {panel::kAccessoryAddBlendCheckbox, BST_UNCHECKED},
    {panel::kBoneSelectRadio, BST_UNCHECKED},
    {panel::kBoxSelectRadio, BST_UNCHECKED},
    {panel::kBoneMoveRadio, BST_UNCHECKED},
    {panel::kBoneRotateRadio, BST_UNCHECKED},
    {panel::kInfoCheckbox, BST_UNCHECKED},
    {panel::kCoordAxisCheckbox, BST_CHECKED},
    {panel::kPerspectiveCheckbox, BST_CHECKED},
    {panel::kCameraRefModelCheckbox, BST_UNCHECKED},
    {panel::kCameraRefBoneCheckbox, BST_UNCHECKED},
    {panel::kPlaybackLoopCheckbox, BST_UNCHECKED},
    {panel::kPlaybackReturnCheckbox, BST_UNCHECKED},
    {panel::kAccessoryShadowCheckbox, BST_UNCHECKED},
    {panel::kAccessoryVisibleCheckbox, BST_UNCHECKED},
};

struct CommandObserver {
    MMDApp* app;
    bool floating;
    int clicks = 0;
};

LRESULT CALLBACK ObserveCommands(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                 DWORD_PTR data) {
    auto& observer = *reinterpret_cast<CommandObserver*>(data);
    if (message == WM_COMMAND && l != 0 && HIWORD(w) == BN_CLICKED) {
        for (const auto& button : kDefaults) {
            if (LOWORD(w) != button.id)
                continue;
            ++observer.clicks;
            if (button.id == panel::kInfoCheckbox || button.id == panel::kCoordAxisCheckbox) {
                if (observer.floating)
                    DispatchSeparateWindowCommand(observer.app, LOWORD(w));
                else
                    CommandDispatch(reinterpret_cast<HWND>(l), w);
            }
            return 0;
        }
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, ObserveCommands, id);
    return DefSubclassProc(window, message, w, l);
}

bool MenuChecked(HWND window, UINT command) {
    const UINT state = GetMenuState(GetMenu(window), command, MF_BYCOMMAND);
    Check(state != static_cast<UINT>(-1), "display menu item exists");
    return (state & MF_CHECKED) != 0;
}

void CheckDefaults(Fixture& fixture, HWND owner) {
    auto& app = *fixture.app;
    Check(app.FpsOverlayEnabled() == 0 && app.GroundGridEnabled() == 1,
          "new scene restores information off and coordinate axes on");
    Check(!MenuChecked(fixture.window, MDS_CMD_TOGGLE_INFORMATION) &&
              MenuChecked(fixture.window, MDS_CMD_TOGGLE_COORDINATE_AXIS),
          "display menu agrees with reset state");
    const UINT submenuState =
        GetMenuState(GetSubMenu(GetMenu(fixture.window), 7), 2, MF_BYPOSITION);
    Check(submenuState != static_cast<UINT>(-1) &&
              (submenuState & (MF_DISABLED | MF_GRAYED)) == (MF_DISABLED | MF_GRAYED),
          "new scene disables submenu 7 item 2 as in the original");
    for (const auto& button : kDefaults) {
        const bool separate =
            button.id == panel::kInfoCheckbox || button.id == panel::kCoordAxisCheckbox;
        HWND control = GetDlgItem(separate ? owner : fixture.window, button.id);
        Check(control != nullptr, "reset preserves button ownership");
        if (SendMessageA(control, BM_GETCHECK, 0, 0) != button.checked) {
            std::fprintf(stderr, "wrong reset check state: control %d\n", button.id);
            Check(false, "all reset buttons match the original MMD defaults");
        }
    }
    Check(app.CameraMode() == 1 && app.EditMode() == ViewportEditMode::None &&
              app.CameraPerspective() == 0 &&
              app.CameraReferenceMode() == CameraAttachmentReference::None &&
              app.PlaybackLoopEnabled() == 0 && app.state.playbackReturnsToStartFrame == 0,
          "reset does not run edit, perspective or playback toggle handlers");
    Check(app.SceneModified() == 0, "new scene remains unmodified");
}

void ExerciseReset(bool floating) {
    Fixture fixture(true);
    Check(fixture.ready, "construct scene reset fixture");
    auto& app = *fixture.app;
    HWND owner = fixture.window;
    if (floating) {
        owner = CreateWindowA("STATIC", "Floating controls", WS_OVERLAPPED, 0, 0, 200, 100, nullptr,
                              nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(owner != nullptr, "create floating control owner");
        app.FloatingWindow() = owner;
    }
    for (const auto& button : kDefaults) {
        DestroyWindow(GetDlgItem(fixture.window, button.id));
        const bool separate =
            button.id == panel::kInfoCheckbox || button.id == panel::kCoordAxisCheckbox;
        Check(CreateWindowA("BUTTON", "", WS_CHILD | BS_AUTOCHECKBOX | BS_PUSHLIKE, 0, 0, 80, 20,
                            separate ? owner : fixture.window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(button.id)),
                            GetModuleHandleW(nullptr), nullptr) != nullptr,
              "create real reset checkbox");
    }
    HMENU menu = CreateMenu();
    Check(menu && AppendMenuA(menu, MF_STRING, MDS_CMD_TOGGLE_INFORMATION, "information") &&
              AppendMenuA(menu, MF_STRING, MDS_CMD_TOGGLE_COORDINATE_AXIS, "axes") &&
              SetMenu(fixture.window, menu),
          "attach display menu");
    for (int index = 2; index < 8; ++index) {
        HMENU submenu = CreatePopupMenu();
        Check(submenu &&
                  AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), "submenu"),
              "create reset submenu");
        for (int item = 0; item < 3; ++item)
            Check(AppendMenuA(submenu, MF_STRING, 10000 + index * 3 + item, "item") != FALSE,
                  "create reset submenu item");
    }
    CommandObserver mainCommands{&app, false}, floatingCommands{&app, true};
    Check(SetWindowSubclass(fixture.window, ObserveCommands, 1,
                            reinterpret_cast<DWORD_PTR>(&mainCommands)) != FALSE,
          "observe main-window notifications");
    if (floating)
        Check(SetWindowSubclass(owner, ObserveCommands, 1,
                                reinterpret_cast<DWORD_PTR>(&floatingCommands)) != FALSE,
              "observe floating-window notifications");

    for (unsigned char information : {0, 1}) {
        for (unsigned char axes : {0, 1}) {
            fixture.LoadModel(0, 1);
            app.CameraMode() = 0;
            app.SetSelectedModelSlot(0);
            for (const auto& button : kDefaults) {
                const bool separate =
                    button.id == panel::kInfoCheckbox || button.id == panel::kCoordAxisCheckbox;
                SendDlgItemMessageA(separate ? owner : fixture.window, button.id, BM_SETCHECK,
                                    !button.checked, 0);
            }
            app.FpsOverlayEnabled() = information;
            app.GroundGridEnabled() = axes;
            CheckMenuItem(menu, MDS_CMD_TOGGLE_INFORMATION,
                          information ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(menu, MDS_CMD_TOGGLE_COORDINATE_AXIS, axes ? MF_CHECKED : MF_UNCHECKED);
            SendDlgItemMessageA(owner, panel::kInfoCheckbox, BM_SETCHECK, information, 0);
            SendDlgItemMessageA(owner, panel::kCoordAxisCheckbox, BM_SETCHECK, axes, 0);
            app.SceneModified() = 1;
            EnableMenuItem(GetSubMenu(menu, 7), 2, MF_BYPOSITION | MF_ENABLED);
            mainCommands.clicks = floatingCommands.clicks = 0;

            ResetAppState(&app);
            Check(mainCommands.clicks == 0 && floatingCommands.clicks == 0,
                  "reset must not synthesize user clicks");
            CheckDefaults(fixture, owner);
            // A second New must preserve the same defaults without any interaction.
            ResetAppState(&app);
            Check(mainCommands.clicks == 0 && floatingCommands.clicks == 0,
                  "repeated reset must not synthesize user clicks");
            CheckDefaults(fixture, owner);

            // The notification observer also exercises the real command handlers:
            // an actual user click must still toggle state, menu and button together.
            SendDlgItemMessageA(owner, panel::kInfoCheckbox, BM_CLICK, 0, 0);
            SendDlgItemMessageA(owner, panel::kCoordAxisCheckbox, BM_CLICK, 0, 0);
            Check(mainCommands.clicks + floatingCommands.clicks == 2,
                  "real button clicks reach production handlers");
            Check(app.FpsOverlayEnabled() == 1 && app.GroundGridEnabled() == 0 &&
                      MenuChecked(fixture.window, MDS_CMD_TOGGLE_INFORMATION) &&
                      !MenuChecked(fixture.window, MDS_CMD_TOGGLE_COORDINATE_AXIS) &&
                      SendDlgItemMessageA(owner, panel::kInfoCheckbox, BM_GETCHECK, 0, 0) ==
                          BST_CHECKED &&
                      SendDlgItemMessageA(owner, panel::kCoordAxisCheckbox, BM_GETCHECK, 0, 0) ==
                          BST_UNCHECKED,
                  "user clicks synchronize display state, menu and controls");
        }
    }
    RemoveWindowSubclass(fixture.window, ObserveCommands, 1);
    if (floating) {
        app.FloatingWindow() = nullptr;
        DestroyWindow(owner);
    }
}
} // namespace

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const HHOOK hook =
        SetWindowsHookExA(WH_CBT, CloseUnexpectedDialog, nullptr, GetCurrentThreadId());
    try {
        ExerciseReset(false);
        ExerciseReset(true);
        Check(dialogs == 0, "no unexpected dialogs during scene reset");
        UnhookWindowsHookEx(hook);
        std::puts("scene reset matches original MMD button defaults");
        return 0;
    } catch (const std::exception& error) {
        UnhookWindowsHookEx(hook);
        std::fprintf(stderr, "scene reset: %s\n", error.what());
        return 1;
    }
}
