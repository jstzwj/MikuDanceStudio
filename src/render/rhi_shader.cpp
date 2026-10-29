#include "mikudancestudio/rhi_shader.hpp"
#include <algorithm>
#include <set>

namespace mikudancestudio::rhi {
namespace {
ShaderResult Fail(const char* reason) {
    return {false, reason};
}
unsigned ValueBytes(ShaderValueType type) {
    switch (type) {
    case ShaderValueType::Float:
    case ShaderValueType::Int:
    case ShaderValueType::UInt:
        return 4;
    case ShaderValueType::Float2:
        return 8;
    case ShaderValueType::Float3:
        return 12;
    case ShaderValueType::Float4:
        return 16;
    case ShaderValueType::Matrix4x4:
        return 64;
    }
    return 0;
}
unsigned InputBytes(ShaderVertexFormat format) {
    switch (format) {
    case ShaderVertexFormat::Float2:
        return 8;
    case ShaderVertexFormat::Float3:
        return 12;
    case ShaderVertexFormat::Float4:
        return 16;
    case ShaderVertexFormat::Rgba8:
        return 4;
    }
    return 0;
}
std::string Key(const ShaderProgramDesc& d) {
    std::string key;
    const auto bytes = [&](const void* data, std::size_t size) {
        const auto n = std::uint64_t(size);
        key.append(reinterpret_cast<const char*>(&n), sizeof(n));
        if (size)
            key.append(static_cast<const char*>(data), size);
    };
    const auto value = [&](const auto& v) { bytes(&v, sizeof(v)); };
    const auto string = [&](const std::string& s) { bytes(s.data(), s.size()); };
    for (const auto* m : {&d.vertex, &d.pixel}) {
        value(m->stage);
        string(m->entry);
        string(m->hlsl);
        bytes(m->spirv.data(), m->spirv.size());
    }
    value(d.bindings.size());
    for (const auto& b : d.bindings) {
        value(b.kind);
        value(b.slot);
        value(b.stages);
        value(b.bytes);
        value(b.srgb);
        value(b.members.size());
        for (const auto& m : b.members) {
            string(m.name);
            value(m.type);
            value(m.offset);
            value(m.count);
        }
    }
    value(d.inputs.size());
    for (const auto& i : d.inputs) {
        string(i.semantic);
        value(i.semanticIndex);
        value(i.location);
        value(i.offset);
        value(i.format);
    }
    value(d.colorOutputs.size());
    for (auto c : d.colorOutputs)
        value(c);
    return key;
}
ShaderProgramDesc Seal(ShaderProgramDesc d) {
    d.vertex.spirv.Seal();
    d.pixel.spirv.Seal();
    return d;
}
}
ShaderProgram::ShaderProgram(ShaderProgramDesc d)
    : description(Seal(std::move(d))), key(Key(description)) {
}
ShaderCapabilities GraphicsShaderProfile(bool hlsl, bool spirv) {
    return {true, hlsl, spirv, 4, 4, 4, 65536};
}
std::uint64_t ShaderConstantBytes(const ShaderPipeline& p, ShaderCapabilities caps) {
    const auto alignment = std::max(1u, caps.constantAlignment);
    const auto aligned = [&](std::uint64_t n) {
        return (n + alignment - 1) / alignment * alignment;
    };
    // Native explicit APIs also bind a zero-filled constant for unused slots.
    std::uint64_t bytes = aligned(16);
    if (p.program)
        for (const auto& b : p.program->description.bindings)
            if (b.kind == ShaderBindingKind::Constants)
                bytes += aligned(b.bytes);
    return bytes;
}
std::vector<ShaderVertexInput> StandardShaderInputs(VertexLayout layout) {
    if (layout == VertexLayout::PositionColor)
        return {{"POSITION", 0, 0, 0, ShaderVertexFormat::Float3},
                {"COLOR", 0, 1, 12, ShaderVertexFormat::Rgba8}};
    if (layout == VertexLayout::PositionNormalUv)
        return {{"POSITION", 0, 0, 0, ShaderVertexFormat::Float3},
                {"NORMAL", 0, 1, 12, ShaderVertexFormat::Float3},
                {"TEXCOORD", 0, 2, 24, ShaderVertexFormat::Float2}};
    return {};
}
ModelPipeline ShaderRasterAdapter(const ShaderPipeline& p) {
    ModelPipeline state;
    state.vertexLayout = p.vertexLayout;
    state.topology = p.topology;
    state.raster = p.raster;
    state.viewport = p.viewport;
    state.colorWriteMask = p.colorWriteMask;
    constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    state.shader.world = state.shader.view = state.shader.projection = state.shader.normalView =
        identity;
    return state;
}
ShaderResult ValidateShaderPipeline(const ShaderPipeline& p, ShaderCapabilities caps) {
    if (!caps.programs)
        return Fail("shader_programs_not_supported");
    if (!p.program)
        return Fail("missing_shader_program");
    const auto& d = p.program->description;
    if (d.vertex.stage != ShaderStage::Vertex || d.pixel.stage != ShaderStage::Pixel)
        return Fail("shader_stage_mismatch");
    for (const auto* m : {&d.vertex, &d.pixel}) {
        if (m->entry.empty() || (m->hlsl.empty() && m->spirv.empty()))
            return Fail("missing_shader_code");
        if ((!caps.hlsl || m->hlsl.empty()) && (!caps.spirv || m->spirv.empty()))
            return Fail("shader_format_not_supported");
        if (!m->spirv.empty() && (m->spirv.size() < 20 || m->spirv.size() % 4))
            return Fail("invalid_spirv_size");
    }
    if (d.colorOutputs.empty() || d.colorOutputs.size() != p.colorFormats.size() ||
        d.colorOutputs.size() > caps.maxColorAttachments)
        return Fail("shader_output_layout_mismatch");
    for (std::size_t i = 0; i < d.colorOutputs.size(); ++i)
        if (!d.colorOutputs[i] || d.colorOutputs[i] > 4 ||
            (p.colorFormats[i] != TextureFormat::Rgba8 &&
             p.colorFormats[i] != TextureFormat::R32Float &&
             p.colorFormats[i] != TextureFormat::Rgba16Float))
            return Fail("shader_output_layout_mismatch");
    const auto stride = ModelVertexStride(p.vertexLayout);
    if (!stride || !ModelPrimitiveArity(p.topology) || p.colorWriteMask > 15 ||
        unsigned(p.raster.cull) > 2 || unsigned(p.raster.depthCompare) > 7 ||
        unsigned(p.raster.sourceBlend) > unsigned(BlendFactor::SourceAlphaSaturate) ||
        unsigned(p.raster.destinationBlend) > unsigned(BlendFactor::SourceAlphaSaturate))
        return Fail("shader_raster_state_not_supported");
    const auto alphaFactor = [](BlendFactor b) {
        return b != BlendFactor::SourceColor && b != BlendFactor::InvSourceColor &&
               b != BlendFactor::DestinationColor && b != BlendFactor::InvDestinationColor &&
               b != BlendFactor::SourceAlphaSaturate;
    };
    if (p.raster.alphaBlend &&
        (!alphaFactor(p.raster.sourceBlend) || !alphaFactor(p.raster.destinationBlend)))
        return Fail("unsupported_alpha_blend");
    if (d.inputs.empty() || d.inputs.size() > 16)
        return Fail("invalid_shader_inputs");
    std::set<unsigned> locations;
    std::set<std::pair<std::string, unsigned>> semantics;
    for (const auto& i : d.inputs) {
        const auto size = InputBytes(i.format);
        if (i.semantic.empty() || !size || i.location >= 16 ||
            (i.format != ShaderVertexFormat::Rgba8 && i.offset % 4) ||
            !locations.insert(i.location).second ||
            !semantics.emplace(i.semantic, i.semanticIndex).second || i.offset > stride ||
            size > stride - i.offset)
            return Fail("invalid_shader_inputs");
    }
    std::set<std::pair<ShaderBindingKind, unsigned>> slots;
    for (const auto& b : d.bindings) {
        const auto limit = b.kind == ShaderBindingKind::Constants ? caps.constantSlots
                           : b.kind == ShaderBindingKind::Texture ? caps.textureSlots
                                                                  : caps.samplerSlots;
        if (unsigned(b.kind) > unsigned(ShaderBindingKind::Sampler) || !b.stages ||
            (b.stages & ~3u) || b.slot >= limit || !slots.emplace(b.kind, b.slot).second)
            return Fail("invalid_shader_binding_layout");
        if (b.kind != ShaderBindingKind::Constants) {
            if (b.stages != unsigned(ShaderStage::Pixel) || b.bytes || !b.members.empty() ||
                (b.kind == ShaderBindingKind::Sampler && b.srgb))
                return Fail("shader_binding_usage_not_supported");
            continue;
        }
        if (!b.bytes || b.bytes % 16 || b.bytes > caps.maxConstantBytes || b.srgb)
            return Fail("invalid_constant_layout");
        std::vector<bool> used(b.bytes);
        std::set<std::string> names;
        for (const auto& m : b.members) {
            const auto size = ValueBytes(m.type);
            const unsigned step = (m.count > 1 || size > 16) ? (size + 15) / 16 * 16 : size;
            if (!size || !m.count || m.count > b.bytes / 4 || m.name.empty() ||
                !names.insert(m.name).second || m.offset % 4 ||
                ((m.count > 1 || size > 16) ? m.offset % 16 : ((m.offset % 16) + size > 16)) ||
                m.offset > b.bytes ||
                std::uint64_t(step) * (m.count - 1) + size > b.bytes - m.offset)
                return Fail("invalid_constant_layout");
            for (unsigned n = 0; n < m.count; ++n)
                for (unsigned j = 0; j < size; ++j) {
                    const auto offset = m.offset + n * step + j;
                    if (used[offset])
                        return Fail("overlapping_constant_members");
                    used[offset] = true;
                }
        }
    }
    return {true, "supported"};
}
ShaderResult ValidateShaderBindings(const ShaderPipeline& p, const ShaderResourceBindings& values,
                                    const std::function<ShaderTextureInfo(Handle)>& texture) {
    if (!p.program)
        return Fail("missing_shader_program");
    std::set<std::pair<ShaderBindingKind, unsigned>> seen;
    const auto binding = [&](ShaderBindingKind kind, unsigned slot) -> const ShaderBinding* {
        if (!seen.emplace(kind, slot).second)
            return nullptr;
        for (const auto& b : p.program->description.bindings)
            if (b.kind == kind && b.slot == slot)
                return &b;
        return nullptr;
    };
    for (const auto& c : values.constants) {
        const auto* b = binding(ShaderBindingKind::Constants, c.slot);
        if (!b || c.bytes.size() != b->bytes)
            return Fail("constant_binding_layout_mismatch");
    }
    for (const auto& t : values.textures) {
        const auto* b = binding(ShaderBindingKind::Texture, t.slot);
        if (!b)
            return Fail("texture_binding_layout_mismatch");
        const auto info = texture(t.texture);
        if (!info.valid)
            return Fail("stale_shader_texture");
        if (!info.initialized)
            return Fail("uninitialized_shader_texture");
        if (!info.sampleable)
            return Fail("shader_texture_not_declared_sampled");
        if ((info.format != TextureFormat::Rgba8 && info.format != TextureFormat::R32Float &&
             info.format != TextureFormat::Rgba16Float) ||
            info.srgb != b->srgb)
            return Fail("shader_texture_usage_mismatch");
    }
    for (const auto& s : values.samplers) {
        if (!binding(ShaderBindingKind::Sampler, s.slot))
            return Fail("sampler_binding_layout_mismatch");
        const auto& a = s.sampler;
        if (unsigned(a.min) > 2 || unsigned(a.mag) > 2 || unsigned(a.mip) > 1 ||
            unsigned(a.u) > 1 || unsigned(a.v) > 1 || a.maxAnisotropy < 1 || a.maxAnisotropy > 16)
            return Fail("shader_sampler_not_supported");
    }
    if (seen.size() != p.program->description.bindings.size())
        return Fail("missing_shader_binding");
    return {true, "bound"};
}
ShaderResult ValidateShaderAttachments(const ShaderPipeline& p,
                                       const std::vector<TextureFormat>& formats) {
    return p.colorFormats == formats ? ShaderResult{true, "supported"}
                                     : Fail("shader_attachment_layout_mismatch");
}
ShaderResult ValidateShaderTargetSupport(const Device& d, const ShaderPipeline& p) {
    const auto valid = ValidateShaderPipeline(p, d.ShaderFeatures());
    if (!valid)
        return valid;
    for (auto f : p.colorFormats) {
        const auto c = d.FormatFeatures(f);
        if (!c.render || (p.raster.alphaBlend && !c.blend))
            return Fail("shader_attachment_format_not_supported");
    }
    return {true, "supported"};
}
ShaderResult QueryShaderTextureSupport(const Device& d, const RenderTextureDesc& desc) {
    if (!d.ShaderFeatures().programs)
        return Fail("shader_programs_not_supported");
    if (desc.format == TextureFormat::Depth24Stencil8 &&
        (desc.access & unsigned(TextureAccess::Sampled)))
        return Fail("depth_sampling_not_supported");
    if (!ValidRenderTextureDesc(desc))
        return Fail("invalid_render_texture_description");
    const auto c = d.FormatFeatures(desc.format);
    if (!c.render || ((desc.access & unsigned(TextureAccess::Sampled)) && !c.sampled) ||
        ((desc.access & unsigned(TextureAccess::CopySource)) && !c.readback))
        return Fail("render_texture_format_not_supported");
    return {true, "supported"};
}
}
