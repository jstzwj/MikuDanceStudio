#define NOMINMAX
#include "mikudancestudio/mme_portable.hpp"
#include "mme_portable_api.h"
#include "mikudancestudio/rhi_post_process.hpp"
#include "../rhi_model_constants.hpp"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <regex>
#include <map>
#include <cctype>
namespace mikudancestudio {
namespace {
using namespace rhi;
using Microsoft::WRL::ComPtr;
std::string Compact(std::string text) {
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](unsigned char c) { return std::isspace(c) != 0; }),
               text.end());
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}
struct Source {
    std::weak_ptr<const std::string> owner;
    std::string legacy, modern;
    std::map<std::string, std::shared_ptr<const ShaderProgram>> programs;
};
std::vector<Source>& Sources() {
    static std::vector<Source> sources;
    return sources;
}
Source* LoadSource(const MmePortableEffect& effect, std::string& reason) {
    auto& sources = Sources();
    sources.erase(std::remove_if(sources.begin(), sources.end(),
                                 [](const auto& s) { return s.owner.expired(); }),
                  sources.end());
    for (auto& source : sources)
        if (source.owner.lock() == effect.source)
            return &source;
    if (!effect.source || effect.source->find("#include") != std::string::npos) {
        reason = "MME_includes_not_supported";
        return nullptr;
    }
    Source next;
    next.owner = effect.source;
    for (bool modern : {false, true}) {
        const D3D_SHADER_MACRO defines[] = {{"MDS_RHI", "1"}, {nullptr, nullptr}};
        ComPtr<ID3DBlob> result, errors;
        if (FAILED(D3DPreprocess(effect.source->data(), effect.source->size(), "portable.fx",
                                 modern ? defines : nullptr, nullptr, &result, &errors))) {
            reason = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                          errors->GetBufferSize())
                            : "MME_preprocess_failed";
            return nullptr;
        }
        (modern ? next.modern : next.legacy)
            .assign(static_cast<const char*>(result->GetBufferPointer()), result->GetBufferSize());
    }
    sources.push_back(std::move(next));
    return &sources.back();
}
bool ValidateLegacy(const Source& source, const MmePortableEffect& effect,
                    const MmePortablePass& pass, std::string& reason) {
    // A deliberately bounded FX state subset. Unknown states never inherit a
    // convenient approximation from the modern material renderer.
    const std::regex identifier("[A-Za-z_][A-Za-z_0-9]*");
    if (!std::regex_match(pass.name, identifier) || !std::regex_match(pass.vertex, identifier) ||
        !std::regex_match(pass.pixel, identifier)) {
        reason = "MME_invalid_entry_name";
        return false;
    }
    const std::regex pattern("\\bpass\\s+" + pass.name + "\\s*(?:<[^>]*>)?\\s*\\{([^{}]*)\\}");
    const std::map<std::string, std::string> required = {
        {"cullmode", "none"},
        {"zenable", effect.post ? "false" : "true"},
        {"zwriteenable", effect.post ? "false" : "true"},
        {"alphatestenable", "false"},
        {"alphablendenable", effect.post ? "false" : "true"},
        {"stencilenable", "false"},
        {"colorwriteenable", "15"},
        {"srgbwriteenable", "false"},
        {"fillmode", "solid"},
        {"depthbias", "0"},
        {"slopescaledepthbias", "0"},
        {"vertexshader", "compilevs_3_0" + Compact(pass.vertex) + "()"},
        {"pixelshader", "compileps_3_0" + Compact(pass.pixel) + "()"}};
    bool found = false;
    for (std::sregex_iterator it(source.legacy.begin(), source.legacy.end(), pattern), end;
         it != end; ++it) {
        std::map<std::string, std::string> values;
        const auto body = (*it)[1].str();
        std::size_t start = 0;
        while (start < body.size()) {
            auto end = body.find(';', start);
            if (end == std::string::npos)
                end = body.size();
            auto value = Compact(body.substr(start, end - start));
            start = end + 1;
            if (value.empty())
                continue;
            const auto split = value.find('=');
            if (split == std::string::npos ||
                !values.emplace(value.substr(0, split), value.substr(split + 1)).second) {
                reason = "MME_state_not_supported";
                return false;
            }
        }
        for (const auto& entry : required) {
            if (values[entry.first] != entry.second) {
                reason = "MME_state_not_supported";
                return false;
            }
            values.erase(entry.first);
        }
        if (!effect.post) {
            if (values["zfunc"] != "lessequal" || values["srcblend"] != "srcalpha" ||
                values["destblend"] != "invsrcalpha" || values["blendop"] != "add" ||
                values["separatealphablendenable"] != "false") {
                reason = "MME_blend_or_depth_not_supported";
                return false;
            }
            values.erase("zfunc");
            values.erase("srcblend");
            values.erase("destblend");
            values.erase("blendop");
            values.erase("separatealphablendenable");
        }
        if (!values.empty()) {
            reason = "MME_state_not_supported";
            return false;
        }
        found = true;
    }
    if (!found) {
        reason = "MME_pass_source_unavailable";
        return false;
    }
    const std::regex samplers(
        R"(\bsampler\s+([A-Za-z_][A-Za-z_0-9]*)\s*=\s*sampler_state\s*\{([^{}]*)\})");
    unsigned count = 0;
    for (std::sregex_iterator it(source.legacy.begin(), source.legacy.end(), samplers), end;
         it != end; ++it) {
        const auto name = (*it)[1].str();
        const auto expected = effect.post             ? (name == "SceneSampler"  ? "SceneColor"
                                                         : name == "PingSampler" ? "Ping"
                                                                                 : "")
                              : name == "baseSampler" ? "baseTexture"
                                                      : "";
        if (!*expected) {
            reason = "MME_sampler_not_supported";
            return false;
        }
        const auto state = Compact((*it)[2].str());
        const auto wanted =
            std::string("texture=<") + Compact(expected) + ">;minfilter=" +
            (effect.post
                 ? "point;magfilter=point;mipfilter=none;addressu=clamp;addressv=clamp;srgbtexture=false;"
                 : "linear;magfilter=linear;mipfilter=linear;addressu=wrap;addressv=wrap;srgbtexture=false;");
        if (state != wanted) {
            reason = "MME_sampler_not_supported";
            return false;
        }
        ++count;
    }
    if (count != (effect.post ? 2u : 1u)) {
        reason = "MME_sampler_not_supported";
        return false;
    }
    return true;
}
SnapshotBytes Bytes(const void* bytes, std::size_t size) {
    SnapshotBytes out;
    out.resize(size);
    std::memcpy(out.data(), bytes, size);
    out.Seal();
    return out;
}
}
std::shared_ptr<const rhi::ShaderProgram> PortableMmeProgram(const MmePortableEffect& effect,
                                                             const MmePortablePass& pass,
                                                             std::string& reason) {
    auto* source = LoadSource(effect, reason);
    if (!source)
        return {};
    const auto key = std::string(effect.post ? "post:" : "material:") + pass.name + ":" +
                     pass.vertex + ":" + pass.pixel;
    auto cached = source->programs.find(key);
    if (cached != source->programs.end())
        return cached->second;
    if (!ValidateLegacy(*source, effect, pass, reason))
        return {};
    ShaderProgramDesc d;
    d.name = pass.technique + "/" + pass.name;
    d.vertex.entry = pass.vertex;
    d.pixel.entry = pass.pixel;
    d.vertex.hlsl = d.pixel.hlsl = source->modern;
    d.inputs = {{"POSITION", 0, 0, 0, ShaderVertexFormat::Float3},
                {"TEXCOORD", 0, 1, 24, ShaderVertexFormat::Float2}};
    if (effect.post)
        d.bindings = {{ShaderBindingKind::Constants,
                       0,
                       2,
                       32,
                       {{"multiplyColor", ShaderValueType::Float4, 0},
                        {"addColor", ShaderValueType::Float4, 16}}}};
    else
        d.bindings = {{ShaderBindingKind::Constants,
                       0,
                       3,
                       96,
                       {{"worldViewProjection", ShaderValueType::Matrix4x4, 0},
                        {"materialDiffuse", ShaderValueType::Float4, 64},
                        {"tint", ShaderValueType::Float4, 80}}}};
    d.bindings.push_back({ShaderBindingKind::Texture, 0});
    d.bindings.push_back({ShaderBindingKind::Sampler, 0});
    auto program = std::make_shared<const ShaderProgram>(std::move(d));
    source->programs.emplace(key, program);
    return program;
}
bool CheckPortableMme(std::string& reason) {
    std::vector<MmePortableEffect> effects;
    if (!MmePortableEffects(effects, reason))
        return false;
    for (const auto& effect : effects)
        for (const auto& pass : effect.passes)
            if (!PortableMmeProgram(effect, pass, reason))
                return false;
    return true;
}
bool ApplyPortableMmeMaterial(rhi::ModelReplayDraw& draw, const MmePortableEffect& effect,
                              std::string& reason) {
    if (effect.post || effect.passes.size() != 1 ||
        draw.pipeline.vertexLayout != VertexLayout::PositionNormalUv ||
        draw.pipeline.topology != PrimitiveTopology::TriangleList) {
        reason = "MME_material_geometry_not_supported";
        return false;
    }
    auto program = PortableMmeProgram(effect, effect.passes[0], reason);
    if (!program)
        return false;
    unsigned base = 3;
    for (unsigned n = 0; n < 3; ++n)
        if (draw.pipeline.shader.layout.stages[n].role == MaterialTextureRole::Base)
            base = n;
    if (base == 3 || draw.pipeline.shader.srgbSample[base]) {
        reason = "MME_material_texture_unavailable";
        return false;
    }
    ReplayShader shader;
    shader.pipeline.program = program;
    shader.pipeline.viewport = draw.pipeline.viewport;
    shader.pipeline.raster = draw.pipeline.raster;
    shader.pipeline.raster.cull = BatchCull::None;
    shader.pipeline.raster.depthTest = shader.pipeline.raster.depthWrite =
        shader.pipeline.raster.alphaBlend = true;
    shader.pipeline.raster.depthCompare = DepthComparison::LessEqual;
    shader.pipeline.raster.sourceBlend = BlendFactor::SourceAlpha;
    shader.pipeline.raster.destinationBlend = BlendFactor::InvSourceAlpha;
    struct Constants {
        std::array<float, 16> wvp;
        std::array<float, 4> diffuse, tint;
    };
    const auto& p = draw.pipeline.shader;
    Constants c{detail::Dx9MatrixMultiply(detail::Dx9MatrixMultiply(p.world, p.view), p.projection),
                p.nativeDiffuse, effect.multiply};
    shader.constants.push_back({0, Bytes(&c, sizeof(c))});
    shader.textures.push_back({0, draw.textures[base], draw.targetTextures[base]});
    ShaderSamplerBinding sampler;
    sampler.sampler.u = sampler.sampler.v = SamplerAddress::Wrap;
    shader.samplers.push_back(sampler);
    draw.shader = std::move(shader);
    draw.textures = {};
    draw.targetTextures = {};
    draw.shadowTarget = 0;
    return true;
}
bool AppendPortableMmePost(rhi::RenderFrame& frame, const MmePortableEffect& effect,
                           std::string& reason) {
    if (!effect.post || effect.passes.size() != 2) {
        reason = "MME_post_pass_count";
        return false;
    }
    auto next = frame;
    auto input = next.finalColor;
    if (!input) {
        reason = "MME_post_missing_scene_color";
        return false;
    }
    for (const auto& pass : effect.passes) {
        auto program = PortableMmeProgram(effect, pass, reason);
        if (!program)
            return false;
        std::uint64_t id = 16;
        for (const auto& t : next.targets)
            id = std::max(id, t.id + 1);
        const auto source = std::find_if(next.targets.begin(), next.targets.end(),
                                         [&](const auto& t) { return t.id == input; });
        if (source == next.targets.end()) {
            reason = "MME_post_missing_scene_color";
            return false;
        }
        const unsigned width = source->description.width, height = source->description.height;
        if (!AppendPostProcess(next, input, id, width, height)) {
            reason = "MME_post_target_failed";
            return false;
        }
        auto& draw = next.passes.back().draws.front();
        ReplayShader shader;
        shader.pipeline.program = program;
        shader.pipeline.viewport = draw.pipeline.viewport;
        shader.pipeline.raster = draw.pipeline.raster;
        std::array<float, 8> constants;
        std::copy(effect.multiply.begin(), effect.multiply.end(), constants.begin());
        std::copy(effect.add.begin(), effect.add.end(), constants.begin() + 4);
        shader.constants.push_back({0, Bytes(constants.data(), sizeof(constants))});
        shader.textures.push_back({0, {}, draw.targetTextures[0]});
        shader.samplers.push_back(
            {0,
             {SamplerFilter::Point, SamplerFilter::Point, SamplerFilter::Point,
              SamplerAddress::Clamp, SamplerAddress::Clamp, 1}});
        draw.shader = std::move(shader);
        draw.targetTextures = {};
        draw.textures = {};
        draw.pipeline = {};
        next.passes.back().name = "MME/" + pass.technique + "/" + pass.name;
        input = id;
    }
    const auto valid = ValidateRenderFrame(next);
    if (!valid) {
        reason = valid.reason;
        return false;
    }
    frame = std::move(next);
    return true;
}
}
