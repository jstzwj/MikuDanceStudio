#pragma once
#include "rhi_material_textures.hpp"
#include <optional>
#include <cmath>
namespace mikudancestudio::rhi {
// Arithmetic policy is independent of API selection. Unsupported policies
// must fail explicitly rather than silently changing the image contract.
enum class ModelCompatibility {
    Default,
    Dx9Measured
};
enum class ModelCompatibilityResult {
    Supported,
    ShaderDoublesRequired,
    InvalidMode
};
constexpr ModelCompatibilityResult CheckModelCompatibility(ModelCompatibility mode,
                                                           bool shaderDoubles) {
    return mode == ModelCompatibility::Default       ? ModelCompatibilityResult::Supported
           : mode != ModelCompatibility::Dx9Measured ? ModelCompatibilityResult::InvalidMode
           : shaderDoubles                           ? ModelCompatibilityResult::Supported
                           : ModelCompatibilityResult::ShaderDoublesRequired;
}
struct ModelOptions {
    ModelCompatibility compatibility = ModelCompatibility::Default;
    // Diagnostic only: truncate Alpha to 12 fractional bits before its byte
    // comparison. Never quantize the output color; requires Dx9Measured.
    // This candidate is not a portable or universal DX9 conversion guarantee.
    bool diagnosticAlpha12 = false;
    // Diagnostic only: camera-normal texture coordinates use ordered fused
    // multiply-add steps, each rounded to float32. Requires Dx9Measured;
    // mesh UVs, scene inputs and the default shading contract are unchanged.
    bool diagnosticTextureFma = false;
    // Diagnostic only: prepared float32 world-view matrix, ordered fused
    // camera dot products and local-viewer half-vector formation.
    // Requires Dx9Measured; fixed-viewer shading and scene inputs are unchanged.
    bool diagnosticSpecularFma = false;
};
struct ModelSupport {
    bool shaderDoubles = false;
    ModelCompatibility selected = ModelCompatibility::Default;
    bool diagnosticAlpha12 = false;
    bool diagnosticTextureFma = false;
    ModelCompatibilityResult requestedSupport = ModelCompatibilityResult::InvalidMode;
    bool diagnosticSpecularFma = false;
};
// Top-left origin in target pixels, integer coverage, clip Z [0,w] mapped
// linearly to [minDepth,maxDepth]. Reversed, empty and out-of-target views fail.
struct ModelViewport {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
    float minDepth = 0, maxDepth = 1;
};
inline bool ValidModelViewport(const ModelViewport& v, std::uint32_t width, std::uint32_t height) {
    return v.width && v.height && v.x <= width && v.y <= height && v.width <= width - v.x &&
           v.height <= height - v.y && std::isfinite(v.minDepth) && std::isfinite(v.maxDepth) &&
           v.minDepth >= 0 && v.maxDepth <= 1 && v.minDepth <= v.maxDepth;
}
inline bool ContainsModelViewport(const ModelViewport& outer, const ModelViewport& inner) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x - outer.x <= outer.width &&
           inner.y - outer.y <= outer.height && inner.width <= outer.width - (inner.x - outer.x) &&
           inner.height <= outer.height - (inner.y - outer.y);
}
// Portable single-batch contract. Color/alpha blending, depth, stencil and color
// writes are explicit; fog, clipping planes, bias, MME and history are absent.
enum class ModelStencilOp {
    Keep = 1,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap
};
enum class ModelBlendOp {
    Add = 1,
    Subtract,
    ReverseSubtract,
    Min,
    Max
};
enum class BuiltinShadowMode {
    None,
    Depth,
    Material,
    Outline
};
struct BuiltinShadowState {
    BuiltinShadowMode mode = BuiltinShadowMode::None;
    // Matrices include the object's world transform. No backend rebuilds them.
    std::array<float, 16> lightViewProjection{}, worldViewProjection{}, rotation{};
    std::array<float, 4> color{}, toonColor{1, 1, 1, 1}, specular{}, lightDirection{}, eye{};
    bool perspective = false, transparent = false, receive = true;
};
inline bool ValidBuiltinShadowState(const BuiltinShadowState& s) {
    if (unsigned(s.mode) > unsigned(BuiltinShadowMode::Outline))
        return false;
    if (s.mode == BuiltinShadowMode::None)
        return true;
    for (const auto* a : {&s.lightViewProjection, &s.worldViewProjection, &s.rotation})
        for (float v : *a)
            if (!std::isfinite(v))
                return false;
    for (const auto* a : {&s.color, &s.toonColor, &s.specular, &s.lightDirection, &s.eye})
        for (float v : *a)
            if (!std::isfinite(v))
                return false;
    return s.specular[3] >= 0;
}
inline VertexLayout BuiltinShadowVertexLayout(BuiltinShadowMode mode) {
    return mode == BuiltinShadowMode::Outline ? VertexLayout::PositionColor
                                              : VertexLayout::PositionNormalUv;
}
enum class PostProcessMode {
    None,
    Copy,
    ColorTransform,
    LegacyScreenCapture
};
struct PostProcessState {
    PostProcessMode mode = PostProcessMode::None;
    std::array<float, 4> multiply{1, 1, 1, 1}, add{};
    // Normalized source rectangle, scale XY then bias XY; top-left origin.
    std::array<float, 4> uvRect{1, 1, 0, 0};
    bool linear = false;
};
struct ModelPipeline {
    // PositionColor: float3 + RGBA8 (16 bytes), no normal or UV attributes.
    // PositionNormalUv: float3 + float3 + float2 (32 bytes).
    VertexLayout vertexLayout = VertexLayout::PositionNormalUv;
    MaterialShaderInputs shader;
    BatchRasterState raster;
    // Absent means full current target, depth [0,1]. Never inherit prior draw.
    std::optional<ModelViewport> viewport;
    bool separateAlpha = false;
    BlendFactor sourceAlpha = BlendFactor::One, destinationAlpha = BlendFactor::Zero;
    ModelBlendOp colorOperation = ModelBlendOp::Add, alphaOperation = ModelBlendOp::Add;
    bool stencil = false;
    std::uint8_t stencilReadMask = 255, stencilWriteMask = 255, colorWriteMask = 15;
    std::uint32_t stencilReference = 0;
    DepthComparison stencilCompare = DepthComparison::Always;
    ModelStencilOp stencilFail = ModelStencilOp::Keep, stencilDepthFail = ModelStencilOp::Keep,
                   stencilPass = ModelStencilOp::Keep;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    BuiltinShadowState shadow;
    PostProcessState postProcess;
};
inline bool ValidPostProcess(const ModelPipeline& p) {
    const auto& s = p.postProcess;
    if (s.mode == PostProcessMode::None)
        return true;
    if (s.mode != PostProcessMode::Copy && s.mode != PostProcessMode::ColorTransform &&
        s.mode != PostProcessMode::LegacyScreenCapture)
        return false;
    if (s.mode == PostProcessMode::LegacyScreenCapture &&
        (!s.linear || !p.viewport || p.viewport->width < 2 || p.viewport->height < 2))
        return false;
    for (const auto* a : {&s.multiply, &s.add, &s.uvRect})
        for (float v : *a)
            if (!std::isfinite(v))
                return false;
    if (s.uvRect[0] <= 0 || s.uvRect[1] <= 0 || s.uvRect[2] < 0 || s.uvRect[3] < 0 ||
        s.uvRect[0] + s.uvRect[2] > 1.000001f || s.uvRect[1] + s.uvRect[3] > 1.000001f)
        return false;
    return p.vertexLayout == VertexLayout::PositionNormalUv &&
           p.topology == PrimitiveTopology::TriangleList &&
           p.shadow.mode == BuiltinShadowMode::None && !p.raster.depthTest &&
           !p.raster.depthWrite && !p.raster.alphaBlend && !p.stencil && p.colorWriteMask == 15 &&
           p.shader.layout.stages[0].role == MaterialTextureRole::Base &&
           p.shader.layout.stages[1].role == MaterialTextureRole::None &&
           p.shader.layout.stages[2].role == MaterialTextureRole::None && !p.shader.srgbSample[0];
}
inline unsigned ModelPrimitiveArity(PrimitiveTopology topology) {
    return topology == PrimitiveTopology::TriangleList ? 3
           : topology == PrimitiveTopology::LineList   ? 2
                                                       : 0;
}
inline unsigned ModelVertexStride(VertexLayout layout) {
    return layout == VertexLayout::PositionNormalUv ? 32
           : layout == VertexLayout::PositionColor  ? 16
                                                    : 0;
}
struct ModelTextureData {
    std::vector<RgbaImage> levels;
    bool srgb = false, generateMips = false;
};
}
