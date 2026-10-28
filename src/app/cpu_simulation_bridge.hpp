#pragma once
#include "mikudancestudio/mmd_app.hpp"

namespace mikudancestudio {
// The serialized/ABI state remains the editor's authority at each boundary.
// Only the per-scene dt remainder has no legacy field. Never mirror it in a
// process global, so two scene hosts cannot consume each other's budget.
inline CpuSimulationState& ImportCpuSimulation(MMDApp& app) {
    auto& s = app.CpuSimulation();
    s.cursorSeconds = app.PlaybackCursorSeconds();
    s.physicsMode = app.PlaybackPhysicsMode();
    s.resetPending = app.PhysicsResetPending();
    s.bodiesMoved = app.state.physicsBodiesMoved;
    s.gravityDirection = {app.GravityX(), app.GravityY(), app.GravityZ()};
    s.gravityMagnitude = app.GravityMagnitude();
    s.gravityNoise = app.GravityNoise();
    s.gravityNoiseEnabled = app.GravityNoiseEnabled() != 0;
    s.gravityNoiseTimer = app.state.gravityNoiseTimer;
    return s;
}
inline void ExportCpuSimulation(MMDApp& app, const CpuSimulationState& s) {
    app.PlaybackCursorSeconds() = s.cursorSeconds;
    app.PhysicsResetPending() = s.resetPending;
    app.state.physicsBodiesMoved = s.bodiesMoved;
    app.GravityY() = s.gravityDirection[1]; // all-zero direction correction
    app.state.gravityNoiseTimer = s.gravityNoiseTimer;
}
inline CpuSimulationContext EditorCpuSimulationContext(MMDApp& app) {
    return {{app.ModelSlots(), kModelSlotCount},
            app.Physics() ? app.Physics()->world : nullptr,
            LegacyCpuSimulationOps()};
}
}
