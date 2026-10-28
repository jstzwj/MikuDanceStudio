#pragma once
#include "mikudancestudio/rhi_assets.hpp"
#include <string>
#include <array>

namespace mikudancestudio::rhi {
enum class ToonKind {
    Default,
    Shared,
    ModelFile
};
enum class MissingToonPolicy {
    Unbound,
    Default
};
enum class TextureOrigin {
    Missing,
    ModelFile,
    SharedFile,
    Builtin
};
struct ToonRequest {
    ToonKind kind = ToonKind::Default;
    unsigned sharedSlot = 0; // 1..10, matching toon01.bmp .. toon10.bmp
    std::filesystem::path modelDirectory;
    std::filesystem::path path; // decoded by the model-format/platform boundary
    MissingToonPolicy missing = MissingToonPolicy::Unbound;
};
struct MaterialTextureAsset {
    std::string identity; // logical identity; revision is decoded content, not timestamp
    AtlasAsset content;
    // Optional experimental preparation, separate from the original decode.
    // It is never selected by the cache until the host accepts a parity gate.
    AtlasAsset prepared;
    bool usePrepared = false;
    TextureOrigin origin = TextureOrigin::Missing;
    std::filesystem::path sourcePath;
    bool fallback = false;
    TextureDesc::Mips mips = TextureDesc::Mips::SingleLevel;
    // Explicit prepared levels are separate from original decode and from
    // backend-owned GenerateLinear. SceneRenderer owns their GPU residency.
    std::optional<PreparedMipChain> mipChain;
};
bool PrepareMaterialMipChain(MaterialTextureAsset&, MipPreparationPolicy,
                             TexturePreparer* backend = nullptr, bool sceneActive = false);
// Base/Sphere share file identity and content even when used in different slots.
// An empty reference is Absent; a referenced but unavailable file is Missing.
enum class MaterialResourceStatus {
    Absent,
    Ready,
    Missing
};
MaterialResourceStatus LoadMaterialTexture(const std::filesystem::path& modelDirectory,
                                           const std::filesystem::path& reference,
                                           MaterialTextureAsset& asset);

enum class MaterialTextureRole {
    None,
    Toon,
    Base,
    Sphere
};
enum class MaterialCoordinates {
    MeshUv0,
    MeshUv1,
    CameraNormal
};
enum class MaterialCombine {
    Disabled,
    SelectTexture,
    Modulate,
    Add
};
enum class SphereOperation {
    Disabled,
    Multiply,
    Add,
    SubTexture
};
enum class MaterialColorSource {
    LitDiffuse,
    Previous,
    Texture
};
struct MaterialTextureInput {
    MaterialTextureRole role = MaterialTextureRole::None;
    MaterialCoordinates coordinates = MaterialCoordinates::MeshUv0;
    MaterialCombine color = MaterialCombine::Disabled;
    MaterialColorSource colorArg1 = MaterialColorSource::Texture;
    MaterialColorSource colorArg2 = MaterialColorSource::Previous;
    MaterialCombine alpha = MaterialCombine::Modulate;
    MaterialColorSource alphaArg1 = MaterialColorSource::Texture;
    MaterialColorSource alphaArg2 = MaterialColorSource::Previous;
};
struct MaterialTextureLayout {
    // Logical DX9-compatible cascade order, not hardware sampler registers.
    std::array<MaterialTextureInput, 3> stages{};
    SphereOperation sphere = SphereOperation::Disabled;
};
// PMD may store a lone .sph/.spa in the base reference. PMX may promote Sphere
// to the second cascade input when Base is absent. Unsupported modes fail closed.
bool BuildMaterialTextureLayout(bool pmd, const std::filesystem::path& base,
                                const std::filesystem::path& sphere, unsigned sphereMode,
                                MaterialTextureLayout& layout);
struct MaterialShaderInputs {
    // Left-handed world/view, row-major row-vector matrices. Projection uses
    // D3D clip depth [0,w]; backend conversion is explicit, never implicit.
    std::array<float, 16> world{}, view{}, projection{};
    // Inverse transpose of world*view's linear part, for row-vector normals.
    std::array<float, 16> normalView{};
    std::array<float, 4> nativeDiffuse{}, materialAmbient{}, materialSpecular{}, emissive{};
    struct DirectionalLight {
        bool enabled = false;
        std::array<float, 3> direction{};
        std::array<float, 4> diffuse{}, ambient{}, specular{};
    };
    std::array<DirectionalLight, 8> lights{};
    std::array<float, 4> globalAmbient{};
    bool normalizeNormals = false, specularEnabled = false, localViewer = false;
    bool colorVertex = false;
    // Material/vertex source selections: Material, Color1, Color2.
    std::array<std::uint8_t, 4> materialSources{};
    MaterialTextureLayout layout;
    std::array<SamplerDesc, 3> samplers{};
    // Row-major matrices, row-vector multiplication, before API clip conversion.
    std::array<std::array<float, 16>, 3> textureTransforms{};
    std::array<float, 4> diffuse{};
    std::array<float, 3> ambient{}, specular{};
    float shininess = 0;
    bool lighting = false;
    bool srgbSample[3]{};
    bool alphaTest = false;
    DepthComparison alphaCompare = DepthComparison::Always;
    std::uint8_t alphaReference = 0;
    // Color combines in cascade order; sampled alpha multiplies current alpha.
    // This is an explicit fixed-function compatibility subset. Separate alpha
    // blending, stencil/fog/shadow passes and MME need additional pipeline/pass
    // contracts before a complete shader backend can replace the old renderer.
};
bool ValidateMaterialShaderInputs(const MaterialShaderInputs& inputs);
bool BuildShaderNormalMatrix(const std::array<float, 16>& world, const std::array<float, 16>& view,
                             std::array<float, 16>& normal);
// Sampling/lighting are separate producers. This function describes the
// logical cascade on already sampled RGBA (linear UNORM, no hidden gamma).
bool EvaluateMaterialTextureCascade(const MaterialTextureLayout& layout,
                                    const std::array<float, 4>& litDiffuse,
                                    const std::array<std::array<float, 4>, 3>& samples,
                                    std::array<float, 4>& output);
bool MaterialAlphaTest(float alpha, bool enabled, DepthComparison comparison,
                       std::uint8_t reference);
std::array<float, 4> TransformShaderVector(const std::array<float, 4>& vector,
                                           const std::array<float, 16>& matrix);
// Shared files take precedence over builtin bytes. Default slot uses builtin
// bytes only. Missing PMD private Toon stays unbound; PMX can request default.
class ToonTextureResolver {
public:
    ToonTextureResolver(std::filesystem::path sharedDirectory, const AssetSource& builtin);
    bool Load(const ToonRequest& request, MaterialTextureAsset& asset) const;

private:
    std::filesystem::path sharedDirectory_;
    const AssetSource& builtin_;
};
class MaterialTextureCache {
public:
    explicit MaterialTextureCache(Device& device) : device_(&device) {}
    ~MaterialTextureCache() { Invalidate(); }
    MaterialTextureCache(const MaterialTextureCache&) = delete;
    MaterialTextureCache& operator=(const MaterialTextureCache&) = delete;
    Handle Get(const MaterialTextureAsset& asset);
    // Call while the old device is alive; caller retains decoded CPU assets.
    bool Rebind(Device& device);
    void Invalidate();
    std::uint64_t Uploads() const { return uploads_; }
    std::uint64_t Hits() const { return hits_; }

private:
    struct Entry {
        std::string identity;
        Handle texture;
        std::uint64_t revision;
        DeviceStatus uploaded{};
        std::uint32_t width, height;
        TextureDesc::Mips mips;
    };
    Device* device_;
    std::vector<Entry> entries_;
    std::uint64_t uploads_ = 0, hits_ = 0;
};
}
