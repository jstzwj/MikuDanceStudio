#pragma once
#include "render_scene_passes.hpp"
#include <memory>

namespace mikudancestudio::render {
// CPU scene walk owns ordering and clear/target semantics. No native capture,
// handles, device epoch, or inferred GPU state is accepted by this assembler.
struct SceneFrameDesc {
    rhi::HistoryDesc target;
    rhi::ClearColor clear;
    std::uint64_t scene = 1, frame = 0;
    std::optional<rhi::ModelViewport> initializedRegion;
};
struct SceneFrameStats {
    std::size_t draws = 0, resources = 0, residentBytes = 0, identityCount = 0;
    std::size_t stagedResourceBytes = 0;
    std::uint64_t publications = 0;
};
class CpuSceneFrameAssembler {
public:
    CpuSceneFrameAssembler();
    ~CpuSceneFrameAssembler();
    CpuSceneFrameAssembler(const CpuSceneFrameAssembler&) = delete;
    CpuSceneFrameAssembler& operator=(const CpuSceneFrameAssembler&) = delete;
    void Begin(const SceneFrameDesc&);
    bool Recording() const;
    void Append(SceneDrawReplacement);
    void Block(const char* reason);
    // All or nothing, including version allocation. Output is unchanged when
    // blocked. Resource IDs survive reordering; changed/revived bytes advance
    // versions. Removed resources retain only identity/version tombstones.
    rhi::ModelReplayResult Finish(rhi::ModelReplayStream&);
    void Reset(); // call with SceneRenderer::Unload before namespace reuse
    SceneFrameStats Stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
