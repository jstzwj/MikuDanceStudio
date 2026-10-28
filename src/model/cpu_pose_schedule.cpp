#include "mikudancestudio/cpu_pose_schedule.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/cpu_scene_simulation.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include <cstdlib>
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

void RunCpuModelPosePass(CpuModelSlots slots, CpuPosePhase phase, int physicsMode, int skipSlot) {
    for (std::size_t order = 0; order < slots.count; ++order)
        for (std::size_t slot = 0; slot < slots.count; ++slot) {
            auto* model = slots.data[slot];
            if (!model || static_cast<int>(slot) == skipSlot || model->comboSelIndex2 != order)
                continue;
            ModelApplyMorphs(model);
            SetPhysicsMode(model, phase == CpuPosePhase::AfterPhysics ? 1 : 0, slots.data,
                           physicsMode);
        }
}
void RunCpuPhysicsModelPass(CpuModelSlots slots, CpuPhysicsModelPhase phase, CpuSlotOrder order,
                            int skipSlot) {
    for (std::size_t n = 0; n < slots.count; ++n) {
        const auto slot = order == CpuSlotOrder::Forward ? n : slots.count - 1 - n;
        auto* model = slots.data[slot];
        if (!model || static_cast<int>(slot) == skipSlot)
            continue;
        switch (phase) {
        case CpuPhysicsModelPhase::KinematicSync:
            ModelKinematicSync(model);
            break;
        case CpuPhysicsModelPhase::DynamicReseat:
            ModelDynamicReseat(model);
            break;
        case CpuPhysicsModelPhase::Readback:
            ModelPhysicsReadback(model);
            break;
        }
    }
}
CpuSimulationOps LegacyCpuSimulationOps() {
    return {RunCpuModelPosePass, RunCpuPhysicsModelPass,
            [](float out[3], const float in[3]) { d3dx::Get().vec3Normalize(out, in); },
            [] { return std::rand(); }};
}
}
