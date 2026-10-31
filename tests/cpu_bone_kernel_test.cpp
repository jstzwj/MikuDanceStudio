#include "mikudancestudio/cpu_bone_transform.hpp"
#include "mikudancestudio/cpu_model_state.hpp"
#include <cmath>
#include <iostream>
using namespace mikudancestudio;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << '\n';                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
bool Near(float a, float b) {
    return std::abs(a - b) < 2e-5f;
}
int main() {
    using Kernel = cpu_bone::TransformKernel<cpu_model::Traits>;
    cpu_model::Bone bones[3];
    bones[0].type = cpu_bone::BoneType::UnderIk;
    bones[1].parent = 0;
    bones[1].position[0] = 2;
    bones[1].type = cpu_bone::BoneType::Effector;
    bones[2].position[1] = 2;
    bones[2].type = cpu_bone::BoneType::Ik;
    std::uint16_t link = 0;
    cpu_model::IkChain chain;
    chain.boneIndex = 2;
    chain.targetBone = 1;
    chain.links = &link;
    chain.linkCount = 1;
    chain.iterations = 32;
    chain.maxAngle = .25f;
    cpu_model::Binding binding;
    cpu_model::Model model;
    model.boneTable = bones;
    model.boneCount = 3;
    model.ikChains = &chain;
    model.ikChainCount = 1;
    model.boneOrderTable = &binding;
    Kernel::Run(&model, 0, 0, nullptr, 0);
    auto tip = cpu_math::TransformPoint(
        {2, 0, 0}, cpu_model::Math::Load(reinterpret_cast<cpu_model::Matrix*>(bones[1].matInit)));
    CHECK(Near(tip[0], 0) && Near(tip[1], 2));
    CHECK(Near(bones[0].rotQuat2[2], std::sqrt(.5f)));
    // The channel router must not transform after-physics bones early.
    cpu_model::Bone delayed;
    delayed.flags = cpu_bone::kBoneFlagAfterPhysics;
    delayed.physicsOffset[0] = 3;
    model.boneTable = &delayed;
    model.boneCount = 1;
    model.ikChainCount = 0;
    Kernel::Run(&model, 0, 0, nullptr, 0);
    CHECK(delayed.matInit[12] == 0);
    Kernel::Run(&model, 1, 0, nullptr, 0);
    CHECK(delayed.matInit[12] == 3);
    // Portable state can visit children after parents without changing source IDs.
    cpu_model::Bone reversed[2];
    reversed[0].parent = 1;
    reversed[0].physicsOffset[0] = 1;
    reversed[1].physicsOffset[1] = 2;
    std::vector<std::size_t> order{1, 0};
    model.order = &order;
    model.boneTable = reversed;
    model.boneCount = 2;
    Kernel::Run(&model, 0, 0, nullptr, 0);
    CHECK(reversed[0].matInit[12] == 1 && reversed[0].matInit[13] == 2);
    std::cout
        << "shared CCD solve, before/after physics channels and source identity order passed\n";
}
