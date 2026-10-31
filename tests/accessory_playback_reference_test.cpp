// Optional x64 oracle: execute the verified original functions with synthetic
// accessory tracks. No original executable or third-party assets are distributed.
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

#include "original_physics_module.hpp"

namespace {
using namespace mikudancestudio;
constexpr int controlIds[] = {panel::kBonePasteButton,  panel::kBoneReversePasteButton,
                              panel::kPasteButton,      panel::kReversePasteButton,
                              panel::kCurvePasteButton, panel::kUndoButton,
                              panel::kRedoButton};

struct Fixture {
    std::unique_ptr<MMDApp> app = std::make_unique<MMDApp>();
    std::unique_ptr<std::array<mdl::AccessoryRecord, mdl::kAccessorySlotCount>> objects =
        std::make_unique<std::array<mdl::AccessoryRecord, mdl::kAccessorySlotCount>>();
    std::array<std::array<mdl::AccessoryKey, 3>, mdl::kAccessorySlotCount> tracks{};
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::unique_ptr<D3DRenderer> renderer = std::make_unique<D3DRenderer>();
    mdl::DisplayKey display{};
    HWND window = CreateWindowW(L"STATIC", L"Accessory reference", WS_OVERLAPPED, 0, 0, 64, 64,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    explicit Fixture(bool dense) {
        Check(window != nullptr, "create reference fixture window");
        app->state = {};
        app->Hwnd() = window;
        app->Renderer() = renderer.get();
        for (int i = 0; i < 7; ++i) {
            app->state.playbackEnabledSnapshot[i] = static_cast<unsigned char>(i % 2);
            Check(CreateWindowW(L"BUTTON", L"", WS_CHILD, 0, 0, 20, 20, window,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlIds[i])),
                                GetModuleHandleW(nullptr), nullptr) != nullptr,
                  "create playback control");
        }
        app->state.characterTransparentMode = 1;
        app->state.blinkPhase = 3;
        for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot) {
            app->AccessoryTrackCursor(slot) = 0xBAD;
            if (!dense && slot != 0 && slot != 54 && slot != 55 && slot != 254)
                continue;
            auto& keys = tracks[slot];
            for (int i = 0; i < 3; ++i) {
                auto& key = keys[i];
                key.frame = i == 0 ? 0 : (i == 1 ? 7 : 31);
                key.previous = i == 0 ? 0 : i - 1;
                key.next = i == 2 ? 0 : i + 1;
                key.visible = static_cast<std::uint8_t>(i != 1);
                key.shadowEnabled = static_cast<std::uint8_t>(i == 1);
                key.parentModel = -1;
                key.parentBone = i;
                for (int axis = 0; axis < 3; ++axis) {
                    key.position[axis] = static_cast<float>(slot + i * 100 + axis);
                    key.rotation[axis] = static_cast<float>(slot + i * 10 + axis) / 17.0f;
                }
                key.scale = i == 0 ? 0.1f : (i == 1 ? 0.2f : 1.1f);
                key.opacity = i == 0 ? 0.2f : (i == 1 ? 0.3f : 0.7f);
            }
            app->AccessorySlot(slot) = &(*objects)[slot];
            app->AccessoryKeys(slot) = keys.data();
        }
        model->displayKeys = &display;
        model->selectedBone = -1;
        model->hwnd = window;
    }
    ~Fixture() { DestroyWindow(window); }
};

int mismatches = 0;
int observations = 0;
void Compare(Fixture& port, Fixture& original, const char* stage, float seconds) {
    ++observations;
    for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot) {
        const bool equal =
            port.app->AccessoryTrackActive(slot) ==
                original.app->state.reservedAccessoryTrackActive[slot] &&
            port.app->AccessoryTrackCursor(slot) == original.app->AccessoryTrackCursor(slot) &&
            std::memcmp(&(*port.objects)[slot], &(*original.objects)[slot],
                        sizeof(mdl::AccessoryRecord)) == 0;
        if (!equal && ++mismatches <= 10)
            std::fprintf(stderr,
                         "%s %.9g slot %d: port scale %.9g opacity %.9g; "
                         "original scale %.9g opacity %.9g\n",
                         stage, seconds, slot, (*port.objects)[slot].scale,
                         (*port.objects)[slot].opacity, (*original.objects)[slot].scale,
                         (*original.objects)[slot].opacity);
    }
}

void Run(const OriginalPhysicsModule& binary, bool dense, bool returnToCursor) {
    Fixture port(dense), original(dense);
    const auto initialize = binary.Function<void (*)(MMDAppState*)>(0x69210);
    const auto advance = binary.Function<void (*)(MMDAppState*, char)>(0x69A20);
    const auto stop = binary.Function<void (*)(MMDAppState*)>(0x6A980);
    // Include near-half-thousandth values and non-exact float fractions.
    for (float start : {0.0f, 0.1f, 0.23333333f, 0.500016689f, 2.0f}) {
        port.app->CurrentFrame() = original.app->CurrentFrame() = 0;
        port.app->state.playbackReturnsToStartFrame =
            original.app->state.playbackReturnsToStartFrame = returnToCursor;
        port.app->PlaybackCursorSeconds() = original.app->PlaybackCursorSeconds() = start;
        UpdateBoneFrames(port.app.get());
        initialize(&original.app->state);
        Compare(port, original, "initialize", start);
        for (int tick = 0; tick <= 180; ++tick) {
            const float seconds = start + tick * 0.011117f;
            port.app->PlaybackCursorSeconds() = original.app->PlaybackCursorSeconds() = seconds;
            PlaybackPoseAdvance(port.app.get(), tick % 3 == 0 ? 1 : 0);
            advance(&original.app->state, tick % 3 == 0 ? 1 : 0);
            Compare(port, original, "advance", seconds);
        }
        // Stop's bone-mode readouts need a selected model; the animation
        // comparison above deliberately isolates accessories from model tracks.
        port.app->ModelSlot(0) = port.model.get();
        original.app->ModelSlot(0) = original.model.get();
        StopPlayback(port.app.get());
        stop(&original.app->state);
        Compare(port, original, "stop", start);
        Check(port.app->CurrentFrame() == original.app->CurrentFrame(),
              "stop restores the same integer frame");
        for (int i = 0; i < 7; ++i) {
            const bool enabled = i % 2 != 0;
            Check((IsWindowEnabled(GetDlgItem(port.window, controlIds[i])) != FALSE) == enabled,
                  "port restores the original playback control snapshot");
            Check((IsWindowEnabled(GetDlgItem(original.window, controlIds[i])) != FALSE) == enabled,
                  "original restores the playback control snapshot");
        }
        Check(port.app->state.characterTransparentMode == 1 && port.app->state.blinkPhase == 3 &&
                  original.app->state.characterTransparentMode == 1 &&
                  original.app->state.blinkPhase == 3,
              "playback preserves adjacent transparency and blink state");
        port.app->ModelSlot(0) = original.app->ModelSlot(0) = nullptr;
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr,
                     "usage: accessory_playback_reference_test <original MikuMikuDance.exe>\n");
        return 1;
    }
    try {
        OriginalPhysicsModule binary(std::filesystem::absolute(argv[1]));
        for (bool returnToCursor : {false, true}) {
            Run(binary, false, returnToCursor);
            Run(binary, true, returnToCursor);
        }
        Check(mismatches == 0, "accessory playback differs from the original x64 executable");
        std::printf("Original x64 accessory playback: %d observations across all %d slots; "
                    "all cursor, flag and payload bytes match\n",
                    observations, mdl::kAccessorySlotCount);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "accessory reference: %s (%d mismatches)\n", error.what(), mismatches);
        return 1;
    }
}
