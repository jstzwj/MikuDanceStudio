#include "mikudancestudio/rhi_builtin_programs.hpp"
#include "rhi_generic_shaders.hpp"
#include "rhi_model_constants.hpp"
#include <cstring>
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
#include "rhi_vulkan_spirv.hpp"
#endif
namespace mikudancestudio::rhi {
std::shared_ptr<const ShaderProgram> PostProcessProgram() {
    static const auto program = [] {
        ShaderProgramDesc d;
        d.name = "shared_postprocess";
        d.vertex.entry = "post_vs";
        d.pixel.entry = "post_ps";
        d.vertex.hlsl = d.pixel.hlsl = genericPostSource;
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
        d.vertex.spirv.resize(sizeof(spirv::generic_post_vertex));
        std::memcpy(d.vertex.spirv.data(), spirv::generic_post_vertex, d.vertex.spirv.size());
        d.pixel.spirv.resize(sizeof(spirv::generic_post_pixel));
        std::memcpy(d.pixel.spirv.data(), spirv::generic_post_pixel, d.pixel.spirv.size());
#endif
        d.inputs = {{"POSITION", 0, 0, 0, ShaderVertexFormat::Float3},
                    {"TEXCOORD", 0, 1, 24, ShaderVertexFormat::Float2}};
        d.bindings = {{ShaderBindingKind::Constants,
                       2,
                       2,
                       64,
                       {{"multiplyColor", ShaderValueType::Float4, 0},
                        {"addColor", ShaderValueType::Float4, 16},
                        {"sourceRect", ShaderValueType::Float4, 32}}},
                      {ShaderBindingKind::Texture, 0},
                      {ShaderBindingKind::Sampler, 0}};
        return std::make_shared<const ShaderProgram>(std::move(d));
    }();
    return program;
}
ShaderPipeline PostProcessPipeline(const ModelPipeline& p) {
    ShaderPipeline out;
    out.program = PostProcessProgram();
    out.vertexLayout = p.vertexLayout;
    out.topology = p.topology;
    out.raster = p.raster;
    out.viewport = p.viewport;
    out.colorWriteMask = p.colorWriteMask;
    return out;
}
ShaderResourceBindings PostProcessBindings(const ModelPipeline& p, Handle source) {
    const auto c = detail::BuildPostProcessConstants(p);
    ShaderResourceBindings out;
    out.constants.push_back({2});
    out.constants.back().bytes.resize(sizeof(c));
    std::memcpy(out.constants.back().bytes.data(), &c, sizeof(c));
    out.constants.back().bytes.Seal();
    out.textures.push_back({0, source});
    out.samplers.push_back({0, p.shader.samplers[0]});
    return out;
}
}
