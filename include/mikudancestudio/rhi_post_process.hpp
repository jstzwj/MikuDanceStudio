#pragma once
#include "rhi_frame_passes.hpp"
#include <algorithm>
#include <cstring>
namespace mikudancestudio::rhi {
// Shared immutable geometry for built-in and programmable fullscreen passes.
inline bool AttachFullscreenTriangle(RenderFrame& frame, ModelReplayDraw& draw) {
    ModelReplayResource vb;
    vb.key = {UINT64_MAX, 1};
    const float vertices[][8] = {
        {-1, 1, 0, 0, 0, 0, 0, 0}, {3, 1, 0, 0, 0, 0, 2, 0}, {-1, -3, 0, 0, 0, 0, 0, 2}};
    vb.bytes.resize(sizeof(vertices));
    std::memcpy(vb.bytes.data(), vertices, sizeof(vertices));
    const auto shared = std::find_if(frame.resources.begin(), frame.resources.end(),
                                     [&](const auto& r) { return r.key.id == vb.key.id; });
    if (shared == frame.resources.end())
        frame.resources.push_back(vb);
    else if (shared->key.version != 1 || shared->kind != ReplayResourceKind::Vertices ||
             shared->vertexLayout != VertexLayout::PositionNormalUv || shared->bytes != vb.bytes)
        return false;
    draw.indexed = false;
    draw.vertices = vb.key;
    draw.drawVertexCount = 3;
    return true;
}
// Append a full-coverage pass and select its output. Existing output targets
// must have the requested description. A failed append is atomic.
inline bool AppendPostProcess(RenderFrame& frame, std::uint64_t input, std::uint64_t output,
                              unsigned width, unsigned height,
                              PostProcessState effect = {PostProcessMode::Copy}) {
    if (!input || !output || input == output || effect.mode == PostProcessMode::None)
        return false;
    const auto source = std::find_if(frame.targets.begin(), frame.targets.end(),
                                     [&](const auto& t) { return t.id == input; });
    if (source == frame.targets.end() || source->description.format != TextureFormat::Rgba8)
        return false;
    const auto destination = std::find_if(frame.targets.begin(), frame.targets.end(),
                                          [&](const auto& t) { return t.id == output; });
    const RenderTextureDesc description{width, height, TextureFormat::Rgba8,
                                        TextureAccess::ColorAttachment | TextureAccess::Sampled |
                                            TextureAccess::CopySource};
    if (destination != frame.targets.end() &&
        !EqualRenderTextureDesc(destination->description, description))
        return false;
    // Reserved immutable fullscreen geometry, shared by every pass. It stays
    // independent of changing model resource counts and capture modes.
    ModelReplayDraw draw;
    draw.targetTextures[0] = {input, source->description.width, source->description.height};
    auto& p = draw.pipeline;
    p.postProcess = effect;
    p.viewport = ModelViewport{0, 0, width, height, 0, 1};
    p.raster.cull = BatchCull::None;
    p.raster.depthTest = p.raster.depthWrite = p.raster.alphaBlend = false;
    constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    p.shader.world = p.shader.view = p.shader.projection = p.shader.normalView = identity;
    p.shader.textureTransforms[0] = identity;
    auto& stage = p.shader.layout.stages[0];
    stage.role = MaterialTextureRole::Base;
    stage.color = MaterialCombine::Modulate;
    stage.colorArg2 = MaterialColorSource::LitDiffuse;
    auto& sampler = p.shader.samplers[0];
    sampler.min = sampler.mag = effect.linear ? SamplerFilter::Linear : SamplerFilter::Point;
    sampler.mip = SamplerFilter::Point;
    sampler.maxAnisotropy = 1;
    sampler.u = sampler.v = SamplerAddress::Clamp;
    auto next = frame;
    if (!AttachFullscreenTriangle(next, draw))
        return false;
    FramePass pass;
    pass.name = "post_process";
    pass.color = FrameAttachment{output};
    pass.sampled = {input};
    pass.draws.push_back(draw);
    if (destination == frame.targets.end())
        next.targets.push_back({output, description});
    next.passes.push_back(std::move(pass));
    next.finalColor = output;
    if (!ValidateRenderFrame(next))
        return false;
    frame = std::move(next);
    return true;
}
}
