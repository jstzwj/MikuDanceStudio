#include "rhi_test_platform.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::to_string(__LINE__) + ": " #x);                          \
    } while (false)
namespace {
RenderFrame MakeFrame(unsigned width, unsigned height) {
    const auto access =
        TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
    RenderTextureDesc chain{width, height, TextureFormat::Rgba8, access};
    for (auto size = std::max(width, height); size > 1; size >>= 1)
        ++chain.mips;
    RenderFrame frame;
    frame.targets = {{1, chain}, {2, {width, height, TextureFormat::Rgba8, access}}};
    frame.finalColor = 2;
    ModelReplayResource geometry;
    geometry.key = {1, 1};
    const float vertices[][8] = {
        {-1, 1, 0, 0, 0, 1, 0, 0}, {3, 1, 0, 0, 0, 1, 2, 0}, {-1, -3, 0, 0, 0, 1, 0, 2}};
    geometry.bytes.resize(sizeof(vertices));
    std::memcpy(geometry.bytes.data(), vertices, sizeof(vertices));
    frame.resources = {geometry};
    ShaderProgramDesc desc;
    desc.vertex.hlsl = desc.pixel.hlsl = R"(
Texture2D image:register(t0); SamplerState pointClamp:register(s0);
struct V { float4 p:SV_Position; float2 uv:TEXCOORD0; };
V VS(float3 p:POSITION, float2 uv:TEXCOORD0) { V v; v.p=float4(p,1); v.uv=uv; return v; }
float4 pattern(V v):SV_Target {
    return float4(uint(v.p.x)&1, uint(v.p.y)&1, v.uv.x<.5 ? 64.0/255 : 192.0/255, 160.0/255);
}
float4 sampleMip(V v):SV_Target { return image.SampleLevel(pointClamp,v.uv,2); }
)";
    desc.vertex.entry = "VS";
    desc.pixel.entry = "pattern";
    desc.inputs = {{"POSITION", 0, 0, 0, ShaderVertexFormat::Float3},
                   {"TEXCOORD", 0, 1, 24, ShaderVertexFormat::Float2}};
    ModelReplayDraw draw;
    draw.vertices = geometry.key;
    draw.indexed = false;
    draw.drawVertexCount = 3;
    draw.shader = ReplayShader{};
    draw.shader->pipeline.program = std::make_shared<const ShaderProgram>(desc);
    auto& raster = draw.shader->pipeline.raster;
    raster.depthTest = raster.depthWrite = raster.alphaBlend = false;
    raster.cull = BatchCull::None;
    FramePass write;
    write.color = FrameAttachment{1};
    write.draws = {draw};
    FramePass mips;
    mips.generateMips = 1;
    FramePass read;
    read.color = FrameAttachment{2};
    read.sampled = {1};
    desc.pixel.entry = "sampleMip";
    desc.bindings = {{ShaderBindingKind::Texture, 0}, {ShaderBindingKind::Sampler, 0}};
    draw.shader->pipeline.program = std::make_shared<const ShaderProgram>(desc);
    draw.shader->textures = {{0, {}, {1, width, height}}};
    draw.shader->samplers = {{0,
                              {SamplerFilter::Point, SamplerFilter::Point, SamplerFilter::Point,
                               SamplerAddress::Clamp, SamplerAddress::Clamp, 1}}};
    read.draws = {draw};
    frame.passes = {write, mips, read};
    return frame;
}
void Contract() {
    auto frame = MakeFrame(32, 16);
    CHECK(ValidateRenderFrame(frame));
    auto bad = frame;
    bad.passes.erase(bad.passes.begin() + 1);
    CHECK(!ValidateRenderFrame(bad));
    bad = frame;
    std::swap(bad.passes[0], bad.passes[1]);
    CHECK(!ValidateRenderFrame(bad));
    bad = frame;
    bad.passes.insert(bad.passes.begin() + 2, bad.passes[0]);
    CHECK(!ValidateRenderFrame(bad)); // Writing level zero invalidates the old chain.
    bad = frame;
    bad.passes[1].draws = bad.passes[0].draws;
    CHECK(!ValidateRenderFrame(bad));
    auto device = CreateNullDevice();
    auto source = device->CreateRenderTexture(frame.targets[0].description);
    auto output = device->CreateRenderTexture(frame.targets[1].description);
    CHECK(source && output && !device->GenerateRenderMips(source));
    CHECK(device->BeginFrame({}) && !device->GenerateRenderMips(source));
    RenderPassDesc write;
    write.color = PassAttachment{source};
    RenderPassDesc read;
    read.color = PassAttachment{output};
    read.sampled = {source};
    CHECK(device->BeginRenderPass(write) && !device->GenerateRenderMips(source) &&
          device->EndRenderPass());
    CHECK(!device->BeginRenderPass(read));
    CHECK(device->GenerateRenderMips(source) && device->BeginRenderPass(read) &&
          device->EndRenderPass());
    CHECK(device->BeginRenderPass(write) && device->EndRenderPass() &&
          !device->BeginRenderPass(read));
    CHECK(device->GenerateRenderMips(source) && device->EndFrame() && device->Present());
    CHECK(device->Destroy(source) && device->Destroy(output));
    CHECK(device->BeginFrame({}) && !device->GenerateRenderMips(source) && device->EndFrame() &&
          device->Present());
}
}
int main(int argc, char** argv) try {
    Contract();
    const std::string api = argc > 1 ? argv[1] : "null";
    if (api == "null") {
        std::puts("PASS explicit mip ordering, initialization, invalidation and stale handles");
        return 0;
    }
    BackendOptions options;
    options.backend = api.find("dx11") == 0   ? Backend::D3D11
                      : api.find("dx12") == 0 ? Backend::D3D12
                                              : Backend::Vulkan;
    options.debugLayer = api.find("debug") != std::string::npos;
    ConfigureVulkanTest(options, api);
    auto native = CreateOffscreenBackendDevice(32, 16, options);
    if (!native && options.debugLayer &&
        (native.reason == "validation_layer_unavailable" ||
         native.reason == "validation_extensions_unavailable"))
        return 77;
    CHECK(native);
    if (!native.device->ShaderFeatures().hlsl)
        return 77;
    SceneRenderer renderer(*native.device);
    for (unsigned width : {32u, 48u, 32u}) {
        auto frame = MakeFrame(width, 16);
        for (unsigned repeat = 0; repeat < 4; ++repeat) {
            if (repeat & 1) {
                frame.passes[0].draws.clear();
                frame.passes[0].color->clear = {1, 0, 64.f / 255, 128.f / 255};
            } else
                frame = MakeFrame(width, 16);
            PreparedModelFrame prepared;
            CompletionToken token;
            const auto ready = renderer.Prepare(1, frame, prepared);
            if (!ready)
                throw std::runtime_error(ready.reason);
            const auto submitted = renderer.Submit(prepared, token);
            if (!submitted)
                throw std::runtime_error(submitted.reason);
            AttachmentReadback output;
            CHECK(renderer.ReadTarget(1, 2, output));
            for (unsigned y = 0; y < 16; ++y)
                for (unsigned x = 0; x < width; ++x) {
                    const unsigned expected[] = {repeat & 1 ? 255u : 128u, repeat & 1 ? 0u : 128u,
                                                 repeat & 1      ? 64u
                                                 : x < width / 2 ? 64u
                                                                 : 192u,
                                                 repeat & 1 ? 128u : 160u};
                    for (unsigned c = 0; c < 4; ++c)
                        CHECK(std::abs(int(output.bytes[(y * width + x) * 4 + c]) -
                                       int(expected[c])) <= 1);
                }
        }
    }
    auto replacement = CreateOffscreenBackendDevice(32, 16, options);
    CHECK(replacement && renderer.Rebind(*replacement.device));
    PreparedModelFrame prepared;
    CompletionToken token;
    CHECK(renderer.Prepare(1, MakeFrame(32, 16), prepared) && renderer.Submit(prepared, token));
    CHECK(renderer.Detach());
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (options.backend == Backend::D3D12)
        CHECK(QueryD3D12Stats(*native.device).debugErrors == 0 &&
              QueryD3D12Stats(*replacement.device).debugErrors == 0);
#endif
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (options.backend == Backend::Vulkan)
        CHECK(QueryVulkanStats(*native.device).validationErrors == 0 &&
              QueryVulkanStats(*replacement.device).validationErrors == 0);
#endif
    std::printf("PASS %s mip-level sampling, alpha, rewriting, repeat, resize and replacement\n",
                api.c_str());
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
