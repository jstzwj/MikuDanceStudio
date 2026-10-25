#include "mikudancestudio/keyboard_state.hpp"
#include "mikudancestudio/menu_commands.h"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include "btBulletDynamicsCommon.h"

#include <cstdio>
#include <memory>

namespace mikudancestudio {
void CmdViewMenu(MMDApp*, HWND, std::uint16_t, std::uint16_t);
}

int main() {
    using namespace mikudancestudio;
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "%s\n", message);
            ++failures;
        }
    };
    HWND window = CreateWindowA("STATIC", "", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr,
                                GetModuleHandleA(nullptr), nullptr);
    HMENU menu = CreateMenu();
    if (!window || !menu)
        return 1;
    AppendMenuA(menu, MF_STRING | MF_CHECKED, MDS_CMD_TOGGLE_PHYSICS_FLOOR, "floor");
    SetMenu(window, menu);
    auto app = std::make_unique<MMDApp>();
    app->state = {};
    app->state.hwnd = window;
    app->state.floorVisible = 1;
    btStaticPlaneShape shape(btVector3(0, 1, 0), 0);
    btRigidBody::btRigidBodyConstructionInfo info(0, nullptr, &shape);
    btRigidBody body(info);
    body.forceActivationState(DISABLE_DEACTIVATION);
    body.setFriction(0.25f);
    body.setRestitution(0.88f);
    body.setDeactivationTime(2.5f);
    PhysicsScene scene{};
    scene.groundBody = &body;
    app->Physics() = &scene;
    // Original x64 0x7FF7CB4700BC/0x7FF7CB4700FB and x86
    // 0x48B779/0x48B7A9 replace the collision flags with exactly 1/-1.
    for (int i = 0; i < 4; ++i) {
        app->LetterKeyState(LetterKey::F) = 0;
        CmdViewMenu(app.get(), window, MDS_CMD_TOGGLE_PHYSICS_FLOOR, 0);
        const bool enabled = (i % 2) != 0;
        check(app->state.floorVisible == static_cast<unsigned char>(enabled),
              "Floor state toggles");
        check(body.getCollisionFlags() == (enabled ? 1 : -1),
              "Floor command replaces collision flags");
        check((GetMenuState(menu, MDS_CMD_TOGGLE_PHYSICS_FLOOR, MF_BYCOMMAND) & MF_CHECKED) ==
                  (enabled ? MF_CHECKED : 0),
              "Floor menu follows its state");
        check(app->LetterKeyState(LetterKey::F) == 1, "Floor command latches the F key");
        check(body.getActivationState() == DISABLE_DEACTIVATION &&
                  body.getDeactivationTime() == 2.5f && body.getFriction() == 0.25f &&
                  body.getRestitution() == 0.88f,
              "Floor command leaves unrelated Bullet fields unchanged");
    }
    app->Physics() = nullptr;
    SetMenu(window, nullptr);
    DestroyMenu(menu);
    DestroyWindow(window);
    return failures == 0 ? 0 : 1;
}
