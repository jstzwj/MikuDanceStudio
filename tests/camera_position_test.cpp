#include "mikudancestudio/mmd_app.hpp"
#include <cstdio>
#include <cstring>
#include <memory>

using namespace mikudancestudio;

int main() {
    auto app = std::make_unique<MMDApp>();
    std::memset(&app->state, 0, sizeof(app->state));
    int failures = 0;
    auto check = [&](bool condition, const char* label) {
        if (!condition) { ++failures; std::printf("FAIL: %s\n", label); }
    };
    const float keyEye[3] = {1.25f, -2.5f, 37.75f};
    // Playback and timeline evaluation copy a complete camera key eye vector;
    // the render/UI readers must observe the very same three components.
    app->CameraPositionZ() = -99.0f;
    app->state.cameraReferenceMode = 1;
    app->state.playbackLoopEnabled = 1;
    app->state.playbackReturnsToStartFrame = 1;
    app->state.viewportToolHovered = 17;
    std::memcpy(app->CameraPosition(), keyEye, sizeof(keyEye));
    check(app->CameraPositionX() == keyEye[0], "key replay X");
    check(app->CameraPositionY() == keyEye[1], "key replay Y");
    check(app->CameraPositionZ() == keyEye[2], "key replay Z");
    check(app->state.cameraReferenceMode == 1 &&
          app->state.playbackLoopEnabled == 1 &&
          app->state.playbackReturnsToStartFrame == 1 &&
          app->state.viewportToolHovered == 17, "key replay preserves neighboring flags");
    // UI edits and PMM component reads must reach frame registration's bulk copy.
    app->CameraPositionX() = -11.0f;
    app->CameraPositionY() = 23.0f;
    app->CameraPositionZ() = -47.0f;
    float registeredEye[3]{};
    std::memcpy(registeredEye, app->CameraPosition(), sizeof(registeredEye));
    check(registeredEye[0] == -11 && registeredEye[1] == 23 &&
          registeredEye[2] == -47, "component edits reach registered key");
    const auto* bytes = reinterpret_cast<const unsigned char*>(&app->state);
    const std::size_t positionOffset = sizeof(void*) == 8 ? 0x36C : 0x334;
    float binaryEye[3]{};
    std::memcpy(binaryEye, bytes + positionOffset, sizeof(binaryEye));
    check(std::memcmp(binaryEye, registeredEye, sizeof(binaryEye)) == 0,
          "binary XYZ field offsets");
    check(reinterpret_cast<const unsigned char*>(&app->CameraPositionZ()) - bytes ==
          positionOffset + 8, "Z occupies original binary field");
    std::printf("camera position: 7 checks, %d failures\n", failures);
    return failures ? 1 : 0;
}
