#pragma once
#include "mikudancestudio/cpu_pose_schedule.hpp"
#include <array>

class btDiscreteDynamicsWorld;

namespace mikudancestudio {
// Per-scene state, independent of a render device, window and wall clock.
// The host supplies absolute playback targets and keeps UI/audio loop policy.
struct CpuSimulationState {
    float cursorSeconds = 0, remainingSeconds = 0;
    int physicsMode = 0;
    std::uint8_t resetPending = 0, bodiesMoved = 0;
    std::array<float, 3> gravityDirection{0, -1, 0};
    float gravityMagnitude = 9.8f, gravityNoiseTimer = 0;
    int gravityNoise = 0;
    bool gravityNoiseEnabled = false;
};

// Preserve the existing CPU arithmetic through an explicit adapter. This
// scheduling layer itself has no dependency on Win32, D3DX or model layouts.
struct CpuSimulationOps {
    void (*pose)(CpuModelSlots, CpuPosePhase, int mode, int skipSlot) = nullptr;
    void (*physics)(CpuModelSlots, CpuPhysicsModelPhase, CpuSlotOrder, int skipSlot) = nullptr;
    void (*normalize)(float out[3], const float in[3]) = nullptr;
    int (*random)() = nullptr; // legacy rand(): integer in [0,32767]
    int (*randomFromContext)(void*) = nullptr;
};
struct CpuSimulationContext {
    CpuModelSlots models{nullptr, 0};
    btDiscreteDynamicsWorld* world = nullptr; // one shared world; caller owns it
    CpuSimulationOps ops;
};
enum class CpuSimulationEvent : std::uint8_t {
    CatchupBeforeStep,
    CatchupAfterStep,
    GravityApplied,
    BeforePose,
    KinematicSync,
    MainStep,
    ExtraStep,
    Stepped,
    SettleReseat,
    SettleStep,
    Settled,
    Readback,
    AfterPose
};
struct CpuSimulationHooks {
    void* user = nullptr;
    // Called at each cursor before pose evaluation, then once at the final
    // clamped target. The final callback may evaluate global scene tracks.
    void (*advance)(void*, CpuSimulationState&, bool substep) = nullptr;
    // Observation only; index is the 1-based settle iteration, dt is the
    // submitted timeStep. Stage boundaries retain the legacy diagnostic order.
    void (*observe)(void*, const CpuSimulationState&, CpuSimulationEvent, int index,
                    float dt) = nullptr;
};
struct CpuSimulationFrame {
    float deltaSeconds = 0;
    bool enabled = true, selectionActive = false, selectionWasActive = false;
    bool modelMode = false, frameStep = false, playbackActive = false;
    bool accessoryEditing = false, frameCopyPending = false;
    bool skipIdleWorld = false; // diagnostic policy; default off
    bool overrideMainTimeStep = false;
    float mainTimeStep = 0;
};

// The budget is initialized by the host before catch-up, including paused
// frames. Keep float stores/subtractions and negative remainders verbatim.
// Invalid context returns false before changing state or touching the world.
bool RunCpuSimulationCatchup(CpuSimulationState&, const CpuSimulationContext&, float targetSeconds,
                             int skipSlot = -1, CpuSimulationHooks = {});
bool RunCpuSimulationFrame(CpuSimulationState&, const CpuSimulationContext&,
                           const CpuSimulationFrame&, CpuSimulationHooks = {});

// Windows compatibility adapter; uses the existing morph/IK/body operations
// and imported D3DX normalization, without creating a graphics device.
CpuSimulationOps LegacyCpuSimulationOps();
}
