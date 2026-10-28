#pragma once
#include "rhi_model_replay.hpp"
#include "rhi_frame_passes.hpp"

namespace mikudancestudio::rhi {
enum class FrameOutput {
    Offscreen,
    Presentation
};
// Scene namespaces must be nonzero and unique while loaded. Versions identify
// immutable content, not GPU handles. Explicit unload allows namespace reuse.
struct SceneRendererStats {
    std::uint64_t uploads = 0, uploadBytes = 0, textureUploads = 0, targetRebuilds = 0,
                  submissions = 0;
    std::size_t residentResources = 0, retiringResources = 0, cpuResources = 0;
};
class PreparedModelFrame {
public:
    PreparedModelFrame();
    ~PreparedModelFrame();
    PreparedModelFrame(PreparedModelFrame&&) noexcept;
    PreparedModelFrame& operator=(PreparedModelFrame&&) noexcept;
    PreparedModelFrame(const PreparedModelFrame&) = delete;
    PreparedModelFrame& operator=(const PreparedModelFrame&) = delete;

private:
    struct Data;
    std::unique_ptr<Data> data;
    friend class SceneRenderer;
};
// Single owner / render thread. The device must outlive the renderer. Frames
// are CPU command snapshots, not a second native device or GPU resource owner.
class SceneRenderer {
public:
    explicit SceneRenderer(Device&);
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;
    ModelReplayResult Prepare(std::uint64_t scene, const ModelReplayStream&, PreparedModelFrame&,
                              FrameOutput = FrameOutput::Offscreen);
    ModelReplayResult Prepare(std::uint64_t scene, const RenderFrame&, PreparedModelFrame&);
    bool ReadTarget(std::uint64_t scene, std::uint64_t target, AttachmentReadback&);
    // No target reconstruction, uploads, validation scans, or readback here.
    // A new Prepare for the same scene invalidates the prior prepared frame.
    ModelReplayResult Submit(const PreparedModelFrame&, CompletionToken&);
    bool Collect();
    bool Unload(std::uint64_t scene);
    // Wait for old-device work before releasing handles, retain CPU snapshots.
    bool Rebind(Device&, std::uint32_t timeoutMs = 5000);
    // Release old GPU ownership while preserving CPU versions. Required before
    // recreating a removed native device on APIs that reuse per-adapter devices.
    bool Detach(std::uint32_t timeoutMs = 5000);
    SceneRendererStats Stats() const;

private:
    ModelReplayResult PrepareImpl(std::uint64_t, const ModelReplayStream&, PreparedModelFrame&,
                                  FrameOutput, const RenderFrame*);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
