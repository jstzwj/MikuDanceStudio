#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <cstdio>
#include <initializer_list>
#include <memory>

namespace mikudancestudio {
MMDApp::MMDApp() : state{} {
}
}

namespace {
using namespace mikudancestudio;
int failures = 0;

template <class Key, class Source, class Register, class Matches>
void CheckTrack(const char* name, MMDApp& app, Key*& track, Source source, Register registerKey,
                Matches matches) {
    const int initialFailures = failures;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "%s: %s\n", name, message);
            ++failures;
        }
    };
    app.CurrentFrame() = 0;
    app.state.lastRegisteredFrame = 0;
    track = nullptr;
    check(registerKey(&app, source) == 0, "missing track returns failure");

    auto table = std::make_unique<Key[]>(10000);
    track = table.get();
    const auto put = [&](unsigned relativeFrame) {
        source.frame = relativeFrame;
        check(registerKey(&app, source) == 1, "registration succeeds");
    };
    put(10);
    check(table[0].next == 1 && table[1].previous == 0 && table[1].frame == 10 &&
              table[1].next == 0,
          "first nonzero frame follows the head");
    put(20);

    // Original registrars compare the head before following its next index.
    // Skipping it creates a second frame-0 record, which is then reused as free.
    put(0);
    table[0].selected = 0;
    put(0);
    check(table[0].selected == 1 && matches(table[0], source),
          "frame zero overwrites the head payload");
    check(table[0].next == 1 && table[1].previous == 0 && table[1].next == 2 &&
              table[2].previous == 1 && table[3].selected == 0 && table[3].frame == 0,
          "head overwrite preserves neighbours and allocates no record");
    check(app.state.lastRegisteredFrame == 20,
          "head overwrite preserves the last registered frame");

    put(30);
    put(5);
    put(10);
    check(table[1].selected == 1 && matches(table[1], source),
          "existing nonzero frame is overwritten");
    check(table[5].frame == 0, "overwriting an existing frame allocates no record");
    app.CurrentFrame() = 17;
    put(3);
    check(table[2].frame == 20 && matches(table[2], source),
          "clipboard frame is relative to the current frame");
    check(app.state.lastRegisteredFrame == 30, "only inserted later frames extend the timeline");

    const auto checkChain = [&](std::initializer_list<unsigned> frames) {
        unsigned index = 0, previous = 0, ordinal = 0;
        for (unsigned frame : frames) {
            if (index >= 10000) {
                check(false, "chain index stays within the track");
                break;
            }
            const Key& key = table[index];
            check(key.frame == frame, "inserts preserve every sorted frame");
            check(key.previous == previous, "backward links match the forward walk");
            previous = index;
            index = key.next;
            check((index == 0) == (++ordinal == frames.size()),
                  "chain ends after the expected number of records");
        }
    };
    checkChain({0, 5, 10, 20, 30});

    // A clipboard offset can address frame zero from a nonzero current frame.
    put(0u - static_cast<unsigned>(app.CurrentFrame()));
    checkChain({0, 5, 10, 20, 30});

    // Updating an existing record must succeed even when all slots are used.
    // Skip this case after a broken walk, which could open the overflow dialog.
    if (failures == initialFailures) {
        for (unsigned i = 0; i < 10000; ++i) {
            table[i] = {};
            table[i].frame = i;
            table[i].previous = i == 0 ? 0 : i - 1;
            table[i].next = i == 9999 ? 0 : i + 1;
        }
        app.CurrentFrame() = 0;
        app.state.lastRegisteredFrame = 9999;
        put(0);
        put(9999);
        check(matches(table[0], source) && matches(table[9999], source) && table[0].selected == 1 &&
                  table[9999].selected == 1,
              "full tracks still overwrite existing head and tail payloads");
        bool intact = true;
        for (unsigned i = 0; i < 10000; ++i) {
            intact &= table[i].frame == i && table[i].previous == (i == 0 ? 0 : i - 1) &&
                      table[i].next == (i == 9999 ? 0 : i + 1);
        }
        check(intact && app.state.lastRegisteredFrame == 9999,
              "full-track overwrites preserve all links and timeline extent");
    }
    track = nullptr;
}
} // namespace

int main() {
    using namespace mikudancestudio;
    auto app = std::make_unique<MMDApp>();
    mdl::CameraClipboardRecord camera{};
    camera.distance = -35.0f;
    camera.eye[1] = 12.5f;
    camera.parentModel = 2;
    camera.parentBone = 4;
    CheckTrack(
        "camera", *app, app->CameraKeys(), camera,
        [](MMDApp* a, const mdl::CameraClipboardRecord& s) { return RegisterCameraKey(a, s); },
        [](const auto& k, const auto& s) {
            return k.distance == s.distance && k.eye[1] == s.eye[1] &&
                   k.parentModel == s.parentModel && k.parentBone == s.parentBone;
        });
    mdl::LightClipboardRecord light{};
    light.color[0] = 0.75f;
    light.direction[1] = -1.0f;
    CheckTrack("light", *app, app->LightKeys(), light, RegisterLightKey,
               [](const auto& k, const auto& s) {
                   return k.color[0] == s.color[0] && k.direction[1] == s.direction[1];
               });
    mdl::ShadowClipboardRecord shadow{};
    shadow.mode = 2;
    shadow.distance = 0.125f;
    CheckTrack(
        "shadow", *app, app->ShadowKeys(), shadow, RegisterSelfShadowKey,
        [](const auto& k, const auto& s) { return k.mode == s.mode && k.distance == s.distance; });
    mdl::GravityClipboardRecord gravity{};
    gravity.acceleration = 9.8f;
    gravity.direction[1] = -1.0f;
    gravity.noise = 12;
    gravity.noiseEnabled = 1;
    CheckTrack("gravity", *app, app->GravityKeys(), gravity, RegisterGravityKey,
               [](const auto& k, const auto& s) {
                   return k.acceleration == s.acceleration && k.direction[1] == s.direction[1] &&
                          k.noise == s.noise && k.noiseEnabled == s.noiseEnabled;
               });
    return failures == 0 ? 0 : 1;
}
