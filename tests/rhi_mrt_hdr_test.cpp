#include "rhi_test_platform.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_float.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string(#x));                                             \
    } while (false)
const char* source = R"(
Texture2D image0:register(t0); Texture2D image1:register(t1);
struct Vertex { float4 p:SV_POSITION; float2 uv:TEXCOORD0; };
Vertex vertex(float3 p:POSITION,float2 uv:TEXCOORD0) { Vertex o; o.p=float4(p,1); o.uv=uv; return o; }
struct Targets { float4 a:SV_Target0; float4 b:SV_Target1; };
Targets split(Vertex i) { float2 q=floor(i.uv*2); Targets o;
 o.a=float4(-1.5+q.x*4,.375+q.y*.5,8,.25);
 o.b=float4(.5,-2+q.y*3,q.x*.25,.5); return o; }
float4 sample0(float2 uv) { uint w,h; image0.GetDimensions(w,h);
 return image0.Load(int3(clamp(int2(uv*float2(w,h)),int2(0,0),int2(w,h)-1),0)); }
float4 combine(Vertex i):SV_Target { uint w,h; image1.GetDimensions(w,h);
 return sample0(i.uv)*.3+image1.Load(int3(clamp(int2(i.uv*float2(w,h)),int2(0,0),int2(w,h)-1),0))*.2; }
float4 copy(Vertex i):SV_Target { return sample0(i.uv); }
float4 tone(Vertex i):SV_Target { float4 c=sample0(i.uv); c.rgb=max(c.rgb,0); c.rgb=c.rgb/(1+c.rgb); return c; }
)";
ShaderPipeline Pipeline(const char* entry, unsigned textures, std::vector<TextureFormat> formats) {
    ShaderProgramDesc desc;
    desc.vertex.hlsl = desc.pixel.hlsl = source;
    desc.vertex.entry = "vertex";
    desc.pixel.entry = entry;
    desc.inputs = {{"POSITION", 0, 0, 0, ShaderVertexFormat::Float3},
                   {"TEXCOORD", 0, 1, 24, ShaderVertexFormat::Float2}};
    desc.colorOutputs.assign(formats.size(), 4);
    for (unsigned n = 0; n < textures; ++n)
        desc.bindings.push_back({ShaderBindingKind::Texture, n});
    ShaderPipeline p;
    p.program = std::make_shared<ShaderProgram>(std::move(desc));
    p.colorFormats = std::move(formats);
    p.raster.cull = BatchCull::None;
    p.raster.depthTest = p.raster.depthWrite = p.raster.alphaBlend = false;
    return p;
}
RenderFrame Frame(unsigned w, unsigned h) {
    RenderFrame frame;
    const auto access =
        TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
    for (unsigned id = 1; id <= 6; ++id)
        frame.targets.push_back(
            {id, {w, h, id < 5 ? TextureFormat::Rgba16Float : TextureFormat::Rgba8, access}});
    frame.finalColor = 6;
    ModelReplayResource geometry;
    geometry.key = {1, 1};
    const float vertices[][8] = {
        {-1, 1, 0, 0, 0, 1, 0, 0}, {3, 1, 0, 0, 0, 1, 2, 0}, {-1, -3, 0, 0, 0, 1, 0, 2}};
    geometry.bytes.resize(sizeof(vertices));
    std::memcpy(geometry.bytes.data(), vertices, sizeof(vertices));
    frame.resources = {geometry};
    const auto add = [&](const char* entry, std::vector<unsigned> targets,
                         std::vector<unsigned> inputs) {
        FramePass pass;
        pass.name = entry;
        std::vector<TextureFormat> formats;
        for (auto id : targets) {
            pass.colors.push_back(FrameAttachment{id});
            formats.push_back(frame.targets[id - 1].description.format);
        }
        ModelReplayDraw draw;
        draw.indexed = false;
        draw.vertices = geometry.key;
        draw.drawVertexCount = 3;
        draw.shader = ReplayShader{};
        draw.shader->pipeline = Pipeline(entry, unsigned(inputs.size()), std::move(formats));
        for (unsigned slot = 0; slot < inputs.size(); ++slot) {
            auto id = inputs[slot];
            pass.sampled.push_back(id);
            draw.shader->textures.push_back({slot, {}, {id, w, h}});
        }
        pass.draws = {draw};
        frame.passes.push_back(std::move(pass));
    };
    add("split", {1, 2}, {});
    add("combine", {3}, {1, 2});
    add("copy", {4}, {3});
    add("tone", {5}, {4});
    add("copy", {6}, {5});
    return frame;
}
float Component(const AttachmentReadback& r, unsigned x, unsigned y, unsigned c) {
    std::uint16_t value;
    std::memcpy(&value, r.bytes.data() + ((std::size_t(y) * r.description.width + x) * 4 + c) * 2,
                2);
    return HalfToFloat(value);
}
int main(int argc, char** argv) try {
    const std::string api = argc > 1 ? argv[1] : "vulkan";
    BackendOptions options;
    options.backend = api.find("dx11") == 0   ? Backend::D3D11
                      : api.find("dx12") == 0 ? Backend::D3D12
                                              : Backend::Vulkan;
    options.debugLayer = api.find("debug") != std::string::npos;
    ConfigureVulkanTest(options, api);
    auto native = CreateOffscreenBackendDevice(40, 24, options);
    if (!native && options.debugLayer &&
        (native.reason == "validation_layer_unavailable" ||
         native.reason == "validation_extensions_unavailable"))
        return 77;
    CHECK(native);
    if (!native.device->ShaderFeatures().hlsl)
        return 77;
    CHECK(native.device->ShaderFeatures().maxColorAttachments >= 2);
    const auto caps = native.device->FormatFeatures(TextureFormat::Rgba16Float);
    CHECK(caps.render && caps.sampled && caps.blend && caps.readback);
    // Reflection must reject a declaration hiding a compiled second output.
    auto wrong = Pipeline("split", 0, {TextureFormat::Rgba16Float});
    CHECK(!native.device->PrepareShaderPipeline(wrong));
    std::vector<std::uint8_t> original;
    for (unsigned replacement = 0; replacement < 2; ++replacement) {
        if (replacement) {
            native = CreateOffscreenBackendDevice(40, 24, options);
            CHECK(native);
        }
        SceneRenderer renderer(*native.device);
        PreparedModelFrame prepared;
        CompletionToken token;
        for (const auto size : {std::pair<unsigned, unsigned>{40, 24}, {18, 10}, {40, 24}}) {
            auto frame = Frame(size.first, size.second);
            CHECK(ValidateRenderFrame(frame));
            auto ready = renderer.Prepare(1, frame, prepared);
            if (!ready)
                throw std::runtime_error(ready.reason + ": " + native.device->ShaderDiagnostic());
            auto submitted = renderer.Submit(prepared, token);
            if (!submitted)
                throw std::runtime_error(submitted.reason);
            AttachmentReadback a, b, hdr, copy, final, toned;
            CHECK(renderer.ReadTarget(1, 1, a) && renderer.ReadTarget(1, 2, b));
            CHECK(renderer.ReadTarget(1, 3, hdr) && renderer.ReadTarget(1, 4, copy));
            CHECK(renderer.ReadTarget(1, 5, toned) && renderer.ReadTarget(1, 6, final));
            CHECK(hdr.bytes == copy.bytes && toned.bytes == final.bytes);
            CHECK(hdr.bytes.size() == std::size_t(size.first) * size.second * 8);
            for (unsigned y = 0; y < size.second; ++y)
                for (unsigned x = 0; x < size.first; ++x) {
                    const float qx = x >= size.first / 2 ? 1.f : 0.f,
                                qy = y >= size.second / 2 ? 1.f : 0.f;
                    const float va[] = {-1.5f + qx * 4, .375f + qy * .5f, 8, .25f},
                                vb[] = {.5f, -2 + qy * 3, qx * .25f, .5f};
                    for (unsigned c = 0; c < 4; ++c) {
                        if (Component(a, x, y, c) != va[c] || Component(b, x, y, c) != vb[c]) {
                            std::fprintf(
                                stderr,
                                "MRT %ux%u (%u,%u) channel %u: A %.9g vs %.9g, B %.9g vs %.9g\n",
                                size.first, size.second, x, y, c, Component(a, x, y, c), va[c],
                                Component(b, x, y, c), vb[c]);
                            CHECK(false);
                        }
                        const float expected = HalfToFloat(FloatToHalf(va[c] * .3f + vb[c] * .2f));
                        // Native attachment conversion may truncate; allow one
                        // binary16 ULP, never a fixed tolerance that grows blindly.
                        const float ulp =
                            std::ldexp(1.f, std::max(-24, std::ilogb(std::abs(expected)) - 10));
                        if (std::abs(Component(hdr, x, y, c) - expected) > ulp) {
                            std::fprintf(stderr,
                                         "HDR (%u,%u) channel %u: actual %.9g, expected %.9g\n", x,
                                         y, c, Component(hdr, x, y, c), expected);
                            CHECK(false);
                        }
                        const float positive = std::max(Component(hdr, x, y, c), 0.f);
                        const float value =
                            c == 3 ? Component(hdr, x, y, c) : positive / (1 + positive);
                        const int unorm = int(std::floor(std::clamp(value, 0.f, 1.f) * 255 + .5f));
                        CHECK(std::abs(int(final.bytes[(std::size_t(y) * size.first + x) * 4 + c]) -
                                       unorm) <= 1);
                    }
                }
            RgbaImage output;
            CHECK(native.device->ReadModelTarget(output) && output.pixels == final.bytes);
            for (unsigned n = 0; n < 8; ++n)
                CHECK(renderer.Submit(prepared, token));
            CHECK(renderer.ReadTarget(1, 6, final) && final.bytes == output.pixels);
            if (size.first == 40) {
                if (original.empty())
                    original = final.bytes;
                else
                    CHECK(original == final.bytes);
            }
        }
        // A format change must select a new PSO. Common additive blending
        // applies to both attachments, including signed half-float values.
        auto mixed = Frame(8, 6);
        mixed.targets.resize(2);
        mixed.targets[1].description.format = TextureFormat::Rgba8;
        mixed.passes.resize(1);
        mixed.finalColor = 2;
        auto& pass = mixed.passes.front();
        pass.colors[0].clear = {-.5f, 2, 1, .25f};
        pass.colors[1].clear = {.25f, .25f, .25f, .25f};
        auto& pipeline = pass.draws.front().shader->pipeline;
        pipeline.colorFormats[1] = TextureFormat::Rgba8;
        pipeline.raster.alphaBlend = true;
        pipeline.raster.sourceBlend = pipeline.raster.destinationBlend = BlendFactor::One;
        CHECK(renderer.Prepare(1, mixed, prepared));
        CHECK(renderer.Submit(prepared, token));
        AttachmentReadback half, unorm;
        CHECK(renderer.ReadTarget(1, 1, half) && renderer.ReadTarget(1, 2, unorm));
        CHECK(half.bytes.size() == 8 * 6 * 8 && unorm.bytes.size() == 8 * 6 * 4);
        for (unsigned y = 0; y < 6; ++y)
            for (unsigned x = 0; x < 8; ++x) {
                CHECK(Component(half, x, y, 0) == (x < 4 ? -2.f : 2.f));
                CHECK(Component(half, x, y, 1) == (y < 3 ? 2.375f : 2.875f));
                CHECK(Component(half, x, y, 2) == 9 && Component(half, x, y, 3) == .5f);
                // UNORM clamps the source before blending; half-float does not.
                const int expected[] = {191, y < 3 ? 64 : 255, x < 4 ? 64 : 128, 191};
                for (unsigned c = 0; c < 4; ++c)
                    CHECK(std::abs(int(unorm.bytes[(y * 8 + x) * 4 + c]) - expected[c]) <= 1);
            }
        CHECK(renderer.Detach());
    }
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (options.backend == Backend::Vulkan)
        CHECK(QueryVulkanStats(*native.device).validationErrors == 0);
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (options.backend == Backend::D3D12)
        CHECK(QueryD3D12Stats(*native.device).debugErrors == 0);
#endif
    std::printf(
        "PASS %s MRT/HDR: signed half-float outputs, sample/combine/tone, exact copies, repeat/resize/replacement\n",
        api.c_str());
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
