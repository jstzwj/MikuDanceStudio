#pragma once
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/cpu_keyframe.hpp"
namespace mikudancestudio {
struct NativeKeyframeTraits {
    using Model = mdl::ModelRecord;
    using Bone = mdl::BoneRecord;
    using BoneKey = mdl::BoneKey;
    using Morph = mdl::MorphRecord;
    using MorphKey = mdl::MorphKey;
    using DisplayKey = mdl::DisplayKey;
    using IkChain = mdl::IkChain;
    using Binding = mdl::BoneOrderEntry;
    using Rigid = mdl::RigidRecord;
    static auto IkStates(const DisplayKey& key) { return mdl::IkStates(key); }
    static auto SelectorStates(const DisplayKey& key) { return mdl::SelectorStates(key); }
};
using NativeKeyframeKernel = cpu_keyframe::Kernel<NativeKeyframeTraits>;
}
