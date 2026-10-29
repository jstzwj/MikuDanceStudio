#include "mikudancestudio/rhi_shader.hpp"
#include <cstdio>
#include <cstring>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
ShaderPipeline Pipeline() {
    ShaderProgramDesc d;
    d.vertex.hlsl = "float4 main(float3 p:POSITION):SV_POSITION{return float4(p,1);}";
    d.pixel.hlsl = "float4 main():SV_TARGET{return 1;}";
    d.inputs = StandardShaderInputs(VertexLayout::PositionNormalUv);
    d.bindings = {
        {ShaderBindingKind::Constants,
         0,
         3,
         80,
         {{"matrix", ShaderValueType::Matrix4x4, 0}, {"color", ShaderValueType::Float4, 64}}},
        {ShaderBindingKind::Texture, 0, 2},
        {ShaderBindingKind::Sampler, 0, 2}};
    ShaderPipeline p;
    p.program = std::make_shared<ShaderProgram>(std::move(d));
    p.raster.depthTest = p.raster.depthWrite = false;
    return p;
}
int main() {
    auto native = CreateNullDevice();
    auto& d = *native;
    const auto p = Pipeline();
    CHECK(ValidateShaderPipeline(p, d.ShaderFeatures()));
    auto bad = p;
    auto description = p.program->description;
    description.bindings[0].members[1].offset = 48;
    bad.program = std::make_shared<ShaderProgram>(description);
    CHECK(ValidateShaderPipeline(bad, d.ShaderFeatures()).reason == "overlapping_constant_members");
    description = p.program->description;
    description.bindings[1].stages = 1;
    bad.program = std::make_shared<ShaderProgram>(description);
    CHECK(!ValidateShaderPipeline(bad, d.ShaderFeatures()));
    description = p.program->description;
    description.bindings[1].slot = 4;
    bad.program = std::make_shared<ShaderProgram>(description);
    CHECK(!ValidateShaderPipeline(bad, d.ShaderFeatures()));
    description = p.program->description;
    description.pixel.stage = ShaderStage::Vertex;
    bad.program = std::make_shared<ShaderProgram>(description);
    CHECK(!ValidateShaderPipeline(bad, d.ShaderFeatures()));
    CHECK(!ValidateShaderPipeline(p, {}));
    const auto access =
        TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
    RenderTextureDesc target{16, 16, TextureFormat::Rgba8, access};
    CHECK(QueryShaderTextureSupport(d, target));
    target.samples = 4;
    CHECK(!QueryShaderTextureSupport(d, target));
    target.samples = 1;
    const auto source = d.CreateRenderTexture(target), output = d.CreateRenderTexture(target),
               undefined = d.CreateRenderTexture(target);
    const float vertices[3][8] = {{-1, 1, 0}, {3, 1, 0}, {-1, -3, 0}};
    const auto vb = d.CreateBuffer({BufferUsage::Vertex, sizeof(vertices)}, vertices);
    CHECK(source && output && vb && undefined);
    CHECK(!d.SetShaderPipeline(p));
    CHECK(d.BeginFrame({}));
    RenderPassDesc clear;
    clear.color = PassAttachment{source};
    CHECK(d.BeginRenderPass(clear) && d.EndRenderPass());
    RenderPassDesc pass;
    pass.color = PassAttachment{output};
    pass.sampled = {source};
    CHECK(d.BeginRenderPass(pass) && d.SetShaderPipeline(p));
    NonIndexedDraw draw{vb, 0, 3};
    draw.layout = VertexLayout::PositionNormalUv;
    draw = d.Record(draw);
    CHECK(!d.Draw(draw));
    ShaderResourceBindings bindings;
    CHECK(!d.BindShaderResources(bindings) && !d.Draw(draw));
    ShaderConstantBinding constants;
    constants.bytes.resize(80);
    bindings.constants = {constants};
    bindings.textures = {{0, source}};
    SamplerDesc sampler;
    sampler.min = sampler.mag = sampler.mip = SamplerFilter::Point;
    bindings.samplers = {{0, sampler}};
    CHECK(d.BindShaderResources(bindings) && d.Draw(draw));
    auto wrong = bindings;
    wrong.constants[0].bytes.resize(64);
    CHECK(!d.BindShaderResources(wrong) && !d.Draw(draw));
    wrong = bindings;
    wrong.textures = {{0, output}};
    CHECK(!d.BindShaderResources(wrong));
    wrong = bindings;
    wrong.textures = {{0, undefined}};
    CHECK(!d.BindShaderResources(wrong));
    wrong = bindings;
    wrong.textures = {{0, vb}};
    CHECK(!d.BindShaderResources(wrong));
    wrong = bindings;
    wrong.samplers.push_back(wrong.samplers[0]);
    CHECK(!d.BindShaderResources(wrong));
    auto alien = source;
    alien.device++;
    wrong = bindings;
    wrong.textures = {{0, alien}};
    CHECK(!d.BindShaderResources(wrong));
    CHECK(d.BindShaderResources(bindings) && d.Draw(draw));
    auto layout = draw;
    layout.layout = VertexLayout::PositionColor;
    CHECK(!d.Draw(layout));
    CHECK(d.EndRenderPass() && !d.Draw(draw) && d.EndFrame() && d.Present());
    CHECK(d.Destroy(source));
    const auto replacement = d.CreateRenderTexture(target);
    CHECK(replacement);
    pass.sampled.clear();
    CHECK(d.BeginFrame({}) && d.BeginRenderPass(pass) && d.SetShaderPipeline(p));
    CHECK(!d.BindShaderResources(bindings));
    CHECK(d.EndRenderPass() && d.EndFrame() && d.Present());
    // Reset invalidates recorded work and clears target initialization.
    CHECK(d.PrepareForReset() && d.RestoreAfterReset() && d.BeginFrame({}));
    clear.color = PassAttachment{replacement};
    CHECK(d.BeginRenderPass(clear) && d.EndRenderPass());
    pass.sampled = {replacement};
    CHECK(d.BeginRenderPass(pass) && d.SetShaderPipeline(p));
    bindings.textures = {{0, replacement}};
    CHECK(d.BindShaderResources(bindings) && !d.Draw(draw) && d.Draw(d.Record(draw)));
    CHECK(d.EndRenderPass() && d.EndFrame() && d.Present());
    CHECK(d.Destroy(replacement) && d.Destroy(output) && d.Destroy(undefined) && d.Destroy(vb));
    std::puts(
        "Shader stage/layout/packing, binding completeness, dependency, stale handle and reset contracts passed");
    return 0;
}
