#pragma once
#include "mikudancestudio/runtime_cpu_scene.hpp"

namespace mikudancestudio {
// Windows X-file decoding is a CPU import boundary. No graphics device is
// created, borrowed or queried. RHI owns the eventual buffers and textures.
class RuntimeCpuAccessory {
public:
    RuntimeCpuAccessory();
    ~RuntimeCpuAccessory();
    bool HasScreenTexture() const;
    bool Load(const std::filesystem::path&, std::uint64_t firstGeometryId, std::string& reason);
    bool LoadMemory(const void*, std::size_t bytes, const std::filesystem::path& directory,
                    std::uint64_t firstGeometryId, std::string& reason, bool editorAxis = false);
    bool Build(const render::ModelFrameInputs&, const render::ModelMatrix& placement,
               const format::PmmEvaluatedAccessory&, bool additive, bool afterModels,
               const rhi::HistoryDesc&, rhi::ClearColor,
               const std::function<void(render::SceneDrawReplacement)>&, std::string& reason,
               bool editorAxis = false);
    const render::ModelGeometrySnapshot* Geometry() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
