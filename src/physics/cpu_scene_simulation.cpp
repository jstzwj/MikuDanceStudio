#include "mikudancestudio/cpu_scene_simulation.hpp"
#include "btBulletDynamicsCommon.h"
#include "LinearMath/btMmdMath.h"

namespace mikudancestudio {
namespace {
constexpr float kStep = 1.0f / 60.0f;
bool Valid(const CpuSimulationContext& c) {
    return c.world && (c.models.data || c.models.count == 0) && c.ops.pose && c.ops.physics;
}
void Observe(CpuSimulationHooks h, const CpuSimulationState& s, CpuSimulationEvent e, int index = 0,
             float dt = 0) {
    if (h.observe)
        h.observe(h.user, s, e, index, dt);
}
void Step(const CpuSimulationContext& c, const CpuSimulationState& s, CpuSimulationHooks h,
          float dt, CpuSimulationEvent e, int index = 0) {
    // Observers run immediately before submission, as in the old trace shim.
    Observe(h, s, e, index, dt);
    c.world->stepSimulation(dt, 10, kStep);
}
void Gravity(CpuSimulationState& s, const CpuSimulationContext& c, float delta) {
    if (s.physicsMode <= 0)
        return;
    if (s.gravityNoiseEnabled) {
        s.gravityNoiseTimer = s.gravityNoiseTimer + delta;
        if (s.gravityNoiseTimer > 0.5f) {
#if defined(_M_IX86)
            const int rStrength = c.ops.random(), rX = c.ops.random(), rY = c.ops.random(),
                      rZ = c.ops.random();
            float result[3];
            btMmdMath::gravityNoise(rStrength, rX, rY, rZ, s.gravityNoise, s.gravityMagnitude,
                                    s.gravityDirection.data(), result);
            const float e0 = result[0], e1 = result[1], e2 = result[2];
#else
            const int rStrength = c.ops.random();
            const float noise = static_cast<float>(s.gravityNoise);
            const float strength =
                ((static_cast<float>(rStrength) * 10.0f) / 32767.0f) * noise + s.gravityMagnitude;
            const float e0 = ((static_cast<float>(c.ops.random()) * 0.2f) / 32767.0f - 0.1f +
                              s.gravityDirection[0]) *
                             strength;
            const float e1 = ((static_cast<float>(c.ops.random()) * 0.2f) / 32767.0f - 0.1f +
                              s.gravityDirection[1]) *
                             strength;
            const float e2 = ((static_cast<float>(c.ops.random()) * 0.2f) / 32767.0f - 0.1f +
                              s.gravityDirection[2]) *
                             strength;
#endif
            c.world->setGravity(btVector3(e0, e1, e2));
            s.gravityNoiseTimer = 0;
        }
        return; // noise replaces normal gravity even before its timer fires
    }
    auto& dir = s.gravityDirection;
    if (dir[0] == 0 && dir[1] == 0 && dir[2] == 0)
        dir[1] = 0.1f;
    float normalized[3];
    c.ops.normalize(normalized, dir.data());
    c.world->setGravity(btVector3((normalized[0] * s.gravityMagnitude) * 10.0f,
                                  (normalized[1] * s.gravityMagnitude) * 10.0f,
                                  (normalized[2] * s.gravityMagnitude) * 10.0f));
}
}

bool RunCpuSimulationCatchup(CpuSimulationState& s, const CpuSimulationContext& c, float target,
                             int skip, CpuSimulationHooks h) {
    if (!Valid(c))
        return false;
    const int mode = s.physicsMode;
    if (mode >= 1) {
        s.cursorSeconds += kStep;
        if (target > s.cursorSeconds)
            do {
                if (h.advance)
                    h.advance(h.user, s, true);
                c.ops.pose(c.models, CpuPosePhase::BeforePhysics, mode, skip);
                c.ops.physics(c.models, CpuPhysicsModelPhase::KinematicSync, CpuSlotOrder::Forward,
                              skip);
                Step(c, s, h, kStep, CpuSimulationEvent::CatchupBeforeStep);
                s.remainingSeconds -= kStep;
                s.cursorSeconds += kStep;
                Observe(h, s, CpuSimulationEvent::CatchupAfterStep);
            } while (s.cursorSeconds < target);
    }
    s.cursorSeconds = target;
    if (h.advance)
        h.advance(h.user, s, false);
    return true;
}

bool RunCpuSimulationFrame(CpuSimulationState& s, const CpuSimulationContext& c,
                           const CpuSimulationFrame& f, CpuSimulationHooks h) {
    if (!Valid(c) ||
        (s.physicsMode > 0 && (!c.ops.normalize || (s.gravityNoiseEnabled && !c.ops.random))))
        return false;
    if (!f.enabled)
        return true;
    const int mode = s.physicsMode;
    Gravity(s, c, f.deltaSeconds);
    Observe(h, s, CpuSimulationEvent::GravityApplied);
    if (f.selectionActive && !f.modelMode && !f.frameStep && !f.selectionWasActive)
        s.resetPending = 1;
    if (f.accessoryEditing)
        return true;

    c.ops.pose(c.models, CpuPosePhase::BeforePhysics, s.resetPending ? 0 : mode, -1);
    Observe(h, s, CpuSimulationEvent::BeforePose);
    const bool moved = s.bodiesMoved != 0, settle = s.resetPending != 0;
    const bool active = moved || settle || f.playbackActive;
    const bool run = mode == 1 || mode == 2 || (mode == 3 && active);
    if (run && !(f.skipIdleWorld && !active) && !f.frameCopyPending) {
        c.ops.physics(c.models, CpuPhysicsModelPhase::KinematicSync, CpuSlotOrder::Forward, -1);
        Observe(h, s, CpuSimulationEvent::KinematicSync);
        const float dt = moved && mode == 3       ? kStep
                         : f.overrideMainTimeStep ? f.mainTimeStep
                                                  : s.remainingSeconds;
        Step(c, s, h, dt, CpuSimulationEvent::MainStep);
        if (moved && mode == 3)
            Step(c, s, h, kStep, CpuSimulationEvent::ExtraStep);
        Observe(h, s, CpuSimulationEvent::Stepped);
        if (settle) {
            for (int n = 1; n <= 3; ++n) {
                c.ops.physics(c.models, CpuPhysicsModelPhase::DynamicReseat, CpuSlotOrder::Forward,
                              -1);
                Observe(h, s, CpuSimulationEvent::SettleReseat, n);
                Step(c, s, h, kStep, CpuSimulationEvent::SettleStep, n);
                Observe(h, s, CpuSimulationEvent::Stepped, n);
            }
            s.resetPending = 0;
        }
        Observe(h, s, CpuSimulationEvent::Settled);
        c.ops.physics(c.models, CpuPhysicsModelPhase::Readback, CpuSlotOrder::Forward, -1);
        Observe(h, s, CpuSimulationEvent::Readback);
        s.bodiesMoved = 0;
    }
    c.ops.pose(c.models, CpuPosePhase::AfterPhysics, mode, -1);
    Observe(h, s, CpuSimulationEvent::AfterPose);
    return true;
}
}
