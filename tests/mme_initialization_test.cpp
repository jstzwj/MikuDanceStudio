#include "mmeffect/device_initialization.h"

#include <cstdio>
#include <string>

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    mme::DeviceInitialization initialization;
    bool effectsDisabled = false;
    int initializeResult = 1;
    int realBeginSceneCalls = 0;
    std::string events;
    const auto frame = [&] {
        const bool ready = initialization.EnsureReady(
            [&] { events += 'I'; return initializeResult; },
            [&] {
                check(!initialization.IsReady(), "Failure must remain pending");
                events += 'E';
                effectsDisabled = true;
            });
        if (ready) {
            events += 'B';
            ++realBeginSceneCalls;
        }
        return ready;
    };

    check(!frame(), "First initialization failure must not begin a scene");
    check(effectsDisabled, "Failure must disable effects");
    check(!frame(), "A disabled engine must still retry failed initialization");
    check(events == "IEIE" && realBeginSceneCalls == 0,
          "Each failure must report and bypass the real device");

    // A different nonzero error has the same pending/retry semantics.
    initializeResult = -1;
    check(!frame(), "Negative initialization errors are also failures");
    check(events == "IEIEIE", "Every failed retry must report again");

    initializeResult = 0;
    check(frame() && initialization.IsReady(), "Successful retry must commit readiness");
    check(effectsDisabled, "Successful retry must not reset sticky effects-disabled state");
    initializeResult = 1;
    check(frame(), "An initialized device must not reinitialize each frame");
    check(events == "IEIEIEIBB" && realBeginSceneCalls == 2,
          "Successful frames must begin the real scene exactly once");

    initialization.Reset();
    check(!initialization.IsReady(), "Device destruction must reset readiness");
    check(!frame(), "Replacement device must attempt initialization again");
    check(events == "IEIEIEIBBIE", "Device lifetime reset must restore retry behavior");

    // First-attempt success must not itself disable effects.
    initialization.Reset();
    effectsDisabled = false;
    initializeResult = 0;
    check(frame() && !effectsDisabled, "Normal initialization must leave effects enabled");
    if (failures == 0)
        std::puts("MME device initialization regression passed");
    return failures == 0 ? 0 : 1;
}
