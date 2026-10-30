#pragma once
#include "rhi_frame_passes.hpp"
struct MmePortableEffect;
struct MmePortablePass;
#include <vector>
namespace mikudancestudio {
bool RegisterPortableMmeTargets(rhi::RenderFrame&, const std::vector<MmePortableEffect>&,
                                std::string& reason);
bool CheckPortableMme(std::string& reason);
std::shared_ptr<const rhi::ShaderProgram>
PortableMmeProgram(const MmePortableEffect&, const MmePortablePass&, std::string& reason);
bool ApplyPortableMmeMaterial(rhi::ModelReplayDraw&, const MmePortableEffect&, std::string& reason);
bool AppendPortableMmePost(rhi::RenderFrame&, const MmePortableEffect&, std::string& reason);
}
