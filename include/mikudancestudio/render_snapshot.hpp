#pragma once
#include "mikudancestudio/scene_session.hpp"
#include "mikudancestudio/rhi.hpp"
#include "mikudancestudio/rhi_material_textures.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace mikudancestudio::render {
enum class TextureEncoding {
    Cp932,
    Utf8,
    Utf16LE,
    SharedToon
};
struct TextureReference {
    std::int32_t index = -1;
    TextureEncoding encoding = TextureEncoding::Cp932;
    bool defaultFallback = false;
    std::vector<std::uint8_t> path;
};
enum class BatchDependency {
    Independent,
    Ik,
    Physics,
    Unsupported,
    Invalid
};
struct MaterialBatch {
    std::uint32_t firstIndex = 0, indexCount = 0;
    std::uint32_t materialIndex = 0;
    std::int32_t textureIndex = -1;
    TextureReference texture, sphere, toon;
    std::uint8_t flags = 0, sphereMode = 0;
    BatchDependency dependency = BatchDependency::Unsupported;
    std::array<float, 4> diffuse{};
};
// One record per actual draw, including a second pass for additive materials.
// Raster state is populated at the draw boundary, never guessed from PMM.
struct MaterialDraw {
    std::uint8_t modelSlot = 0, modelOrder = 0;
    std::uint32_t sequence = 0, pass = 0;
    MaterialBatch batch;
    rhi::BatchRasterState raster;
    std::optional<rhi::MaterialShaderInputs> shaderInputs;
};
struct ModelSnapshot {
    std::uint8_t slot = 0;
    std::uint8_t drawOrder = 0;
    bool visible = false;
    format::ModelKind kind = format::ModelKind::Unknown;
    std::uint32_t vertexCount = 0, indexCount = 0;
    std::vector<MaterialBatch> batches;
    std::vector<std::optional<format::PmmWorldMatrix>> bones;
};
struct AccessorySnapshot {
    std::uint8_t slot = 0;
    bool visible = false;
    std::optional<format::PmmWorldMatrix> world;
};
struct SceneSnapshot {
    std::uint32_t frame = 0;
    format::PmmEvaluatedCamera camera;
    std::optional<std::array<float, 3>> cameraAttachment;
    std::vector<ModelSnapshot> models;
    // Empty for a document-only capture. A rendering producer appends the
    // concrete draw records at submission time.
    std::vector<MaterialDraw> materialDraws;
    std::vector<AccessorySnapshot> accessories;
};
bool CaptureSceneSnapshot(const format::SceneSession& session, SceneSnapshot& output);
BatchDependency ClassifyBatchDependency(const format::ModelDocument& model,
                                        std::uint32_t firstIndex, std::uint32_t indexCount);
}
