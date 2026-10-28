#pragma once
#include <cstddef>
#include <cstdint>

namespace mikudancestudio {
namespace mdl {
struct ModelRecord;
}
// Compatibility boundary for the existing model records, not a second solver.
// The caller owns slots, timeline/catch-up clocks and the shared Bullet world.
// No application, window, renderer, or native GPU resource enters this API.
struct CpuModelSlots {
    mdl::ModelRecord* const* data;
    std::size_t count;
};
enum class CpuPosePhase : std::uint8_t {
    BeforePhysics,
    AfterPhysics
};
enum class CpuPhysicsModelPhase : std::uint8_t {
    KinematicSync,
    DynamicReseat,
    Readback
};
enum class CpuSlotOrder : std::uint8_t {
    Forward,
    Reverse
};

// Preserve display order, then slot order for ties, and each model's complete
// morph -> pose/layers/IK sequence. Skip applies only to this pass.
void RunCpuModelPosePass(CpuModelSlots, CpuPosePhase, int physicsMode, int skipSlot = -1);
// The host decides where world->stepSimulation occurs between these passes.
void RunCpuPhysicsModelPass(CpuModelSlots, CpuPhysicsModelPhase,
                            CpuSlotOrder = CpuSlotOrder::Forward, int skipSlot = -1);
}
