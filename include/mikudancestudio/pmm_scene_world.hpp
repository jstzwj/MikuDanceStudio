#pragma once
#include "mikudancestudio/model_reader.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include <optional>

namespace mikudancestudio::format {
using PmmWorldMatrix = std::array<float, 16>;

// Row-major matrices with row-vector multiplication, matching the Windows
// bone matrices. Unsupported IK, physics, inheritance, and external parents
// remain nullopt, including their descendants. Invalid topology returns false.
bool EvaluatePmmKinematicWorld(const ModelDocument& model, const PmmEvaluatedModel& pose,
                               std::vector<std::optional<PmmWorldMatrix>>& world);

// The Windows attachment paths transform the bone's rest position by its
// skin matrix; this is not the matrix's translation component alone.
bool PmmAttachedBonePoint(const ModelDocument& model,
                          const std::vector<std::optional<PmmWorldMatrix>>& world,
                          std::int32_t boneIndex, std::array<float, 3>& point);
bool PmmAccessoryWorld(const PmmEvaluatedAccessory& accessory, const ModelDocument* model,
                       const std::vector<std::optional<PmmWorldMatrix>>* bones,
                       PmmWorldMatrix& world);
}
