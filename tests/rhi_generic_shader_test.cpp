#include "rhi_test_platform.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string(#x) + ": " + native.device->ShaderDiagnostic());  \
    } while (0)
int main(int argc, char** argv) try {
    BackendOptions options;
    const std::string api = argc > 1 ? argv[1] : "vulkan";
    options.backend = api.find("dx11") == 0   ? Backend::D3D11
                      : api.find("dx12") == 0 ? Backend::D3D12
                                              : Backend::Vulkan;
    options.debugLayer = api.find("debug") != std::string::npos;
    ConfigureVulkanTest(options, api);
    auto native = CreateOffscreenBackendDevice(48, 32, options);
    if (!native && options.debugLayer &&
        (native.reason == "validation_layer_unavailable" ||
         native.reason == "validation_extensions_unavailable"))
        return 77;
    if (!native)
        throw std::runtime_error(native.reason);
    if (!native.device->ShaderFeatures().hlsl)
        return 77;
    ShaderProgramDesc desc;
    desc.vertex.hlsl = desc.pixel.hlsl = R"(
cbuffer Parameters : register(b0) { row_major float4x4 transform; float4 tint; };
Texture2D image : register(t15); SamplerState imageSampler : register(s15);
struct Output { float4 p:SV_POSITION; float2 uv:TEXCOORD0; };
Output vertex(float3 p:POSITION, float2 uv:TEXCOORD0) {
 Output o; o.p=mul(float4(p,1),transform); o.uv=uv; return o;
}
float4 pixel(Output o):SV_TARGET { return image.SampleLevel(imageSampler,o.uv,0)*tint; }
)";
    desc.vertex.entry = "vertex";
    desc.pixel.entry = "pixel";
    desc.inputs = {{"POSITION", 0, 0, 0, ShaderVertexFormat::Float3},
                   {"TEXCOORD", 0, 1, 24, ShaderVertexFormat::Float2}};
    desc.bindings = {
        {ShaderBindingKind::Constants,
         0,
         3,
         80,
         {{"transform", ShaderValueType::Matrix4x4, 0}, {"tint", ShaderValueType::Float4, 64}}},
        {ShaderBindingKind::Texture, 15},
        {ShaderBindingKind::Sampler, 15}};
    ShaderPipeline pipeline;
    pipeline.program = std::make_shared<const ShaderProgram>(desc);
    pipeline.raster.depthTest = pipeline.raster.depthWrite = pipeline.raster.alphaBlend = false;
    pipeline.raster.cull = BatchCull::None;
    CHECK(native.device->PrepareShaderPipeline(pipeline));
    auto wrong = desc;
    wrong.bindings[0].members[0].type = ShaderValueType::Float4;
    auto invalid = pipeline;
    invalid.program = std::make_shared<const ShaderProgram>(wrong);
    CHECK(!native.device->PrepareShaderPipeline(invalid));
    wrong = desc;
    auto& pixelSource = wrong.pixel.hlsl;
    const auto varying = pixelSource.find("float2 uv:TEXCOORD0");
    pixelSource.replace(varying, std::strlen("float2 uv:TEXCOORD0"), "float3 uv:TEXCOORD0");
    const auto assignment = pixelSource.find("o.uv=uv");
    pixelSource.replace(assignment, std::strlen("o.uv=uv"), "o.uv=float3(uv,0)");
    const auto function = pixelSource.find("float4 pixel");
    pixelSource.erase(function);
    pixelSource +=
        "float4 pixel(Output o):SV_TARGET { return image.SampleLevel(imageSampler,o.uv.xy,0)*tint*(1+o.uv.z); }";
    invalid.program = std::make_shared<const ShaderProgram>(wrong);
    CHECK(!native.device->PrepareShaderPipeline(invalid));
    CHECK(std::string(native.device->ShaderDiagnostic()) == "shader_stage_interface_mismatch");
    wrong = desc;
    for (auto* module : {&wrong.vertex, &wrong.pixel}) {
        const auto at = module->hlsl.find("row_major");
        module->hlsl.replace(at, 9, "column_major");
    }
    invalid.program = std::make_shared<const ShaderProgram>(wrong);
    CHECK(!native.device->PrepareShaderPipeline(invalid));
    wrong = desc;
    wrong.pixel.hlsl += "\n invalid shader";
    invalid.program = std::make_shared<const ShaderProgram>(wrong);
    CHECK(!native.device->PrepareShaderPipeline(invalid));
    wrong = desc;
    wrong.pixel.hlsl = R"(
struct Output { float4 p:SV_POSITION; float2 uv:TEXCOORD0; };
float4 pixel(Output o):SV_TARGET { double value=double(o.uv.x);return float(value*value); }
)";
    invalid.program = std::make_shared<const ShaderProgram>(wrong);
    CHECK(!native.device->PrepareShaderPipeline(invalid));
    RenderFrame frame;
    frame.targets = {{1,
                      {48, 32, TextureFormat::Rgba8,
                       unsigned(TextureAccess::ColorAttachment) | unsigned(TextureAccess::Sampled) |
                           unsigned(TextureAccess::CopySource)}}};
    frame.finalColor = 1;
    ModelReplayResource geometry;
    geometry.key = {1, 1};
    const float vertices[][8] = {
        {-1, 1, 0, 0, 0, 1, 0, 0}, {3, 1, 0, 0, 0, 1, 2, 0}, {-1, -3, 0, 0, 0, 1, 0, 2}};
    geometry.bytes.resize(sizeof(vertices));
    std::memcpy(geometry.bytes.data(), vertices, sizeof(vertices));
    ModelReplayResource texture;
    texture.key = {2, 1};
    texture.kind = ReplayResourceKind::Texture;
    texture.texture.levels.push_back(
        {2, 2, {255, 0, 0, 64, 0, 255, 0, 128, 0, 0, 255, 192, 255, 255, 255, 254}});
    frame.resources = {geometry, texture};
    ModelReplayDraw draw;
    draw.indexed = false;
    draw.vertices = geometry.key;
    draw.drawVertexCount = 3;
    draw.shader = ReplayShader{};
    draw.shader->pipeline = pipeline;
    float constants[20] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, .25f, 0, 0, 1, 1, 1, 1, .5f};
    draw.shader->constants.push_back({0});
    draw.shader->constants.back().bytes.resize(sizeof(constants));
    std::memcpy(draw.shader->constants.back().bytes.data(), constants, sizeof(constants));
    draw.shader->textures.push_back({15, texture.key});
    draw.shader->samplers.push_back(
        {15, {SamplerFilter::Point, SamplerFilter::Point, SamplerFilter::Point}});
    FramePass pass;
    pass.color = FrameAttachment{1};
    pass.color->clear = {0, 0, 0, 0};
    pass.draws = {draw};
    frame.passes = {pass};
    SceneRenderer renderer(*native.device);
    PreparedModelFrame prepared;
    CompletionToken token;
    for (unsigned n = 0; n < 6; ++n) {
        const auto ready = renderer.Prepare(1, frame, prepared);
        if (!ready)
            throw std::runtime_error(ready.reason);
        CHECK(renderer.Submit(prepared, token));
        AttachmentReadback output;
        CHECK(renderer.ReadTarget(1, 1, output));
        const auto pixel = [&](unsigned x, unsigned y, unsigned c) {
            return output.bytes[(y * 48 + x) * 4 + c];
        };
        CHECK(pixel(1, 1, 0) == 0 && pixel(1, 1, 3) == 0); // translation is in the final row
        CHECK(pixel(9, 4, 0) == 255 && pixel(9, 4, 3) == 32);
        CHECK(pixel(40, 4, 1) == 255 && pixel(40, 4, 3) == 64);
        CHECK(pixel(9, 28, 2) == 255 && pixel(9, 28, 3) == 96);
        CHECK(pixel(40, 28, 0) == 255 && pixel(40, 28, 3) == 127);
    }
    if (native.device->ShaderFeatures().maxFrameConstantBytes) {
        auto wide = desc;
        for (auto* module : {&wide.vertex, &wide.pixel}) {
            const auto at = module->hlsl.find("float4 tint;");
            module->hlsl.insert(at + std::strlen("float4 tint;"), " float4 reserved[4091];");
        }
        wide.bindings[0].bytes = 65536;
        auto tooLarge = frame;
        auto largeDraw = draw;
        largeDraw.shader->pipeline.program = std::make_shared<const ShaderProgram>(wide);
        largeDraw.shader->constants[0].bytes.resize(65536);
        largeDraw.shader->constants[0].bytes.Seal();
        tooLarge.passes[0].draws.assign(40, largeDraw);
        const auto uploads = renderer.Stats().uploads;
        const auto rejected = renderer.Prepare(1, tooLarge, prepared);
        CHECK(!rejected && rejected.reason == "shader_frame_constant_budget_exceeded");
        CHECK(renderer.Stats().uploads == uploads);
    }
    // Exercise all sixteen live descriptors together with distinct contents.
    auto all = desc;
    std::string declarations, expression = "image.SampleLevel(imageSampler,o.uv,0)";
    for (unsigned slot = 0; slot < 15; ++slot) {
        const auto n = std::to_string(slot);
        declarations += "Texture2D image" + n + ":register(t" + n + ");SamplerState sampler" + n +
                        ":register(s" + n + ");\n";
        expression += "+image" + n + ".SampleLevel(sampler" + n + ",o.uv,0)";
        all.bindings.push_back({ShaderBindingKind::Texture, slot});
        all.bindings.push_back({ShaderBindingKind::Sampler, slot});
    }
    for (auto* module : {&all.vertex, &all.pixel}) {
        module->hlsl.insert(0, declarations);
        module->hlsl.erase(module->hlsl.find("float4 pixel"));
        module->hlsl += "float4 pixel(Output o):SV_TARGET{return (" + expression + ")*(tint/16);}";
    }
    auto sixteen = frame;
    auto& combined = *sixteen.passes[0].draws[0].shader;
    combined.pipeline.program = std::make_shared<const ShaderProgram>(all);
    combined.textures.clear();
    combined.samplers.clear();
    for (unsigned slot = 0; slot < 16; ++slot) {
        auto resource = texture;
        resource.key = {100 + slot, 1};
        resource.texture.levels = {{1, 1, {std::uint8_t(slot * 2), 0, 0, 254}}};
        sixteen.resources.push_back(resource);
        combined.textures.push_back({slot, resource.key});
        combined.samplers.push_back(
            {slot, {SamplerFilter::Point, SamplerFilter::Point, SamplerFilter::Point}});
    }
    CHECK(renderer.Prepare(1, sixteen, prepared) && renderer.Submit(prepared, token));
    AttachmentReadback summed;
    CHECK(renderer.ReadTarget(1, 1, summed));
    const auto center = (16 * 48 + 24) * 4;
    CHECK(summed.bytes[center] == 15 && summed.bytes[center + 1] == 0 &&
          summed.bytes[center + 3] == 127);
    auto replacement = CreateOffscreenBackendDevice(48, 32, options);
    CHECK(replacement);
    CHECK(renderer.Rebind(*replacement.device));
    CHECK(renderer.Prepare(1, frame, prepared));
    CHECK(renderer.Submit(prepared, token));
    CHECK(renderer.Detach());
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (options.backend == Backend::Vulkan)
        CHECK(QueryVulkanStats(*native.device).validationErrors == 0);
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (options.backend == Backend::D3D12)
        CHECK(QueryD3D12Stats(*native.device).debugErrors == 0);
#endif
    std::printf(
        "%s generic shaders: matrix, alignment, arbitrary slots, alpha, repeat, replacement, reflection rejection passed\n",
        api.c_str());
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
