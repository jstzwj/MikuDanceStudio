#define NOMINMAX
#include "mikudancestudio/mme_portable.hpp"
#include "mme_portable_api.h"
#include "mme_legacy_api.h"
#include "mme_standard_api.h"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/rhi_post_process.hpp"
#include "../rhi_model_constants.hpp"
#include "mikudancestudio/rhi_shared_targets.hpp"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <regex>
#include <map>
#include <cctype>
#include <set>
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
    if (effect.legacy && effect.source) {
        // Import already consumed the loaded FX9 expansion. It must not depend
        // on a second, unrelated MDS_RHI branch or reopen includes from disk.
        Source next;
        next.owner = effect.source;
        sources.push_back(std::move(next));
        return &sources.back();
    }
    if (!effect.source || (effect.source->find("#include") != std::string::npos &&
                           (!effect.legacySource || !effect.modernSource))) {
        reason = "MME_includes_not_supported";
        return nullptr;
    }
    Source next;
    next.owner = effect.source;
    if (effect.legacySource && effect.modernSource) {
        next.legacy = *effect.legacySource;
        next.modern = *effect.modernSource;
        sources.push_back(std::move(next));
        return &sources.back();
    }
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
    std::map<std::string, std::string> required = {
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
    for (unsigned slot = 1; slot < pass.colors.size(); ++slot)
        required["colorwriteenable" + std::to_string(slot)] = "15";
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
        std::string expected = name == "baseSampler" && !effect.post ? "baseTexture" : "";
        if (effect.post)
            for (const auto& target : effect.targets) {
                const auto sampler =
                    target.name == "SceneColor" ? "SceneSampler" : target.name + "Sampler";
                if (name == sampler && target.name != "FinalColor")
                    expected = target.name;
            }
        if (expected.empty()) {
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
    const auto colorTargets =
        std::count_if(effect.targets.begin(), effect.targets.end(),
                      [](const auto& t) { return !t.depth && t.name != "FinalColor"; });
    if (count != (effect.post ? colorTargets : 1u)) {
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
bool ReflectParameters(ShaderProgramDesc& d, const MmePortableEffect& effect, std::string& reason) {
    if (effect.parameters.empty())
        return true;
    for (const auto* module : {&d.vertex, &d.pixel}) {
        ComPtr<ID3DBlob> code, errors;
        const auto hr = D3DCompile(
            module->hlsl.data(), module->hlsl.size(), "mme-parameters", nullptr, nullptr,
            module->entry.c_str(), module->stage == ShaderStage::Vertex ? "vs_5_0" : "ps_5_0",
            D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_IEEE_STRICTNESS, 0, &code, &errors);
        if (FAILED(hr)) {
            reason = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                          errors->GetBufferSize())
                            : "MME_parameter_shader_compile";
            return false;
        }
        ComPtr<ID3D11ShaderReflection> reflection;
        if (FAILED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(),
                              IID_PPV_ARGS(&reflection)))) {
            reason = "MME_parameter_reflection";
            return false;
        }
        D3D11_SHADER_DESC sd{};
        reflection->GetDesc(&sd);
        for (unsigned n = 0; n < sd.BoundResources; ++n) {
            D3D11_SHADER_INPUT_BIND_DESC resource{};
            reflection->GetResourceBindingDesc(n, &resource);
            if (resource.Type != D3D_SIT_CBUFFER || resource.BindPoint == 0)
                continue;
            if (resource.BindPoint >= 4 || resource.BindCount != 1) {
                reason = "MME_parameter_slot";
                return false;
            }
            auto* cb = reflection->GetConstantBufferByName(resource.Name);
            D3D11_SHADER_BUFFER_DESC block{};
            cb->GetDesc(&block);
            ShaderBinding binding{ShaderBindingKind::Constants, resource.BindPoint,
                                  unsigned(module->stage), block.Size};
            for (unsigned i = 0; i < block.Variables; ++i) {
                auto* var = cb->GetVariableByIndex(i);
                D3D11_SHADER_VARIABLE_DESC v{};
                D3D11_SHADER_TYPE_DESC t{};
                var->GetDesc(&v);
                var->GetType()->GetDesc(&t);
                const auto parameter =
                    std::find_if(effect.parameters.begin(), effect.parameters.end(),
                                 [&](const auto& p) { return p.name == v.Name; });
                if (parameter == effect.parameters.end() || t.Rows != parameter->rows ||
                    t.Columns != parameter->columns ||
                    (t.Elements ? t.Elements : 1) != parameter->elements) {
                    reason = "MME_parameter_layout_mismatch";
                    return false;
                }
                ShaderValueType type;
                if (t.Type == D3D_SVT_FLOAT &&
                    parameter->type == MmePortableParameter::Type::Float) {
                    if (t.Rows == 4 && t.Columns == 4 && t.Class == D3D_SVC_MATRIX_ROWS)
                        type = ShaderValueType::Matrix4x4;
                    else if (t.Rows == 1)
                        type = t.Columns == 1   ? ShaderValueType::Float
                               : t.Columns == 2 ? ShaderValueType::Float2
                               : t.Columns == 3 ? ShaderValueType::Float3
                                                : ShaderValueType::Float4;
                    else {
                        reason = "MME_parameter_matrix_not_supported";
                        return false;
                    }
                } else if (t.Rows == 1 && t.Columns == 1 && t.Type == D3D_SVT_INT &&
                           parameter->type == MmePortableParameter::Type::Int)
                    type = ShaderValueType::Int;
                else if (t.Rows == 1 && t.Columns == 1 && t.Type == D3D_SVT_UINT &&
                         parameter->type == MmePortableParameter::Type::Bool)
                    type = ShaderValueType::UInt;
                else {
                    reason = "MME_parameter_type_mismatch";
                    return false;
                }
                binding.members.push_back(
                    {v.Name, type, v.StartOffset, t.Elements ? t.Elements : 1});
            }
            const auto previous =
                std::find_if(d.bindings.begin(), d.bindings.end(), [&](const auto& b) {
                    return b.kind == ShaderBindingKind::Constants && b.slot == binding.slot;
                });
            if (previous == d.bindings.end())
                d.bindings.push_back(std::move(binding));
            else {
                if (previous->bytes != binding.bytes ||
                    previous->members.size() != binding.members.size()) {
                    reason = "MME_parameter_stage_layout";
                    return false;
                }
                for (unsigned i = 0; i < binding.members.size(); ++i) {
                    const auto& a = previous->members[i];
                    const auto& b = binding.members[i];
                    if (a.name != b.name || a.offset != b.offset || a.type != b.type ||
                        a.count != b.count) {
                        reason = "MME_parameter_stage_layout";
                        return false;
                    }
                }
                previous->stages |= binding.stages;
            }
        }
    }
    return true;
}
bool BindParameters(ReplayShader& shader, const MmePortableEffect& effect, std::string& reason) {
    for (const auto& binding : shader.pipeline.program->description.bindings) {
        if (binding.kind != ShaderBindingKind::Constants || !binding.slot)
            continue;
        SnapshotBytes bytes;
        bytes.resize(binding.bytes);
        for (const auto& member : binding.members) {
            const auto p = std::find_if(effect.parameters.begin(), effect.parameters.end(),
                                        [&](const auto& p) { return p.name == member.name; });
            if (p == effect.parameters.end()) {
                reason = "MME_parameter_missing_value";
                return false;
            }
            const bool floating = p->type == MmePortableParameter::Type::Float;
            const auto expected = member.type == ShaderValueType::Float       ? 1u
                                  : member.type == ShaderValueType::Float2    ? 2u
                                  : member.type == ShaderValueType::Float3    ? 3u
                                  : member.type == ShaderValueType::Float4    ? 4u
                                  : member.type == ShaderValueType::Matrix4x4 ? 16u
                                                                              : 0u;
            if ((expected && (!floating || p->rows * p->columns != expected ||
                              p->rows != (expected == 16 ? 4u : 1u))) ||
                (!expected && (p->rows != 1 || p->columns != 1 ||
                               p->type != (member.type == ShaderValueType::Int
                                               ? MmePortableParameter::Type::Int
                                               : MmePortableParameter::Type::Bool)))) {
                reason = "MME_parameter_snapshot_type";
                return false;
            }
            const unsigned size = p->rows * p->columns * 4,
                           stride = p->elements > 1 ? (size + 15) / 16 * 16 : size;
            if (member.offset + stride * (member.count - 1) + size > bytes.size() ||
                member.count != p->elements || p->value.size() != size * p->elements) {
                reason = "MME_parameter_snapshot_layout";
                return false;
            }
            for (unsigned i = 0; i < member.count; ++i)
                std::memcpy(bytes.data() + member.offset + i * stride, p->value.data() + i * size,
                            size);
        }
        bytes.Seal();
        shader.constants.push_back({binding.slot, std::move(bytes)});
    }
    return true;
}
const MmeLegacyPass* LegacyPass(const MmePortableEffect& effect, const MmePortablePass& pass) {
    if (!effect.legacy)
        return nullptr;
    const auto found = std::find_if(
        effect.legacy->passes.begin(), effect.legacy->passes.end(), [&](const auto& p) {
            return p.technique == pass.technique && p.name == pass.name &&
                   p.vertex == pass.vertex && p.pixel == pass.pixel;
        });
    return found == effect.legacy->passes.end() ? nullptr : &*found;
}
bool LegacyRaster(const MmeLegacyPass& pass, BatchRasterState& raster, std::string& reason) {
    auto states = pass.states;
    const auto fail = [&] {
        reason = "MME_legacy_render_state";
        return false;
    };
    // These host-dependent features have no implicit native approximation.
    static const std::map<std::string, std::string> fixed = {{"ALPHATESTENABLE", "FALSE"},
                                                             {"STENCILENABLE", "FALSE"},
                                                             {"FILLMODE", "SOLID"},
                                                             {"DEPTHBIAS", "0"},
                                                             {"SLOPESCALEDEPTHBIAS", "0"},
                                                             {"SRGBWRITEENABLE", "FALSE"},
                                                             {"COLORWRITEENABLE", "15"},
                                                             {"SEPARATEALPHABLENDENABLE", "FALSE"},
                                                             {"BLENDOP", "ADD"}};
    for (const auto& [key, value] : fixed) {
        if (states[key] != value)
            return fail();
        states.erase(key);
    }
    for (const auto* key : {"ZENABLE", "ZWRITEENABLE", "ALPHABLENDENABLE"}) {
        const auto found = states.find(key);
        if (found == states.end())
            continue; // Inherit this draw's explicit CPU state.
        if (found->second != "TRUE" && found->second != "FALSE")
            return fail();
        (std::string(key) == "ZENABLE"        ? raster.depthTest
         : std::string(key) == "ZWRITEENABLE" ? raster.depthWrite
                                              : raster.alphaBlend) = found->second == "TRUE";
        states.erase(found);
    }
    if (states.count("CULLMODE")) {
        const auto& value = states.at("CULLMODE");
        if (value != "NONE" && value != "CW" && value != "CCW")
            return fail();
        raster.cull = value == "NONE" ? BatchCull::None
                      : value == "CW" ? BatchCull::Clockwise
                                      : BatchCull::CounterClockwise;
        states.erase("CULLMODE");
    }
    static const std::map<std::string, DepthComparison> comparisons = {
        {"NEVER", DepthComparison::Never},
        {"LESS", DepthComparison::Less},
        {"EQUAL", DepthComparison::Equal},
        {"LESSEQUAL", DepthComparison::LessEqual},
        {"GREATER", DepthComparison::Greater},
        {"NOTEQUAL", DepthComparison::NotEqual},
        {"GREATEREQUAL", DepthComparison::GreaterEqual},
        {"ALWAYS", DepthComparison::Always}};
    if (states.count("ZFUNC")) {
        if (!comparisons.count(states.at("ZFUNC")))
            return fail();
        raster.depthCompare = comparisons.at(states.at("ZFUNC"));
        states.erase("ZFUNC");
    }
    static const std::map<std::string, BlendFactor> factors = {
        {"ZERO", BlendFactor::Zero},
        {"ONE", BlendFactor::One},
        {"SRCALPHA", BlendFactor::SourceAlpha},
        {"INVSRCALPHA", BlendFactor::InvSourceAlpha},
        {"DESTALPHA", BlendFactor::DestinationAlpha},
        {"INVDESTALPHA", BlendFactor::InvDestinationAlpha}};
    for (const auto* key : {"SRCBLEND", "DESTBLEND"}) {
        if (!states.count(key))
            continue;
        if (!factors.count(states.at(key)))
            return fail();
        (std::string(key) == "SRCBLEND" ? raster.sourceBlend : raster.destinationBlend) =
            factors.at(states.at(key));
        states.erase(key);
    }
    return states.empty() || fail();
}
bool BindLegacyMaterial(ReplayShader& shader, const ModelReplayDraw& draw,
                        const MmePortableEffect& effect, std::string& reason) {
    auto values = effect;
    const auto& s = draw.pipeline.shader;
    if (!draw.pipeline.viewport || !draw.pipeline.viewport->width ||
        !draw.pipeline.viewport->height) {
        reason = "MME_legacy_viewport_missing";
        return false;
    }
    const auto& viewport = *draw.pipeline.viewport;
    const std::array<float, 4> dimensions{1.f / viewport.width, 1.f / viewport.height,
                                          float(viewport.width), float(viewport.height)};
    shader.constants.push_back({0, Bytes(dimensions.data(), sizeof(dimensions))});
    MmeStandardInputs inputs;
    inputs.world = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // PMD/PMX object world
    inputs.beginWorld = s.world;
    inputs.view = detail::Dx9MatrixMultiply(s.world, s.view);
    inputs.projection = s.projection;
    inputs.material = {s.nativeDiffuse, s.materialAmbient, s.materialSpecular, s.emissive};
    inputs.lightColor = {s.lights[0].diffuse, s.lights[0].ambient, s.lights[0].specular};
    inputs.lightDirection = s.lights[0].direction;
    inputs.viewport = {float(viewport.width), float(viewport.height)};
    inputs.power = s.shininess;
    inputs.clock = effect.clock;
    inputs.clockReady = effect.clockReady;
    inputs.counts = effect.counts;
    for (const auto& stage : s.layout.stages) {
        if (stage.role == MaterialTextureRole::Base)
            inputs.flags[0] = true;
        if (stage.role == MaterialTextureRole::Sphere) {
            inputs.flags[1] = true;
            inputs.flags[2] = stage.coordinates == MaterialCoordinates::MeshUv1;
        }
        if (stage.role == MaterialTextureRole::Toon)
            inputs.flags[3] = true;
    }
    inputs.flags[4] = effect.transparent;
    MmeStandardMath math;
    math.multiply = detail::Dx9MatrixMultiply;
    math.inverse = [](const auto& matrix, auto& inverse) {
        using Matrix = d3dx::D3DXMATRIXF;
        Matrix m, value;
        std::memcpy(&m, matrix.data(), 64);
        if (!d3dx::Get().inverse(&value, nullptr, &m))
            return false;
        std::memcpy(inverse.data(), &value, 64);
        return true;
    };
    math.normalize = [](auto& value) { d3dx::Get().vec3Normalize(value.data(), value.data()); };
    for (auto& p : values.parameters) {
        const auto definition =
            std::find_if(effect.legacy->variables.begin(), effect.legacy->variables.end(),
                         [&](const auto& v) { return v.name == p.name; });
        if (definition == effect.legacy->variables.end()) {
            reason = "MME_legacy_parameter_missing";
            return false;
        }
        MmeStandardBinding binding;
        if (!MmeStandardClassify(*definition, binding, reason))
            return false;
        if (binding.kind != MmeStandardBinding::Kind::Value &&
            !MmeStandardEvaluate(binding, inputs, math, p.value, reason))
            return false;
    }
    if (!BindParameters(shader, values, reason))
        return false;
    for (unsigned slot = 0; slot < effect.legacy->samplers.size(); ++slot) {
        const auto& sampler = effect.legacy->samplers[slot];
        if (sampler.semantic.empty() || sampler.semantic == "RENDERCOLORTARGET" ||
            sampler.semantic == "OFFSCREENRENDERTARGET") {
            const auto target =
                std::find_if(effect.targets.begin(), effect.targets.end(),
                             [&](const auto& t) { return t.name == sampler.texture; });
            if (target == effect.targets.end() || target->depth || !target->identity) {
                reason = "MME_shared_material_target_missing";
                return false;
            }
            if (target->mipmapped && sampler.mipNone) {
                reason = "MME_mipped_sampler_none_not_supported";
                return false;
            }
            shader.textures.push_back({slot,
                                       {},
                                       {SharedFrameTargetId(target->identity),
                                        target->width ? target->width : viewport.width,
                                        target->height ? target->height : viewport.height}});
            shader.samplers.push_back(
                {slot,
                 {sampler.linearMin ? SamplerFilter::Linear : SamplerFilter::Point,
                  sampler.linearMag ? SamplerFilter::Linear : SamplerFilter::Point,
                  sampler.linearMip ? SamplerFilter::Linear : SamplerFilter::Point,
                  sampler.clampU ? SamplerAddress::Clamp : SamplerAddress::Wrap,
                  sampler.clampV ? SamplerAddress::Clamp : SamplerAddress::Wrap, 1}});
            continue;
        }
        const auto role = sampler.semantic == "MATERIALTEXTURE"     ? MaterialTextureRole::Base
                          : sampler.semantic == "MATERIALSPHEREMAP" ? MaterialTextureRole::Sphere
                                                                    : MaterialTextureRole::Toon;
        unsigned input = 3;
        for (unsigned n = 0; n < 3; ++n)
            if (s.layout.stages[n].role == role)
                input = n;
        if (input == 3 || s.srgbSample[input]) {
            reason = "MME_legacy_material_texture_unavailable";
            return false;
        }
        shader.textures.push_back({slot, draw.textures[input], draw.targetTextures[input]});
        shader.samplers.push_back(
            {slot,
             {sampler.linearMin ? SamplerFilter::Linear : SamplerFilter::Point,
              sampler.linearMag ? SamplerFilter::Linear : SamplerFilter::Point,
              sampler.linearMip ? SamplerFilter::Linear : SamplerFilter::Point,
              sampler.clampU ? SamplerAddress::Clamp : SamplerAddress::Wrap,
              sampler.clampV ? SamplerAddress::Clamp : SamplerAddress::Wrap, 1}});
    }
    return true;
}
}
std::shared_ptr<const rhi::ShaderProgram> PortableMmeProgram(const MmePortableEffect& effect,
                                                             const MmePortablePass& pass,
                                                             std::string& reason) {
    auto* source = LoadSource(effect, reason);
    if (!source)
        return {};
    const auto key = std::string(effect.post ? "post:" : "material:") + pass.technique + ":" +
                     pass.name + ":" + pass.vertex + ":" + pass.pixel + ":" +
                     std::to_string(pass.colors.size()) + ":" + std::to_string(pass.sampled.size());
    auto cached = source->programs.find(key);
    if (cached != source->programs.end())
        return cached->second;
    if (effect.legacy) {
        const auto* imported = LegacyPass(effect, pass);
        BatchRasterState raster;
        if (!imported || !LegacyRaster(*imported, raster, reason))
            return {};
        if (effect.post &&
            (!imported->states.count("CULLMODE") || !imported->states.count("ALPHABLENDENABLE") ||
             (raster.alphaBlend &&
              (!imported->states.count("SRCBLEND") || !imported->states.count("DESTBLEND"))) ||
             (imported->states.count("ZENABLE") && raster.depthTest &&
              (!imported->states.count("ZWRITEENABLE") || !imported->states.count("ZFUNC"))))) {
            reason = "MME_legacy_scene_inherited_state";
            return {};
        }
        ShaderProgramDesc d;
        std::string hlsl;
        std::vector<MmeLegacyInput> inputs;
        if (!MmeLegacyShader(*effect.legacy, *imported, hlsl, inputs, reason))
            return {};
        d.name = pass.technique + "/" + pass.name;
        d.vertex.entry = "mdsLegacyVS";
        d.pixel.entry = "mdsLegacyPS";
        d.vertex.hlsl = d.pixel.hlsl = std::move(hlsl);
        for (const auto& i : inputs) {
            const bool uv = i.semantic == "TEXCOORD0",
                       normal = i.semantic == "NORMAL" || i.semantic == "NORMAL0";
            d.inputs.push_back({uv       ? "TEXCOORD"
                                : normal ? "NORMAL"
                                         : "POSITION",
                                0, unsigned(d.inputs.size()),
                                uv       ? 24u
                                : normal ? 12u
                                         : 0u,
                                uv ? ShaderVertexFormat::Float2 : ShaderVertexFormat::Float3});
        }
        d.bindings.push_back({ShaderBindingKind::Constants,
                              0,
                              1,
                              16,
                              {{"mdsLegacyViewport", ShaderValueType::Float4, 0}}});
        if (effect.post)
            d.colorOutputs.assign(pass.colors.size(), 4);
        const auto active = effect.post ? pass.sampleSlots : std::vector<unsigned>{};
        for (unsigned slot = 0; slot < effect.legacy->samplers.size(); ++slot) {
            if (effect.post && std::find(active.begin(), active.end(), slot) == active.end())
                continue;
            d.bindings.push_back({ShaderBindingKind::Texture, slot});
            d.bindings.push_back({ShaderBindingKind::Sampler, slot});
        }
        if (!ReflectParameters(d, effect, reason))
            return {};
        auto program = std::make_shared<const ShaderProgram>(std::move(d));
        source->programs.emplace(key, program);
        return program;
    }
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
    if (effect.post)
        d.colorOutputs.assign(pass.colors.size(), 4);
    for (unsigned slot = 0; slot < (effect.post ? pass.sampled.size() : 1); ++slot) {
        d.bindings.push_back({ShaderBindingKind::Texture, slot});
        d.bindings.push_back({ShaderBindingKind::Sampler, slot});
    }
    if (!ReflectParameters(d, effect, reason))
        return {};
    auto program = std::make_shared<const ShaderProgram>(std::move(d));
    source->programs.emplace(key, program);
    return program;
}
bool RegisterPortableMmeTargets(RenderFrame& frame, const std::vector<MmePortableEffect>& effects,
                                std::string& reason) {
    const auto output = std::find_if(frame.targets.begin(), frame.targets.end(),
                                     [&](const auto& t) { return t.id == frame.finalColor; });
    if (output == frame.targets.end()) {
        reason = "MME_post_missing_scene_color";
        return false;
    }
    const auto width = output->description.width, height = output->description.height;
    auto next = frame;
    SharedFrameTargets registry(next);
    for (const auto& effect : effects)
        for (const auto& target : effect.targets) {
            if (!target.identity)
                continue;
            const auto format = target.depth ? TextureFormat::Depth24Stencil8
                                : target.format == MmePortableFormat::Rgba16Float
                                    ? TextureFormat::Rgba16Float
                                    : TextureFormat::Rgba8;
            RenderTextureDesc desc{
                target.width ? target.width : width, target.height ? target.height : height, format,
                target.depth ? unsigned(TextureAccess::DepthStencilAttachment)
                             : TextureAccess::ColorAttachment | TextureAccess::Sampled |
                                   TextureAccess::CopySource};
            if (target.mipmapped) {
                desc.mips = 1;
                for (auto size = std::max(desc.width, desc.height); size > 1; size >>= 1)
                    ++desc.mips;
            }
            SharedTargetLease lease;
            if (!registry.Declare(target.identity, desc, lease, reason))
                return false;
        }
    frame = std::move(next);
    return true;
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
    if (effect.legacy) {
        const auto* imported = LegacyPass(effect, effect.passes[0]);
        if (!imported) {
            reason = "MME_legacy_pass_source";
            return false;
        }
        ReplayShader shader;
        shader.pipeline.program = program;
        shader.pipeline.viewport = draw.pipeline.viewport;
        shader.pipeline.raster = draw.pipeline.raster;
        if (!LegacyRaster(*imported, shader.pipeline.raster, reason) ||
            !BindLegacyMaterial(shader, draw, effect, reason))
            return false;
        draw.shader = std::move(shader);
        draw.textures = {};
        draw.targetTextures = {};
        draw.shadowTarget = 0;
        draw.pipeline.shadow = {};
        return true;
    }
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
    if (!BindParameters(shader, effect, reason))
        return false;
    shader.textures.push_back({0, draw.textures[base], draw.targetTextures[base]});
    ShaderSamplerBinding sampler;
    sampler.sampler.u = sampler.sampler.v = SamplerAddress::Wrap;
    shader.samplers.push_back(sampler);
    draw.shader = std::move(shader);
    draw.textures = {};
    draw.targetTextures = {};
    draw.shadowTarget = 0;
    draw.pipeline.shadow = {};
    return true;
}
// Preserve the native Buffer quad's vertex inputs and diagonal. A large triangle
// is equivalent only for affine vertex shaders, not arbitrary imported FX9 VS.
bool AttachLegacyBufferQuad(RenderFrame& frame, ModelReplayDraw& draw, unsigned width,
                            unsigned height) {
    const float left = -1 - (1.f / width) * 2, top = 1 + (1.f / height) * 2, u = -1.f / width,
                v = -1.f / height;
    const float vertices[][8] = {{1, -1, 0, 0, 0, 0, 1, 1},     {left, -1, 0, 0, 0, 0, u, 1},
                                 {left, top, 0, 0, 0, 0, u, v}, {1, -1, 0, 0, 0, 0, 1, 1},
                                 {left, top, 0, 0, 0, 0, u, v}, {1, top, 0, 0, 0, 0, 1, v}};
    ModelReplayResource resource;
    // Each assembled frame publishes a new revision, including after an effect
    // disappears/reappears or a resize returns to an old extent. Native dynamic
    // buffers reuse the completion-protected allocation for this tiny stream.
    resource.key.id = UINT64_MAX - (std::uint64_t(1) << 32) - (std::uint64_t(width) << 16) - height;
    resource.bytes.resize(sizeof(vertices));
    std::memcpy(resource.bytes.data(), vertices, sizeof(vertices));
    const auto found = std::find_if(frame.resources.begin(), frame.resources.end(),
                                    [&](const auto& r) { return r.key.id == resource.key.id; });
    if (found == frame.resources.end()) {
        static std::atomic<std::uint64_t> nextRevision{1};
        resource.key.version = nextRevision++;
        frame.resources.push_back(resource);
    } else if (found->kind != resource.kind || found->vertexLayout != resource.vertexLayout ||
               found->bytes != resource.bytes)
        return false;
    else
        resource.key = found->key;
    draw.indexed = false;
    draw.vertices = resource.key;
    draw.drawVertexCount = 6;
    return true;
}
bool AppendPortableScene(RenderFrame& frame, const MmePortableEffect& effect, std::string& reason,
                         bool deferValidation) {
    const auto fail = [&](const char* r) {
        reason = r;
        return false;
    };
    const auto source = std::find_if(frame.targets.begin(), frame.targets.end(),
                                     [&](const auto& t) { return t.id == frame.finalColor; });
    if (source == frame.targets.end() || source->description.format != TextureFormat::Rgba8)
        return fail("MME_post_missing_scene_color");
    const unsigned width = source->description.width, height = source->description.height;
    const auto capture = std::find_if(effect.steps.begin(), effect.steps.end(), [](const auto& s) {
        return s.kind == MmePortableStep::Kind::Capture;
    });
    if (capture == effect.steps.end() || capture->colors.size() != 1)
        return fail("MME_scene_capture_layout");
    auto next = frame;
    if (!RegisterPortableMmeTargets(next, {effect}, reason))
        return false;
    next.passes.clear();
    std::map<std::string, std::uint64_t> ids;
    std::map<std::string, TextureFormat> formats;
    const auto captureTarget =
        std::find_if(effect.targets.begin(), effect.targets.end(),
                     [&](const auto& t) { return t.name == capture->colors[0]; });
    ids[capture->colors[0]] = captureTarget != effect.targets.end() && captureTarget->identity
                                  ? SharedFrameTargetId(captureTarget->identity)
                                  : frame.finalColor;
    formats[capture->colors[0]] = TextureFormat::Rgba8;
    const auto captured =
        std::find_if(next.targets.begin(), next.targets.end(),
                     [&](const auto& t) { return t.id == ids.at(capture->colors[0]); });
    if (captured == next.targets.end() || captured->description.width != width ||
        captured->description.height != height)
        return fail("MME_scene_capture_extent_not_supported");
    std::uint64_t incomingDepth = 0;
    if (!capture->depth.empty()) {
        for (const auto& pass : frame.passes)
            if (PrimaryAttachment(pass) && PrimaryAttachment(pass)->target == frame.finalColor &&
                pass.depthStencil)
                incomingDepth = pass.depthStencil->target;
        const auto target = std::find_if(effect.targets.begin(), effect.targets.end(),
                                         [&](const auto& t) { return t.name == capture->depth; });
        if (target != effect.targets.end() && target->identity)
            ids[capture->depth] = SharedFrameTargetId(target->identity);
        else if (incomingDepth)
            ids[capture->depth] = incomingDepth;
    }
    auto descriptions = effect.targets;
    descriptions.push_back({"FinalColor", MmePortableFormat::Rgba8});
    descriptions.push_back({"FinalDepth", MmePortableFormat::Rgba8, true});
    std::uint64_t id = 16;
    for (const auto& t : frame.targets)
        if (t.id < (std::uint64_t(1) << 62))
            id = std::max(id, t.id + 1);
    for (const auto& t : descriptions) {
        const auto format = t.depth ? TextureFormat::Depth24Stencil8
                            : t.format == MmePortableFormat::Rgba16Float
                                ? TextureFormat::Rgba16Float
                                : TextureFormat::Rgba8;
        formats[t.name] = format;
        if (ids.count(t.name))
            continue;
        if (t.identity) {
            ids[t.name] = SharedFrameTargetId(t.identity);
            continue;
        }
        ids[t.name] = id++;
        next.targets.push_back({ids[t.name],
                                {width, height, format,
                                 t.depth ? unsigned(TextureAccess::DepthStencilAttachment)
                                         : TextureAccess::ColorAttachment | TextureAccess::Sampled |
                                               TextureAccess::CopySource}});
    }
    std::set<std::uint64_t> initialized;
    for (const auto& step : effect.steps) {
        if (step.kind == MmePortableStep::Kind::Capture) {
            for (auto pass : frame.passes) {
                const auto remap = [&](FrameAttachment& a) {
                    if (a.target == frame.finalColor)
                        a.target = ids.at(capture->colors[0]);
                };
                if (pass.color)
                    remap(*pass.color);
                for (auto& a : pass.colors)
                    remap(a);
                if (pass.depthStencil && incomingDepth &&
                    pass.depthStencil->target == incomingDepth)
                    pass.depthStencil->target = ids.at(capture->depth);
                const auto preserve = [&](FrameAttachment& a) {
                    if (initialized.count(a.target))
                        a.load = AttachmentLoad::Load;
                    initialized.insert(a.target);
                };
                if (pass.color)
                    preserve(*pass.color);
                for (auto& a : pass.colors)
                    preserve(a);
                if (pass.depthStencil)
                    preserve(*pass.depthStencil);
                next.passes.push_back(std::move(pass));
            }
            continue;
        }
        FramePass output;
        output.name = "MME/script";
        const auto attachment = [&](const std::string& name, bool clear) {
            FrameAttachment a{ids.at(name)};
            a.load = clear                         ? AttachmentLoad::Clear
                     : initialized.count(a.target) ? AttachmentLoad::Load
                                                   : AttachmentLoad::Discard;
            a.clear = {step.color[0], step.color[1], step.color[2], step.color[3]};
            a.clearDepth = step.depthValue;
            initialized.insert(a.target);
            return a;
        };
        if (step.kind == MmePortableStep::Kind::Draw || step.clearColor)
            for (const auto& name : step.colors)
                output.colors.push_back(attachment(name, step.clearColor));
        if (!step.depth.empty() &&
            ((step.kind == MmePortableStep::Kind::Draw && initialized.count(ids.at(step.depth))) ||
             step.clearDepth))
            output.depthStencil = attachment(step.depth, step.clearDepth);
        if (step.kind == MmePortableStep::Kind::Draw) {
            const auto& pass = effect.passes[step.pass];
            auto instance = effect;
            instance.parameters = step.parameters;
            const auto destination =
                std::find_if(next.targets.begin(), next.targets.end(),
                             [&](const auto& t) { return t.id == output.colors.front().target; });
            if (destination == next.targets.end())
                return fail("MME_scene_missing_destination");
            const auto drawWidth = destination->description.width,
                       drawHeight = destination->description.height;
            ModelReplayDraw draw;
            if (!(effect.legacy ? AttachLegacyBufferQuad(next, draw, drawWidth, drawHeight)
                                : AttachFullscreenTriangle(next, draw)))
                return fail("MME_post_geometry_failed");
            ReplayShader shader;
            shader.pipeline.program = PortableMmeProgram(effect, pass, reason);
            if (!shader.pipeline.program)
                return false;
            shader.pipeline.viewport = ModelViewport{0, 0, drawWidth, drawHeight, 0, 1};
            shader.pipeline.raster.cull = BatchCull::None;
            shader.pipeline.raster.depthTest = shader.pipeline.raster.depthWrite =
                shader.pipeline.raster.alphaBlend = false;
            shader.pipeline.colorFormats.clear();
            for (const auto& name : step.colors)
                shader.pipeline.colorFormats.push_back(formats.at(name));
            if (effect.legacy) {
                const auto* imported = LegacyPass(effect, pass);
                if (!imported || !LegacyRaster(*imported, shader.pipeline.raster, reason))
                    return false;
                const std::array<float, 4> dimensions{1.f / drawWidth, 1.f / drawHeight,
                                                      float(drawWidth), float(drawHeight)};
                shader.constants.push_back({0, Bytes(dimensions.data(), sizeof(dimensions))});
                for (auto& p : instance.parameters)
                    if (p.semantic == "VIEWPORTPIXELSIZE") {
                        const float dimensions[] = {float(drawWidth), float(drawHeight)};
                        if (p.rows != 1 || p.columns != 2 || p.elements != 1 ||
                            p.value.size() != 8) {
                            reason = "MME_scene_viewport_layout";
                            return false;
                        }
                        std::memcpy(p.value.data(), dimensions, 8);
                    }
            } else {
                std::array<float, 8> constants;
                std::copy(effect.multiply.begin(), effect.multiply.end(), constants.begin());
                std::copy(effect.add.begin(), effect.add.end(), constants.begin() + 4);
                shader.constants.push_back({0, Bytes(constants.data(), sizeof(constants))});
            }
            if (!BindParameters(shader, instance, reason))
                return false;
            for (unsigned slot = 0; slot < pass.sampled.size(); ++slot) {
                const auto input = ids.at(pass.sampled[slot]);
                if (std::find(output.sampled.begin(), output.sampled.end(), input) ==
                    output.sampled.end())
                    output.sampled.push_back(input);
                const auto nativeSlot = effect.legacy ? pass.sampleSlots.at(slot) : slot;
                const auto inputTarget = std::find_if(next.targets.begin(), next.targets.end(),
                                                      [&](const auto& t) { return t.id == input; });
                if (inputTarget == next.targets.end())
                    return fail("MME_scene_unknown_sample");
                shader.textures.push_back(
                    {nativeSlot,
                     {},
                     {input, inputTarget->description.width, inputTarget->description.height}});
                if (effect.legacy) {
                    const auto& s = effect.legacy->samplers.at(nativeSlot);
                    if (inputTarget->description.mips > 1 && s.mipNone)
                        return fail("MME_mipped_sampler_none_not_supported");
                    shader.samplers.push_back(
                        {nativeSlot,
                         {s.linearMin ? SamplerFilter::Linear : SamplerFilter::Point,
                          s.linearMag ? SamplerFilter::Linear : SamplerFilter::Point,
                          s.linearMip ? SamplerFilter::Linear : SamplerFilter::Point,
                          s.clampU ? SamplerAddress::Clamp : SamplerAddress::Wrap,
                          s.clampV ? SamplerAddress::Clamp : SamplerAddress::Wrap, 1}});
                    continue;
                }
                shader.samplers.push_back(
                    {slot,
                     {SamplerFilter::Point, SamplerFilter::Point, SamplerFilter::Point,
                      SamplerAddress::Clamp, SamplerAddress::Clamp, 1}});
            }
            draw.shader = std::move(shader);
            output.draws.push_back(std::move(draw));
            output.name = "MME/" + pass.technique + "/" + pass.name;
        }
        next.passes.push_back(std::move(output));
    }
    next.finalColor = ids.at("FinalColor");
    const auto valid = ValidateRenderFrame(next);
    if (!deferValidation && !valid) {
        reason = valid.reason;
        return false;
    }
    frame = std::move(next);
    return true;
}
bool AppendPortableMmePostImpl(rhi::RenderFrame& frame, const MmePortableEffect& effect,
                               std::string& reason, bool deferValidation) {
    if (effect.profile == "scene-v2" || effect.profile == "legacy-scene-v1")
        return AppendPortableScene(frame, effect, reason, deferValidation);
    if (!effect.post || effect.passes.empty() || effect.targets.empty()) {
        reason = "MME_post_pass_count";
        return false;
    }
    auto next = frame;
    const auto source = std::find_if(next.targets.begin(), next.targets.end(),
                                     [&](const auto& t) { return t.id == next.finalColor; });
    if (!next.finalColor || source == next.targets.end() ||
        source->description.format != TextureFormat::Rgba8) {
        reason = "MME_post_missing_scene_color";
        return false;
    }
    const unsigned width = source->description.width, height = source->description.height;
    std::map<std::string, std::uint64_t> targets;
    std::map<std::string, TextureFormat> formats;
    std::uint64_t id = 16;
    for (const auto& t : next.targets)
        if (t.id < (std::uint64_t(1) << 62))
            id = std::max(id, t.id + 1);
    for (const auto& t : effect.targets) {
        if (!targets.emplace(t.name, t.name == "SceneColor" ? next.finalColor : id++).second) {
            reason = "MME_duplicate_target";
            return false;
        }
        const auto format = t.format == MmePortableFormat::Rgba16Float ? TextureFormat::Rgba16Float
                                                                       : TextureFormat::Rgba8;
        formats.emplace(t.name, format);
        if (t.name != "SceneColor")
            next.targets.push_back({targets.at(t.name),
                                    {width, height, format,
                                     TextureAccess::ColorAttachment | TextureAccess::Sampled |
                                         TextureAccess::CopySource}});
    }
    if (!targets.count("SceneColor") || !targets.count("FinalColor")) {
        reason = "MME_post_missing_target";
        return false;
    }
    for (const auto& pass : effect.passes) {
        auto program = PortableMmeProgram(effect, pass, reason);
        if (!program)
            return false;
        FramePass output;
        ModelReplayDraw draw;
        if (!AttachFullscreenTriangle(next, draw)) {
            reason = "MME_post_geometry_failed";
            return false;
        }
        ReplayShader shader;
        shader.pipeline.program = program;
        shader.pipeline.viewport = ModelViewport{0, 0, width, height, 0, 1};
        shader.pipeline.raster.cull = BatchCull::None;
        shader.pipeline.raster.depthTest = shader.pipeline.raster.depthWrite =
            shader.pipeline.raster.alphaBlend = false;
        shader.pipeline.colorFormats.clear();
        for (const auto& name : pass.colors) {
            if (!targets.count(name)) {
                reason = "MME_post_missing_target";
                return false;
            }
            output.colors.push_back(FrameAttachment{targets.at(name)});
            shader.pipeline.colorFormats.push_back(formats.at(name));
        }
        std::array<float, 8> constants;
        std::copy(effect.multiply.begin(), effect.multiply.end(), constants.begin());
        std::copy(effect.add.begin(), effect.add.end(), constants.begin() + 4);
        shader.constants.push_back({0, Bytes(constants.data(), sizeof(constants))});
        if (!BindParameters(shader, effect, reason))
            return false;
        for (unsigned slot = 0; slot < pass.sampled.size(); ++slot) {
            const auto& name = pass.sampled[slot];
            if (!targets.count(name)) {
                reason = "MME_post_missing_target";
                return false;
            }
            const auto input = targets.at(name);
            output.sampled.push_back(input);
            shader.textures.push_back({slot, {}, {input, width, height}});
            shader.samplers.push_back(
                {slot,
                 {SamplerFilter::Point, SamplerFilter::Point, SamplerFilter::Point,
                  SamplerAddress::Clamp, SamplerAddress::Clamp, 1}});
        }
        draw.shader = std::move(shader);
        draw.targetTextures = {};
        draw.textures = {};
        draw.pipeline = {};
        output.name = "MME/" + pass.technique + "/" + pass.name;
        output.draws.push_back(std::move(draw));
        next.passes.push_back(std::move(output));
    }
    next.finalColor = targets.at("FinalColor");
    const auto valid = ValidateRenderFrame(next);
    if (!deferValidation && !valid) {
        reason = valid.reason;
        return false;
    }
    frame = std::move(next);
    return true;
}
bool AppendPortableMmePost(RenderFrame& frame, const MmePortableEffect& effect,
                           std::string& reason) {
    return AppendPortableMmePostImpl(frame, effect, reason, false);
}
bool AppendPortableMmePosts(RenderFrame& frame, const std::vector<MmePortableEffect>& effects,
                            std::string& reason, bool deferValidation) {
    auto next = frame;
    unsigned count = 0;
    // Native scene enter walks backwards; resume walks forwards. Wrapping in
    // ascending host order gives exactly that nesting around ScriptExternal.
    for (const auto& effect : effects)
        if (effect.post) {
            if (++count > 8) {
                reason = "MME_scene_nesting_limit";
                return false;
            }
            if (!AppendPortableMmePostImpl(next, effect, reason, true))
                return false;
        }
    const auto valid = ValidateRenderFrame(next);
    if (!deferValidation && !valid) {
        reason = valid.reason;
        return false;
    }
    frame = std::move(next);
    return true;
}
bool PrependPortableMmeTurns(RenderFrame& frame, const std::vector<MmePortableTurn>& turns,
                             const std::vector<PortableMmeDrawSource>& sources,
                             std::vector<MmePortableEffect>& completed, std::string& reason) {
    if (turns.empty())
        return true;
    auto next = frame;
    std::vector<FramePass> prefix;
    for (auto it = turns.rbegin(); it != turns.rend(); ++it) {
        const auto& turn = *it;
        MmePortableEffect declaration;
        declaration.targets = {turn.target};
        if (!RegisterPortableMmeTargets(next, {declaration}, reason) ||
            !RegisterPortableMmeTargets(next, turn.effects, reason))
            return false;
        const auto color = SharedFrameTargetId(turn.target.identity);
        const auto target = std::find_if(next.targets.begin(), next.targets.end(),
                                         [&](const auto& t) { return t.id == color; });
        if (target == next.targets.end()) {
            reason = "MME_offscreen_target_missing";
            return false;
        }
        const auto width = target->description.width, height = target->description.height;
        std::uint64_t depth = 16;
        for (const auto& t : next.targets)
            if (t.id < (std::uint64_t(1) << 62))
                depth = std::max(depth, t.id + 1);
        next.targets.push_back({depth,
                                {width, height, TextureFormat::Depth24Stencil8,
                                 unsigned(TextureAccess::DepthStencilAttachment)}});
        FramePass pass;
        pass.name = "MME/offscreen/" + turn.target.name;
        pass.color = FrameAttachment{color};
        pass.color->clear = {turn.clearColor[0], turn.clearColor[1], turn.clearColor[2],
                             turn.clearColor[3]};
        pass.depthStencil = FrameAttachment{depth};
        pass.depthStencil->clearDepth = turn.clearDepth;
        for (const auto& source : sources) {
            auto draw = source.draw;
            MmePortableEffect effect;
            const int selected = source.object
                                     ? MmePortableDraw(source.object, source.subset, source.mode,
                                                       source.flags, effect, reason, turn.id)
                                     : 0;
            if (selected < 0)
                return false;
            if (selected == 2)
                continue;
            if (selected == 0 && source.object && source.mode < 2) {
                reason = "MME_offscreen_host_pipeline_not_supported";
                return false;
            }
            draw.pipeline.viewport = ModelViewport{0, 0, width, height, 0, 1};
            if (selected == 1) {
                for (auto& t : effect.targets)
                    if (t.identity) {
                        const auto known = std::find_if(
                            next.targets.begin(), next.targets.end(), [&](const auto& item) {
                                return item.id == SharedFrameTargetId(t.identity);
                            });
                        if (known != next.targets.end()) {
                            t.width = known->description.width;
                            t.height = known->description.height;
                        }
                    }
                effect.counts[0] = source.vertexCount;
                effect.transparent = source.transparent;
                if (!ApplyPortableMmeMaterial(draw, effect, reason))
                    return false;
            }
            if (!draw.shader && (draw.pipeline.shadow.mode == BuiltinShadowMode::Material ||
                                 draw.pipeline.shadow.mode == BuiltinShadowMode::Depth)) {
                reason = "MME_offscreen_self_shadow_not_supported";
                return false;
            }
            draw.pipeline.viewport = ModelViewport{0, 0, width, height, 0, 1};
            if (draw.shader) {
                draw.shader->pipeline.viewport = draw.pipeline.viewport;
                for (const auto& b : draw.shader->textures)
                    if (b.target.target && std::find(pass.sampled.begin(), pass.sampled.end(),
                                                     b.target.target) == pass.sampled.end())
                        pass.sampled.push_back(b.target.target);
            }
            pass.draws.push_back(std::move(draw));
        }
        auto turnFrame = next;
        turnFrame.finalColor = color;
        turnFrame.passes = {std::move(pass)};
        if (!AppendPortableMmePosts(turnFrame, turn.effects, reason, true))
            return false;
        // A scene's empty RenderColorTarget restores the turn's actual output.
        if (turnFrame.finalColor != color) {
            for (auto& p : turnFrame.passes) {
                if (p.color && p.color->target == turnFrame.finalColor)
                    p.color->target = color;
                for (auto& a : p.colors)
                    if (a.target == turnFrame.finalColor)
                        a.target = color;
            }
        }
        if (turn.target.mipmapped) {
            FramePass generate;
            generate.name = "MME/mips/" + turn.target.name;
            generate.generateMips = color;
            turnFrame.passes.push_back(std::move(generate));
        }
        next.targets = std::move(turnFrame.targets);
        next.resources = std::move(turnFrame.resources);
        prefix.insert(prefix.end(), turnFrame.passes.begin(), turnFrame.passes.end());
        completed.insert(completed.end(), turn.effects.begin(), turn.effects.end());
    }
    prefix.insert(prefix.end(), next.passes.begin(), next.passes.end());
    next.passes = std::move(prefix);
    const auto valid = ValidateRenderFrame(next);
    if (!valid) {
        reason = valid.reason;
        return false;
    }
    frame = std::move(next);
    return true;
}
}
