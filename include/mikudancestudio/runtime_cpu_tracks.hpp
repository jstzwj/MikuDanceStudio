#pragma once
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include <string>

namespace mikudancestudio {
namespace mdl {
struct ModelRecord;
}
// CPU compatibility adapter. Publication preserves the linked indices used by
// the existing seek/advance/physics-arming code; it never owns GPU resources.
bool ValidateCpuPmmTracks(mdl::ModelRecord*, const format::PmmSceneModel&, format::PmmVersion,
                          std::string& reason);
bool PublishCpuPmmTracks(mdl::ModelRecord*, const format::PmmSceneModel&, format::PmmVersion,
                         std::string& reason);
// Shared with the editor: rigid/selector bones retain native arming and backup
// semantics. Portable evaluation overrides only the same eligible bones.
void ApplyCpuPmmPose(mdl::ModelRecord*, const format::PmmEvaluatedModel&, bool selectors);
bool ApplyMappedCpuPmmPose(mdl::ModelRecord*, const format::PmmSceneModel&,
                           const format::PmmEvaluatedModel&, format::PmmVersion);
}
