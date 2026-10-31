#pragma once

#include <cmath>
#include "mikudancestudio/cpu_bone_axis.hpp"

#include "mikudancestudio/d3dx_dyn.hpp"

namespace mikudancestudio::mdl {

// VPD import and ordinary bone-key registration use the same correction:
// keep the angle's sign relative to the source axis, then rotate about the
// bone's axis. The mirrored registrar has a different reflection sequence.
// x64: RVAs 0x6C144..0x6C273 / 0xC95E5..0xC972E.
// x86: VAs 0x4190B5..0x419221 / 0x49DA43..0x49DBA3.
inline bool ConstrainBoneRotationAxis(float* quaternion, const float (&boneAxis)[3]) {
    auto& api=d3dx::Get();
    struct Ops {
        d3dx::Api& api;
        void quatToAxisAngle(const float* q,float* axis,float* angle){api.quatToAxisAngle(q,axis,angle);}
        void quatNormalize(float* out,const float* in){api.quatNormalize(out,in);}
    } ops{api};
    return cpu_bone::ConstrainRotationAxis(quaternion,boneAxis,ops);
}
} // namespace mikudancestudio::mdl
