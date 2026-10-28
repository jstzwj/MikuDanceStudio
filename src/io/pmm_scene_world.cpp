#include "mikudancestudio/pmm_scene_world.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

namespace mikudancestudio::format {
namespace {
PmmWorldMatrix Multiply(const PmmWorldMatrix& left, const PmmWorldMatrix& right) {
    PmmWorldMatrix result{};
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            for (int inner = 0; inner < 4; ++inner)
                result[row * 4 + column] += left[row * 4 + inner] * right[inner * 4 + column];
    return result;
}
PmmWorldMatrix LocalMatrix(const BoneData& bone, const PmmEvaluatedBone& pose) {
    const auto& q = pose.rotation;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    PmmWorldMatrix matrix{1 - 2 * (y * y + z * z),
                          2 * (x * y + z * w),
                          2 * (x * z - y * w),
                          0,
                          2 * (x * y - z * w),
                          1 - 2 * (x * x + z * z),
                          2 * (y * z + x * w),
                          0,
                          2 * (x * z + y * w),
                          2 * (y * z - x * w),
                          1 - 2 * (x * x + y * y),
                          0,
                          0,
                          0,
                          0,
                          1};
    for (int axis = 0; axis < 3; ++axis) {
        matrix[12 + axis] = bone.position[axis] + pose.translation[axis];
        for (int column = 0; column < 3; ++column)
            matrix[12 + axis] -= bone.position[column] * matrix[column * 4 + axis];
    }
    return matrix;
}
const std::vector<BoneData>* Bones(const ModelDocument& model) {
    if (model.kind == ModelKind::Pmd)
        return &model.pmd.bones;
    return nullptr;
}
const BoneData* BoneAt(const ModelDocument& model, std::size_t index) {
    if (model.kind == ModelKind::Pmd && index < model.pmd.bones.size())
        return &model.pmd.bones[index];
    if (model.kind == ModelKind::Pmx && index < model.pmx.bones.size())
        return &model.pmx.bones[index].fields;
    return nullptr;
}
}

bool EvaluatePmmKinematicWorld(const ModelDocument& model, const PmmEvaluatedModel& pose,
                               std::vector<std::optional<PmmWorldMatrix>>& world) {
    std::vector<BoneData> pmxBones;
    const auto* bones = Bones(model);
    if (model.kind == ModelKind::Pmx) {
        pmxBones.reserve(model.pmx.bones.size());
        for (const auto& bone : model.pmx.bones)
            pmxBones.push_back(bone.fields);
        bones = &pmxBones;
    }
    if (!bones || bones->size() != pose.bones.size())
        return false;
    std::vector<bool> hostOwned(bones->size(), false);
    if (model.kind == ModelKind::Pmd) {
        for (std::size_t index = 0; index < bones->size(); ++index)
            hostOwned[index] = (*bones)[index].type > 2;
        for (const auto& chain : model.pmd.ikChains) {
            if (chain.boneIndex < hostOwned.size())
                hostOwned[chain.boneIndex] = true;
            if (chain.targetBone < hostOwned.size())
                hostOwned[chain.targetBone] = true;
            for (const auto link : chain.links)
                if (link < hostOwned.size())
                    hostOwned[link] = true;
        }
        for (const auto& rigid : model.pmd.rigids)
            if (rigid.boneIndex >= 0 &&
                static_cast<std::size_t>(rigid.boneIndex) < hostOwned.size())
                hostOwned[rigid.boneIndex] = true;
    } else {
        for (std::size_t index = 0; index < bones->size(); ++index)
            hostOwned[index] = ((*bones)[index].flags & 0x3320) != 0;
        for (const auto& rigid : model.pmx.rigids)
            if (rigid.fields.boneIndex >= 0 &&
                static_cast<std::size_t>(rigid.fields.boneIndex) < hostOwned.size())
                hostOwned[rigid.fields.boneIndex] = true;
        for (std::size_t index = 0; index < bones->size(); ++index)
            if (((*bones)[index].flags & 0x0020) != 0) {
                for (const auto& link : (*bones)[index].ikLinks)
                    if (link.boneIndex >= 0 &&
                        static_cast<std::size_t>(link.boneIndex) < hostOwned.size())
                        hostOwned[link.boneIndex] = true;
                if ((*bones)[index].ikTarget >= 0 &&
                    static_cast<std::size_t>((*bones)[index].ikTarget) < hostOwned.size())
                    hostOwned[(*bones)[index].ikTarget] = true;
            }
    }
    std::vector<std::optional<PmmWorldMatrix>> candidate(bones->size());
    std::vector<std::uint8_t> state(bones->size(), 0);
    const std::function<bool(std::size_t)> visit = [&](std::size_t index) {
        if (state[index] == 2)
            return true;
        if (state[index] == 1)
            return false;
        state[index] = 1;
        const auto& bone = (*bones)[index];
        if (bone.parent < -1 ||
            (bone.parent >= 0 && (static_cast<std::size_t>(bone.parent) >= bones->size() ||
                                  !visit(static_cast<std::size_t>(bone.parent)))))
            return false;
        bool finite = true;
        for (const float component : bone.position)
            finite &= std::isfinite(component);
        for (const float component : pose.bones[index].translation)
            finite &= std::isfinite(component);
        for (const float component : pose.bones[index].rotation)
            finite &= std::isfinite(component);
        if (!finite)
            return false;
        if (!hostOwned[index] && (bone.parent < 0 || candidate[bone.parent])) {
            const auto local = LocalMatrix(bone, pose.bones[index]);
            candidate[index] = bone.parent < 0 ? local : Multiply(local, *candidate[bone.parent]);
        }
        state[index] = 2;
        return true;
    };
    for (std::size_t index = 0; index < bones->size(); ++index)
        if (!visit(index))
            return false;
    world = std::move(candidate);
    return true;
}

bool PmmAttachedBonePoint(const ModelDocument& model,
                          const std::vector<std::optional<PmmWorldMatrix>>& world,
                          std::int32_t boneIndex, std::array<float, 3>& point) {
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= world.size() || !world[boneIndex])
        return false;
    const BoneData* bone = BoneAt(model, static_cast<std::size_t>(boneIndex));
    if (!bone)
        return false;
    std::array<float, 3> result{};
    const auto& matrix = *world[boneIndex];
    for (int axis = 0; axis < 3; ++axis) {
        result[axis] = matrix[12 + axis];
        for (int column = 0; column < 3; ++column)
            result[axis] += bone->position[column] * matrix[column * 4 + axis];
    }
    point = result;
    return true;
}

bool PmmAccessoryWorld(const PmmEvaluatedAccessory& accessory, const ModelDocument* model,
                       const std::vector<std::optional<PmmWorldMatrix>>* bones,
                       PmmWorldMatrix& world) {
    const auto& angles = accessory.rotation;
    for (const float angle : angles)
        if (!std::isfinite(angle))
            return false;
    for (const float position : accessory.position)
        if (!std::isfinite(position))
            return false;
    if (!std::isfinite(accessory.scale))
        return false;
    PmmWorldMatrix result{accessory.scale * 10,
                          0,
                          0,
                          0,
                          0,
                          accessory.scale * 10,
                          0,
                          0,
                          0,
                          0,
                          accessory.scale * 10,
                          0,
                          0,
                          0,
                          0,
                          1};
    const float cz = std::cos(angles[2]), sz = std::sin(angles[2]);
    const PmmWorldMatrix rotateZ{cz, sz, 0, 0, -sz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float cx = std::cos(angles[0]), sx = std::sin(angles[0]);
    const PmmWorldMatrix rotateX{1, 0, 0, 0, 0, cx, sx, 0, 0, -sx, cx, 0, 0, 0, 0, 1};
    const float cy = std::cos(angles[1]), sy = std::sin(angles[1]);
    const PmmWorldMatrix rotateY{cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1};
    PmmWorldMatrix translation{1,
                               0,
                               0,
                               0,
                               0,
                               1,
                               0,
                               0,
                               0,
                               0,
                               1,
                               0,
                               accessory.position[0],
                               accessory.position[1],
                               accessory.position[2],
                               1};
    result = Multiply(Multiply(Multiply(Multiply(result, rotateZ), rotateX), rotateY), translation);
    if (accessory.parentModel >= 0) {
        if (!model || !bones)
            return false;
        const auto index = std::max(accessory.parentBone, 0);
        if (static_cast<std::size_t>(index) >= bones->size() || !(*bones)[index])
            return false;
        const BoneData* rest = BoneAt(*model, static_cast<std::size_t>(index));
        if (!rest)
            return false;
        translation[12] = rest->position[0];
        translation[13] = rest->position[1];
        translation[14] = rest->position[2];
        result = Multiply(Multiply(result, translation), *(*bones)[index]);
    }
    world = result;
    return true;
}
}
