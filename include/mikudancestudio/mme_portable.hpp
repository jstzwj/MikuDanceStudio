#pragma once
#include "rhi_frame_passes.hpp"
struct MmePortableEffect;
struct MmePortablePass;
struct MmePortableTurn;
#include <vector>
namespace mikudancestudio {
bool RegisterPortableMmeTargets(rhi::RenderFrame&, const std::vector<MmePortableEffect>&,
                                std::string& reason);
bool CheckPortableMme(std::string& reason);
std::shared_ptr<const rhi::ShaderProgram>
PortableMmeProgram(const MmePortableEffect&, const MmePortablePass&, std::string& reason);
bool ApplyPortableMmeMaterial(rhi::ModelReplayDraw&, const MmePortableEffect&, std::string& reason);
// Append one object in native draw order. Scripted materials split the caller's
// pending pass and restore its load state; all edits are atomic on failure.
bool AppendPortableMmeMaterial(rhi::RenderFrame&, rhi::FramePass& pending,
                               const rhi::ModelReplayDraw&, const MmePortableEffect&,
                               std::string& reason);
bool AppendPortableMmePost(rhi::RenderFrame&, const MmePortableEffect&, std::string& reason);
bool AppendPortableMmePosts(rhi::RenderFrame&, const std::vector<MmePortableEffect>&,
                            std::string& reason, bool deferValidation = false);
struct PortableMmeDrawSource {
    rhi::ModelReplayDraw draw;
    unsigned long long object = 0;
    int subset = 0, mode = 0, vertexCount = -1;
    unsigned flags = 0;
    bool transparent = false;
};
bool PrependPortableMmeTurns(rhi::RenderFrame&, const std::vector<MmePortableTurn>&,
                             const std::vector<PortableMmeDrawSource>&,
                             std::vector<MmePortableEffect>& completed, std::string& reason);
}
