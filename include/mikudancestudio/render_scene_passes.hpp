#pragma once
#include "render_model_inputs.hpp"

namespace mikudancestudio::render {
enum class SceneDrawFamily {
    GroundLines,
    GroundPlane,
    ProjectedModelShadow,
    ModelMaterial,
    AccessoryMaterial,
    ModelOutline
};
struct ScenePassSettings {
    bool stencil = true, alphaTest = true, shadowBlend = true;
    unsigned maxAnisotropy = 16;
    // Vertex-color shadows have no normals. The CPU material/light records
    // determine their emissive/ambient color; no native state is inherited.
    ModelMaterialInputs shadowMaterial;
};
bool BuildScenePassInputs(const ModelFrameInputs&, SceneDrawFamily, const ScenePassSettings&,
                          rhi::ModelPipeline&, const ModelInputMath& = {});
bool BuildGroundDraw(const ModelFrameInputs&, SceneDrawFamily, const ScenePassSettings&,
                     const rhi::HistoryDesc&, rhi::ClearColor, rhi::ModelReplayStream&,
                     const ModelInputMath& = {});
struct AccessoryPassInputs {
    ModelMaterialInputs material;
    float opacity = 1;
    unsigned textureType = 0;
    bool primaryTexture = false, sphereTexture = false, additive = false;
    bool afterModelPass = false;
    // Screen sampling is unsupported; an explicitly unbound screen source
    // still has defined native semantics (terminate the texture cascade).
    bool unboundScreen = false;
};
bool BuildAccessoryPassInputs(const ModelFrameInputs&, const AccessoryPassInputs&,
                              const ScenePassSettings&, rhi::ModelPipeline&,
                              const ModelInputMath& = {});
struct AccessorySubset {
    std::uint32_t material = 0, firstIndex = 0, indexCount = 0;
};
struct AccessoryCpuResource {
    ModelGeometrySnapshot geometry;
    std::vector<AccessorySubset> subsets;
    std::vector<ModelMaterialTextureSources> materials;
    std::vector<bool> texturesReady;
    std::string failure;
};
// Explicit boundary is supplied by the ordered scene walk. Each fragment owns
// one draw and immutable CPU resources. Only diagnostics need a native baseline.
struct SceneDrawReplacement {
    std::size_t boundary = 0;
    SceneDrawFamily family = SceneDrawFamily::GroundLines;
    std::uint32_t model = 0, material = 0;
    rhi::ModelReplayStream cpu;
    // Optional canonical CPU asset identities. Shared Toon/base textures can
    // keep one resident version across materials and objects. Empty identities
    // remain local to the producer; GPU addresses are never identities.
    std::array<std::string, 3> textureIdentities{};
};
// Audit uses active shader/pipeline fields and exact geometry/texture bytes.
// It never copies a baseline field into generated input. Atomic on failure.
rhi::ModelReplayResult ReplaceSceneDraws(const rhi::ModelReplayStream& baseline,
                                         const std::vector<SceneDrawReplacement>&,
                                         rhi::ModelReplayStream& output);
const char* SceneDrawFamilyName(SceneDrawFamily);
}
