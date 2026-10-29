#pragma once
#include "rhi_model_pipeline.hpp"
#include "rhi_render_pass.hpp"
#include <functional>
#include <string>

namespace mikudancestudio::rhi {
enum class ShaderStage : unsigned {
    Vertex = 1,
    Pixel = 2
};
enum class ShaderBindingKind {
    Constants,
    Texture,
    Sampler
};
enum class ShaderValueType {
    Float,
    Float2,
    Float3,
    Float4,
    Int,
    UInt,
    Matrix4x4
};
enum class ShaderVertexFormat {
    Float2,
    Float3,
    Float4,
    Rgba8
};
struct ShaderConstantMember {
    std::string name;
    ShaderValueType type = ShaderValueType::Float4;
    unsigned offset = 0;
    // Matrices are explicitly row-major float4 rows. Arrays use a 16-byte
    // stride (64 bytes for float4x4); no native C++ packing is inferred.
    unsigned count = 1;
};
struct ShaderBinding {
    ShaderBindingKind kind = ShaderBindingKind::Constants;
    unsigned slot = 0, stages = unsigned(ShaderStage::Pixel), bytes = 0;
    std::vector<ShaderConstantMember> members;
    bool srgb = false;
};
struct ShaderVertexInput {
    std::string semantic;
    unsigned semanticIndex = 0, location = 0, offset = 0;
    ShaderVertexFormat format = ShaderVertexFormat::Float3;
};
struct ShaderModule {
    ShaderStage stage = ShaderStage::Vertex;
    std::string entry = "main", hlsl;
    SnapshotBytes spirv;
};
struct ShaderProgramDesc {
    std::string name;
    ShaderModule vertex, pixel{ShaderStage::Pixel};
    std::vector<ShaderBinding> bindings;
    std::vector<ShaderVertexInput> inputs;
    // Number of float components written to each dense SV_TargetN output.
    std::vector<unsigned> colorOutputs{4};
};
// Immutable CPU description. Native program caches belong to each Device;
// replacement recompiles/recreates from this snapshot, never native pointers.
class ShaderProgram {
public:
    explicit ShaderProgram(ShaderProgramDesc);
    const ShaderProgramDesc description;
    const std::string key;
};
struct ShaderPipeline {
    std::shared_ptr<const ShaderProgram> program;
    VertexLayout vertexLayout = VertexLayout::PositionNormalUv;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    BatchRasterState raster;
    std::optional<ModelViewport> viewport;
    std::uint8_t colorWriteMask = 15;
    std::vector<TextureFormat> colorFormats{TextureFormat::Rgba8};
};
struct ShaderConstantBinding {
    unsigned slot = 0;
    SnapshotBytes bytes;
};
struct ShaderTextureBinding {
    unsigned slot = 0;
    Handle texture;
};
struct ShaderSamplerBinding {
    unsigned slot = 0;
    SamplerDesc sampler{SamplerFilter::Linear, SamplerFilter::Linear, SamplerFilter::Linear};
};
struct ShaderResourceBindings {
    std::vector<ShaderConstantBinding> constants;
    std::vector<ShaderTextureBinding> textures;
    std::vector<ShaderSamplerBinding> samplers;
};
struct ShaderResult {
    bool supported = false;
    std::string reason;
    explicit operator bool() const { return supported; }
};
struct ShaderTextureInfo {
    bool valid = false, initialized = false, sampleable = false, srgb = false;
    TextureFormat format = TextureFormat::Rgba8;
};
ShaderCapabilities GraphicsShaderProfile(bool hlsl, bool spirv);
ShaderResult ValidateShaderPipeline(const ShaderPipeline&, ShaderCapabilities);
ShaderResult ValidateShaderTargetSupport(const Device&, const ShaderPipeline&);
ShaderResult ValidateShaderAttachments(const ShaderPipeline&, const std::vector<TextureFormat>&);
std::uint64_t ShaderConstantBytes(const ShaderPipeline&, ShaderCapabilities);
ShaderResult ValidateShaderBindings(const ShaderPipeline&, const ShaderResourceBindings&,
                                    const std::function<ShaderTextureInfo(Handle)>&);
// Allocation-time query for the first graphics profile, with explicit reasons.
ShaderResult QueryShaderTextureSupport(const Device&, const RenderTextureDesc&);
std::vector<ShaderVertexInput> StandardShaderInputs(VertexLayout);
// Reuse existing native raster/depth/blend implementations during migration.
// Material arithmetic and bindings are not part of the generic shader program.
ModelPipeline ShaderRasterAdapter(const ShaderPipeline&);
}
