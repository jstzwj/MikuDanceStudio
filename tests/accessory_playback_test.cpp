#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <type_traits>

using namespace mikudancestudio;

// Storage and walks must cover the same slot domain on both architectures.
static_assert(std::extent_v<decltype(AppRuntimeState::accessoryTrackActive)> ==
              mdl::kAccessorySlotCount);
static_assert(std::extent_v<decltype(MMDAppState::accessoryTrackCursor)> ==
              mdl::kAccessorySlotCount);
static_assert(std::extent_v<decltype(MMDAppState::accKeyTracks)> == mdl::kAccessorySlotCount);
static_assert(std::extent_v<decltype(MMDAppState::objectSlots)> == mdl::kAccessorySlotCount);

namespace {
int failures = 0;

void Check(bool condition, const char* message, int slot) {
    if (!condition) {
        std::fprintf(stderr, "slot %d: %s\n", slot, message);
        ++failures;
    }
}

struct Window {
    HWND handle = CreateWindowW(L"STATIC", L"Accessory playback regression", WS_OVERLAPPED, 0, 0,
                                64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Window() {
        if (handle)
            DestroyWindow(handle);
    }
};

void TestPlayback(MMDApp& app, bool dense) {
    auto accessories =
        std::make_unique<std::array<mdl::AccessoryRecord, mdl::kAccessorySlotCount>>();
    auto tracks =
        std::make_unique<std::array<std::array<mdl::AccessoryKey, 2>, mdl::kAccessorySlotCount>>();
    for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot) {
        // Exercise the old bound on either side, the final slot, and holes.
        if (!dense && slot != 0 && slot != 54 && slot != 55 && slot != mdl::kAccessorySlotCount - 1)
            continue;
        auto& keys = (*tracks)[slot];
        keys[0].next = 1;
        keys[1].frame = 30;
        for (int key = 0; key < 2; ++key) {
            keys[key].visible = 1;
            keys[key].parentModel = -1;
            keys[key].position[0] = static_cast<float>(slot + key * 1024);
            keys[key].scale = 1.0f + key * 2.0f;
            keys[key].opacity = 0.25f + key * 0.5f;
        }
        app.AccessorySlot(slot) = &(*accessories)[slot];
        app.AccessoryKeys(slot) = keys.data();
    }

    for (int repeat = 0; repeat < 2; ++repeat) {
        app.PlaybackCursorSeconds() = 0.0f;
        UpdateBoneFrames(&app);
        for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot) {
            const bool loaded = app.AccessorySlot(slot) != nullptr;
            Check(app.AccessoryTrackActive(slot) == static_cast<std::uint8_t>(loaded),
                  "initialize active flag", slot);
            if (loaded)
                Check(app.AccessoryTrackCursor(slot) == 0, "restart cursor at first key", slot);
        }

        app.PlaybackCursorSeconds() = 0.5f;
        PlaybackPoseAdvance(&app, 0);
        for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot) {
            if (const auto* accessory = app.AccessorySlot(slot)) {
                Check(app.AccessoryTrackActive(slot) == 1 && app.AccessoryTrackCursor(slot) == 1,
                      "advance to the next key", slot);
                Check(accessory->scale == 2.0f && accessory->opacity == 0.5f &&
                          accessory->position[0] == static_cast<float>(slot),
                      "interpolate floats and retain previous discrete state", slot);
            }
        }

        for (float seconds : {1.0f, 2.0f}) {
            app.PlaybackCursorSeconds() = seconds;
            PlaybackPoseAdvance(&app, 0);
            for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot) {
                if (const auto* accessory = app.AccessorySlot(slot)) {
                    Check(app.AccessoryTrackActive(slot) == (seconds == 1.0f) &&
                              app.AccessoryTrackCursor(slot) == 1,
                          "exact hit stays active; passing the terminal key retires the track",
                          slot);
                    Check(accessory->scale == 3.0f && accessory->opacity == 0.75f &&
                              accessory->position[0] == static_cast<float>(slot + 1024),
                          "apply the terminal payload", slot);
                }
            }
        }

        StopPlayback(&app);
        for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot) {
            if (const auto* accessory = app.AccessorySlot(slot))
                Check(accessory->scale == 1.0f && accessory->opacity == 0.25f &&
                          accessory->position[0] == static_cast<float>(slot),
                      "stop restores the editing frame for every accessory", slot);
        }

        // Starting after the last key uses a different initialization branch.
        UpdateBoneFrames(&app);
        for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot) {
            Check(app.AccessoryTrackActive(slot) == 0, "initialize after the terminal key", slot);
            if (const auto* accessory = app.AccessorySlot(slot))
                Check(app.AccessoryTrackCursor(slot) == 1 && accessory->scale == 3.0f,
                      "initialization applies the terminal payload", slot);
        }
    }

    for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot) {
        app.AccessorySlot(slot) = nullptr;
        app.AccessoryKeys(slot) = nullptr;
    }
}

#if defined(_M_X64)
void TestFloatRounding(MMDApp& app) {
    // Bit pattern recorded from original x64 RVA 0x69A20. Computing the
    // frame rounding or interpolation fraction in double changes this bit.
    mdl::AccessoryRecord accessory{};
    mdl::AccessoryKey keys[3]{};
    keys[0].next = 1;
    keys[1].frame = 7;
    keys[1].next = 2;
    keys[2].frame = 31;
    keys[2].previous = 1;
    keys[0].scale = 0.1f;
    keys[1].scale = 0.2f;
    keys[2].scale = 1.1f;
    const int slot = mdl::kAccessorySlotCount - 1;
    app.AccessorySlot(slot) = &accessory;
    app.AccessoryKeys(slot) = keys;
    app.PlaybackCursorSeconds() = 0.0f;
    UpdateBoneFrames(&app);
    app.PlaybackCursorSeconds() = 0.277925014f;
    PlaybackPoseAdvance(&app, 0);
    std::uint32_t bits;
    std::memcpy(&bits, &accessory.scale, sizeof bits);
    Check(bits == 0x3E8016F1, "single-precision stages match the original x64 result", slot);
    app.AccessorySlot(slot) = nullptr;
    app.AccessoryKeys(slot) = nullptr;
}
#endif
} // namespace

int main() {
    Window window;
    if (!window.handle)
        return 1;
    auto app = std::make_unique<MMDApp>();
    app->state = {};
    app->Hwnd() = window.handle;

    // A minimal model supplies the bone-mode UI state used by StopPlayback.
    // No renderer, graphics device, asset files or physics world are needed.
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::DisplayKey display{};
    model->displayKeys = &display;
    model->selectedBone = -1;
    model->hwnd = window.handle;
    app->ModelSlot(0) = model.get();

    std::memset(app->state.reservedAccessoryTrackActive, 0xA5,
                sizeof app->state.reservedAccessoryTrackActive);
    std::memset(app->state.playbackEnabledSnapshot, 1, sizeof app->state.playbackEnabledSnapshot);
    for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot)
        app->AccessoryTrackActive(slot) = 1;
    UpdateBoneFrames(app.get());
    PlaybackPoseAdvance(app.get(), 0);
    StopPlayback(app.get());
    for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot)
        Check(app->AccessoryTrackActive(slot) == 0, "empty slots are inactive", slot);

    TestPlayback(*app, false);
    TestPlayback(*app, true);
#if defined(_M_X64)
    TestFloatRounding(*app);
#endif
    for (auto byte : app->state.reservedAccessoryTrackActive)
        Check(byte == 0xA5, "cursor writes preserve the following compatibility storage", -1);
    for (auto byte : app->state.playbackEnabledSnapshot)
        Check(byte == 1, "playback preserves the UI snapshot", -1);
    if (!failures)
        std::puts("Accessory playback: empty, sparse and all 255 slots pass repeated start/stop");
    return failures ? 1 : 0;
}
