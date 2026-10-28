#pragma once
#include "pmm_scene_evaluation.hpp"
#include "rhi_model_pipeline.hpp"
#include "render_model_geometry.hpp"
#include <memory>

namespace mikudancestudio::render {
using ModelMatrix = std::array<float, 16>;
// CPU arithmetic is a compatibility policy, separate from a graphics device.
// Windows can retain its measured D3DX arithmetic without GPU state reads.
struct ModelInputMath {
    void (*multiply)(ModelMatrix&, const ModelMatrix&, const ModelMatrix&) = nullptr;
    void (*normalize)(std::array<float, 3>&) = nullptr;
    std::array<float, 3> (*cross)(const std::array<float, 3>&,
                                  const std::array<float, 3>&) = nullptr;
    void (*rotateX)(ModelMatrix&, float) = nullptr;
    void (*lookAt)(ModelMatrix&, const std::array<float, 3>&) = nullptr;
    void (*perspective)(ModelMatrix&, float, float, float, float) = nullptr;
    void (*lookAtTarget)(ModelMatrix&, const std::array<float, 3>&, const std::array<float, 3>&,
                         const std::array<float, 3>&) = nullptr;
};
enum class FovConversion {
    FloatMultiply,
    DoubleMultiply
};
struct ModelViewSettings {
    float aspect = 1, nearPlane = 1, farPlane = 100000;
    FovConversion fovConversion = FovConversion::FloatMultiply;
    // Editor navigation is explicit. Timeline mode uses SceneSession's camera.
    bool editorNavigation = false;
    std::array<float, 3> editorEye{};
    float editorFov = 45;
    std::uint8_t editorPerspective = 0;
    rhi::ModelViewport viewport;
};
struct ModelFrameInputs {
    // Only render-relevant SceneSession fields, never a copy of every bone.
    struct Scene {
        std::uint32_t frame = 0;
        format::PmmEvaluatedCamera camera;
        std::array<float, 6> lightState{};
    } scene;
    ModelViewSettings view;
    // Final CPU transforms supplied after legacy IK/physics/camera attachment.
    ModelMatrix world{}, lightRotation{};
    // Model pass follows the accessory light phase. Its -0.3/+0.3 arithmetic
    // is observable in fixed-function light ambient, even with no accessories.
    bool accessoryLightRoundTrip = true;
    // Explicit CPU frame-entry state: the legacy HUD leaves blending enabled
    // for the next ground pass. Device initialization resets it to disabled.
    bool groundAlphaBlend = false;
};
struct ModelMaterialInputs {
    std::array<float, 4> diffuse{}, ambient{}, specular{}, emissive{};
    float shininess = 0;
    bool pmd = true, doubleSided = false;
    std::filesystem::path base, sphere;
    unsigned sphereMode = 0;
};
struct ModelMaterialTextureSources {
    rhi::ToonRequest request;
    std::array<rhi::MaterialTextureAsset, 3> assets;
    std::filesystem::path base, sphere;
};
struct ModelPassSettings {
    bool alphaTest = true, stencil = true, anisotropic = true;
    unsigned maxAnisotropy = 16;
};
// No device, native handle, state query or resource borrowing in this producer.
bool BuildModelInputs(const ModelFrameInputs&, const ModelMaterialInputs&, const ModelPassSettings&,
                      rhi::ModelPipeline&, const ModelInputMath& = {});
struct ModelInputDifference {
    std::string field;
    unsigned component = 0;
    double generated = 0, reference = 0;
};
// Exact semantic comparison (never struct padding); inactive texture stages
// and disabled lights have no shader meaning and are excluded deliberately.
std::vector<ModelInputDifference> CompareModelInputs(const rhi::ModelPipeline& generated,
                                                     const rhi::ModelPipeline& reference);

// Ordered CPU material packets. Sequence is the original model draw order,
// not a material sort key. A selected subset is explicitly labelled by its
// material indices; it is never advertised as a complete model/frame.
struct ModelMaterialPacket {
    std::uint32_t material = 0, firstIndex = 0, indexCount = 0;
    rhi::ModelPipeline pipeline;
    std::array<rhi::MaterialTextureAsset, 3> textures;
    // Immutable CPU catalog ownership avoids copying decoded images/Mips for
    // every material. Existing value producers remain supported.
    std::array<std::shared_ptr<const rhi::MaterialTextureAsset>, 3> textureResources;
};
class ModelSubmissionBuilder {
public:
    // One owner per model. Texture identity + sampling color space gives a
    // stable ID, shared across material/stage bindings. Changed CPU bytes
    // receive increasing versions; failed builds leave the registry untouched.
    explicit ModelSubmissionBuilder(std::uint64_t firstTextureId = 0x8000000000000001ull)
        : nextId_(firstTextureId) {}
    rhi::ModelReplayResult Build(const ModelGeometrySnapshot&, const rhi::HistoryDesc&,
                                 rhi::ClearColor, const std::vector<ModelMaterialPacket>&,
                                 rhi::ModelReplayStream&);

private:
    struct Texture {
        std::string identity;
        rhi::ModelReplayResource resource;
        bool active = false;
        std::uint64_t preparationRevision = 0, sourceRevision = 0;
    };
    std::vector<Texture> textures_;
    std::uint64_t nextId_;
};
}
