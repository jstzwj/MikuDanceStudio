#pragma once
#include "rhi_frame_passes.hpp"
#include <algorithm>
#include <cstring>
namespace mikudancestudio::rhi {
// Append a full-coverage pass and select its output. A failed append is atomic.
inline bool AppendPostProcess(RenderFrame& frame, std::uint64_t input, std::uint64_t output,
                              unsigned width, unsigned height,
                              PostProcessState effect = {PostProcessMode::Copy}) {
    if (!input || !output || input == output || effect.mode == PostProcessMode::None)
        return false;
    const auto source = std::find_if(frame.targets.begin(), frame.targets.end(),
                                     [&](const auto& t) { return t.id == input; });
    if (source == frame.targets.end() || source->description.format != TextureFormat::Rgba8)
        return false;
    for (const auto& t : frame.targets)
        if (t.id == output)
            return false;
    std::uint64_t resource = 1;
    for (const auto& r : frame.resources) {
        if (r.key.id == UINT64_MAX)
            return false;
        resource = std::max(resource, r.key.id + 1);
    }
    ModelReplayDraw draw;
    draw.indexed = false;
    draw.vertices = {resource, 1};
    draw.drawVertexCount = 3;
    draw.targetTextures[0] = {input, source->description.width, source->description.height};
    auto& p = draw.pipeline;
    p.postProcess = effect;
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
    const float vertices[][8] = {
        {-1, 1, 0, 0, 0, 0, 0, 0}, {3, 1, 0, 0, 0, 0, 2, 0}, {-1, -3, 0, 0, 0, 0, 0, 2}};
    ModelReplayResource vb;
    vb.key = draw.vertices;
    vb.bytes.resize(sizeof(vertices));
    std::memcpy(vb.bytes.data(), vertices, sizeof(vertices));
    FramePass pass;
    pass.name = "post_process";
    pass.color = FrameAttachment{output};
    pass.sampled = {input};
    pass.draws.push_back(draw);
    auto next = frame;
    next.targets.push_back(
        {output,
         {width, height, TextureFormat::Rgba8,
          TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource}});
    next.resources.push_back(std::move(vb));
    next.passes.push_back(std::move(pass));
    next.finalColor = output;
    if (!ValidateRenderFrame(next))
        return false;
    frame = std::move(next);
    return true;
}
}
