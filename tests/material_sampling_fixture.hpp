#pragma once
#include "mikudancestudio/rhi_frame_passes.hpp"
#include <cstring>
namespace sampling_fixture {
using namespace mikudancestudio::rhi;
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
struct Vertex {
    float x, y, z, nx, ny, nz, u, v;
};
inline RenderFrame Fixture(unsigned pattern, SamplerFilter filter, unsigned anisotropy,
                           SamplerAddress address, float scale, bool mipmaps, bool shadow,
                           bool perspective = false) {
    RenderFrame f;
    const auto access =
        TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
    f.targets = {
        {1, {96, 64, TextureFormat::Rgba8, access}},
        {2,
         {96, 64, TextureFormat::Depth24Stencil8, unsigned(TextureAccess::DepthStencilAttachment)}},
        {3, {32, 32, TextureFormat::R32Float, access}}};
    Vertex vertices[] = {{-.95f, .9f, .5f, 0, 0, -1, -.37f, -.13f},
                         {.91f, .87f, .5f, 0, 0, -1, scale, -.13f},
                         {-.79f, -.9f, .5f, 0, 0, -1, -.37f, scale * .23f},
                         {-.79f, -.9f, .5f, 0, 0, -1, -.37f, scale * .23f},
                         {.91f, .87f, .5f, 0, 0, -1, scale, -.13f},
                         {.85f, -.7f, .5f, 0, 0, -1, scale, scale * .23f}};
    if (perspective)
        for (auto& v : vertices) {
            const float w = 2 + v.x * 1.3f;
            v.x *= w;
            v.y *= w;
            v.z = w;
        }
    ModelReplayResource vb;
    vb.key = {1, 1};
    vb.bytes.resize(sizeof(vertices));
    std::memcpy(vb.bytes.data(), vertices, sizeof(vertices));
    f.resources.push_back(vb);
    ModelReplayResource texture;
    texture.key = {2, 1};
    texture.kind = ReplayResourceKind::Texture;
    unsigned level = 0;
    for (unsigned size = 128; size; size /= 2, ++level) {
        RgbaImage image{size, size, std::vector<std::uint8_t>(std::size_t(size) * size * 4)};
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x) {
                const auto n = (std::size_t(y) * size + x) * 4;
                image.pixels[n] =
                    pattern == 1 ? std::uint8_t(level * 31) : std::uint8_t((x * 37 + y * 13) & 255);
                image.pixels[n + 1] = pattern == 1 ? std::uint8_t(255 - level * 23)
                                                   : std::uint8_t((x * 3 + y * 71) & 255);
                image.pixels[n + 2] =
                    pattern == 1 ? std::uint8_t(level * 17) : std::uint8_t((x * 101 + y * 7) & 255);
                image.pixels[n + 3] = pattern == 2 ? std::uint8_t((x * 43 + y * 29) & 255) : 255;
            }
        texture.texture.levels.push_back(std::move(image));
        if (!mipmaps)
            break;
    }
    f.resources.push_back(std::move(texture));
    ModelReplayDraw draw;
    draw.indexed = false;
    draw.vertices = {1, 1};
    draw.textures[0] = {2, 1};
    draw.drawVertexCount = 6;
    auto& p = draw.pipeline;
    p.raster.cull = BatchCull::None;
    p.raster.alphaBlend = pattern == 2;
    p.shader.world = p.shader.view = p.shader.projection = p.shader.normalView =
        p.shader.textureTransforms[0] = identity;
    p.shader.nativeDiffuse = p.shader.diffuse = {1, 1, 1, 1};
    if (perspective) {
        p.shader.projection[10] = .5f;
        p.shader.projection[11] = 1;
        p.shader.projection[15] = 0;
    }
    auto& stage = p.shader.layout.stages[0];
    stage.role = MaterialTextureRole::Base;
    stage.color = MaterialCombine::Modulate;
    stage.colorArg2 = MaterialColorSource::LitDiffuse;
    p.shader.samplers[0] = {filter, filter, filter, address, address, anisotropy};
    p.shader.alphaTest = pattern == 2;
    p.shader.alphaCompare = DepthComparison::GreaterEqual;
    p.shader.alphaReference = 128;
    if (shadow) {
        p.shadow.mode = BuiltinShadowMode::Material;
        p.shadow.receive = false;
        p.shadow.lightViewProjection = p.shadow.worldViewProjection = p.shadow.rotation = identity;
        if (perspective)
            p.shadow.worldViewProjection = p.shader.projection;
        p.shadow.color = p.shadow.toonColor = {1, 1, 1, 1};
        p.shadow.lightDirection = {0, 0, 1, 0};
        p.shadow.specular = {0, 0, 0, 1};
        p.shadow.eye = {0, 0, -3, 1};
        draw.shadowTarget = 3;
    }
    FramePass clear;
    clear.color = FrameAttachment{3};
    clear.color->clear = {1, 1, 1, 1};
    FramePass main;
    main.color = FrameAttachment{1};
    main.color->clear = {1, 1, 1, 1};
    main.depthStencil = FrameAttachment{2};
    main.sampled = {3};
    main.draws = {draw};
    f.passes = {clear, main};
    return f;
}
}
