#pragma once
#include "render_scene_passes.hpp"
#include <memory>
#include <functional>

namespace mikudancestudio {
class MMDApp;
namespace mdl {
struct ModelRecord;
}
class PhysicsScene;
class RuntimeCpuScene;
// Windows legacy CPU arithmetic adapter. Import and deformation have no GPU
// ownership. Final solved matrices come from the host's unchanged IK/physics
// schedule; this adapter does not invent a second animation solver.
class RuntimeCpuModel {
public:
    RuntimeCpuModel();
    ~RuntimeCpuModel();
    RuntimeCpuModel(const RuntimeCpuModel&) = delete;
    RuntimeCpuModel& operator=(const RuntimeCpuModel&) = delete;
    bool Load(const std::filesystem::path&, std::string& reason);
    bool Deform(const std::vector<render::ModelMatrix>& finalSkinMatrices,
                const std::vector<float>& morphWeights, float edgeDistance, bool opaqueEdge,
                std::string& reason);
    bool PrepareMaterials(const rhi::ToonTextureResolver&, std::string& reason);
    bool Build(const render::ModelFrameInputs&, const rhi::HistoryDesc&, rhi::ClearColor,
               const std::function<void(render::SceneDrawReplacement)>&, std::string& reason);
    const render::ModelGeometrySnapshot* Geometry() const;
    bool BuildEdges(const render::ModelFrameInputs&, render::SceneDrawFamily,
                    const render::ScenePassSettings&, const rhi::AtlasAsset* helper,
                    const rhi::HistoryDesc&, rhi::ClearColor,
                    const std::function<void(render::SceneDrawReplacement)>&, std::string& reason);

private:
    friend class RuntimeCpuScene;
    bool Load(const std::filesystem::path&, MMDApp*, PhysicsScene*, std::string&);
    mdl::ModelRecord* Record() const;
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
