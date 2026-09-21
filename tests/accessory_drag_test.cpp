#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

using namespace mikudancestudio;

int main() {
    auto app = std::make_unique<MMDApp>();
    std::memset(&app->state, 0, sizeof(app->state));
    app->state.coordinateSystem = 2;
    app->state.optflag[0] = 1;
    app->state.cameraParentModel = -1;
    app->state.selectedObjectSlot = 1;

    mdl::AccessoryRecord initial{};
    for (wchar_t& character : initial.directory) character = L'Q';
    initial.visible = 1;
    initial.shadowEnabled = 1;
    initial.scale = 1.5f;
    initial.opacity = 0.75f;
    initial.parentModel = -1;
    initial.parentBone = 7;
    for (int axis = 0; axis < 3; ++axis) {
        initial.position[axis] = 10.0f * (axis + 1);
        initial.rotation[axis] = static_cast<float>(axis + 1);
    }
    mdl::AccessoryRecord accessory = initial;
    mdl::AccessoryRecord unselected = initial;
    app->AccessorySlot(0) = &unselected;
    app->AccessorySlot(1) = &accessory;
    int failures = 0;
    for (int dy : {-25, 0, 25}) {
        app->PreviousMouseY() = 100;
        app->MouseY() = 100 + dy;
        for (int axis = 0; axis < 3; ++axis) {
            for (bool rotate : {false, true}) {
                accessory = initial;
                mdl::AccessoryRecord expected = initial;
                // These fixtures have exact final results under both x86 and
                // x64 arithmetic, isolating field selection from rounding.
                if (rotate) expected.rotation[axis] += dy / 50.0f;
                else expected.position[axis] -= dy / 20.0f;
                if (rotate) ModeAngleAdjust(app.get(), axis);
                else ModeCameraAdjust(app.get(), axis);
                if (std::memcmp(&accessory, &expected, sizeof(expected)) != 0 ||
                    std::memcmp(&unselected, &initial, sizeof(initial)) != 0) {
                    std::fprintf(stderr, "%s axis %d dy %d changed wrong data\n",
                                 rotate ? "rotation" : "position", axis, dy);
                    ++failures;
                }
            }
        }
    }
    accessory = initial;
    for (int axis : {-1, 3}) {
        ModeCameraAdjust(app.get(), axis);
        ModeAngleAdjust(app.get(), axis);
    }
    if (std::memcmp(&accessory, &initial, sizeof(initial)) != 0) ++failures;
    app->AccessorySlot(1) = nullptr;
    for (int axis = 0; axis < 3; ++axis) {
        ModeCameraAdjust(app.get(), axis);
        ModeAngleAdjust(app.get(), axis);
    }
    std::printf("accessory drag: %d failures\n", failures);
    return failures ? 1 : 0;
}
