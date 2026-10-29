#include "mikudancestudio/rhi_model_replay.hpp"
#include <cmath>
#include <cstring>
#include <map>
#include <set>
namespace mikudancestudio::rhi {
static ModelReplayResult ValidateReplay(const ModelReplayStream& stream, bool frameTargets) {
    ModelReplayResult out;
    auto fail = [&](const char* why) {
        out.reason = why;
        return out;
    };
    const auto& t = stream.target;
    if (!stream.complete)
        return fail("incomplete_stream");
    if (!ValidHistoryDesc(t) || t.width > 4096 || t.height > 4096 || t.samples != 1 ||
        t.representation != HistoryRepresentation::SingleSample || !t.color || !t.depth ||
        !t.stencil)
        return fail("unsupported_target");
    for (float v : {stream.clear.red, stream.clear.green, stream.clear.blue, stream.clear.alpha})
        if (!std::isfinite(v) || v < 0 || v > 1)
            return fail("invalid_clear");
    if (stream.initializedRegion &&
        !ValidModelViewport(*stream.initializedRegion, t.width, t.height))
        return fail("invalid_initialized_region");
    if (stream.events.empty() || stream.events[0].op != ModelReplayOp::Clear)
        return fail("missing_initial_clear");
    std::map<ReplayResourceKey, const ModelReplayResource*> snapshots;
    for (const auto& r : stream.resources) {
        if (!r.key.id || !r.key.version || !snapshots.emplace(r.key, &r).second)
            return fail("duplicate_or_invalid_resource_key");
        if (r.kind == ReplayResourceKind::Texture) {
            if (!r.bytes.empty() || r.texture.levels.empty() ||
                (r.texture.generateMips && r.texture.levels.size() != 1))
                return fail("invalid_texture");
            unsigned w = r.texture.levels[0].width, h = r.texture.levels[0].height;
            if (!w || !h || w > 16384 || h > 16384)
                return fail("invalid_texture_size");
            for (std::size_t i = 0; i < r.texture.levels.size(); ++i) {
                const auto& level = r.texture.levels[i];
                if (level.width != w || level.height != h ||
                    level.pixels.size() != std::uint64_t(w) * h * 4)
                    return fail("invalid_mip_data");
                if (w == 1 && h == 1 && i + 1 < r.texture.levels.size())
                    return fail("excess_mips");
                w = w > 1 ? w / 2 : 1;
                h = h > 1 ? h / 2 : 1;
            }
        } else {
            if (!r.texture.levels.empty() || r.bytes.empty() || r.bytes.size() > 0xffffffffu)
                return fail("invalid_buffer");
            if (r.kind == ReplayResourceKind::Vertices) {
                const auto stride = ModelVertexStride(r.vertexLayout);
                if (!stride || r.bytes.size() % stride)
                    return fail("invalid_vertex_layout");
            } else if (r.kind == ReplayResourceKind::Indices) {
                if ((r.indexFormat != IndexFormat::UInt16 &&
                     r.indexFormat != IndexFormat::UInt32) ||
                    r.bytes.size() % (r.indexFormat == IndexFormat::UInt16 ? 2 : 4))
                    return fail("invalid_index_format");
            } else
                return fail("unsupported_resource_kind");
        }
    }
    std::map<std::uint64_t, ReplayResourceKey> active;
    auto get = [&](ReplayResourceKey k, ReplayResourceKind kind) -> const ModelReplayResource* {
        auto a = active.find(k.id);
        auto r = snapshots.find(k);
        return a != active.end() && a->second.version == k.version && r != snapshots.end() &&
                       r->second->kind == kind
                   ? r->second
                   : nullptr;
    };
    for (std::size_t e = 0; e < stream.events.size(); ++e) {
        const auto& event = stream.events[e];
        out.sequence = e + 1;
        if (event.sequence != e + 1)
            return fail("non_contiguous_sequence");
        if (event.op == ModelReplayOp::Clear) {
            if (e)
                return fail("unsupported_additional_clear");
            continue;
        }
        if (event.op == ModelReplayOp::PublishResource) {
            auto r = snapshots.find(event.resource);
            if (r == snapshots.end())
                return fail("missing_snapshot");
            auto old = active.find(event.resource.id);
            if (old != active.end()) {
                if (old->second.version >= event.resource.version)
                    return fail("non_increasing_resource_version");
                if (snapshots.at(old->second)->kind != r->second->kind)
                    return fail("resource_kind_changed");
            }
            active[event.resource.id] = event.resource;
            continue;
        }
        if (event.op != ModelReplayOp::Draw)
            return fail("unsupported_event");
        const auto& d = event.draw;
        const auto p = d.shader ? ShaderRasterAdapter(d.shader->pipeline) : d.pipeline;
        if (d.shader) {
            const auto check =
                ValidateShaderPipeline(d.shader->pipeline, GraphicsShaderProfile(true, true));
            if (!check)
                return fail(check.reason.c_str());
            if (d.shadowTarget)
                return fail("mixed_shader_bindings");
            for (unsigned slot = 0; slot < 3; ++slot)
                if (d.textures[slot].id || d.textures[slot].version ||
                    d.targetTextures[slot].target || d.targetTextures[slot].width ||
                    d.targetTextures[slot].height)
                    return fail("mixed_shader_bindings");
            ShaderResourceBindings bindings;
            bindings.constants = d.shader->constants;
            bindings.samplers = d.shader->samplers;
            std::vector<ShaderTextureInfo> infos;
            for (const auto& texture : d.shader->textures) {
                const auto& ref = texture.target;
                ShaderTextureInfo info;
                if (ref.target || ref.width || ref.height) {
                    if (!frameTargets)
                        return fail("render_target_requires_frame");
                    if (!ref.target || !ref.width || !ref.height || texture.texture.id ||
                        texture.texture.version)
                        return fail("invalid_target_texture_binding");
                    info = {true, true, true, false, TextureFormat::Rgba8};
                } else {
                    const auto* r = get(texture.texture, ReplayResourceKind::Texture);
                    if (r)
                        info = {true, true, true, r->texture.srgb, TextureFormat::Rgba8};
                }
                bindings.textures.push_back({texture.slot, {unsigned(infos.size()), 1, 1}});
                infos.push_back(info);
            }
            const auto bound = ValidateShaderBindings(d.shader->pipeline, bindings, [&](Handle h) {
                return h.index < infos.size() ? infos[h.index] : ShaderTextureInfo{};
            });
            if (!bound)
                return fail(bound.reason.c_str());
        }
        if (!ValidPostProcess(p))
            return fail("invalid_postprocess_pipeline");
        if (!ValidBuiltinShadowState(p.shadow) ||
            (p.shadow.mode != BuiltinShadowMode::None &&
             (p.vertexLayout != BuiltinShadowVertexLayout(p.shadow.mode) ||
              p.topology != PrimitiveTopology::TriangleList)))
            return fail("invalid_builtin_shadow_pipeline");
        const auto& s = p.shader;
        const auto stride = ModelVertexStride(p.vertexLayout);
        if (!stride)
            return fail("unsupported_vertex_layout");
        const auto arity = ModelPrimitiveArity(p.topology);
        if (!arity)
            return fail("unsupported_topology");
        const auto viewport = p.viewport.value_or(ModelViewport{0, 0, t.width, t.height, 0, 1});
        if (!ValidModelViewport(viewport, t.width, t.height))
            return fail("invalid_viewport");
        if (stream.initializedRegion && !ContainsModelViewport(*stream.initializedRegion, viewport))
            return fail("draw_outside_initialized_region");
        std::array<float, 4> color;
        std::array<std::array<float, 4>, 3> samples{};
        if (!ValidateMaterialShaderInputs(s) ||
            !EvaluateMaterialTextureCascade(s.layout, {1, 1, 1, 1}, samples, color))
            return fail("unsupported_shader_inputs");
        if (unsigned(p.raster.depthCompare) > 7 || unsigned(p.raster.cull) > 2 ||
            unsigned(p.raster.sourceBlend) > 10 || unsigned(p.raster.destinationBlend) > 10 ||
            unsigned(p.sourceAlpha) > 10 || unsigned(p.destinationAlpha) > 10 ||
            unsigned(p.colorOperation) < 1 || unsigned(p.colorOperation) > 5 ||
            unsigned(p.alphaOperation) < 1 || unsigned(p.alphaOperation) > 5 ||
            p.colorWriteMask > 15 || unsigned(p.stencilCompare) > 7 ||
            unsigned(p.stencilFail) < 1 || unsigned(p.stencilFail) > 8 ||
            unsigned(p.stencilDepthFail) < 1 || unsigned(p.stencilDepthFail) > 8 ||
            unsigned(p.stencilPass) < 1 || unsigned(p.stencilPass) > 8)
            return fail("unsupported_pipeline");
        // D3D11 alpha factors cannot name RGB channels or source-alpha-saturate.
        auto alphaFactor = [](BlendFactor b) {
            return b != BlendFactor::SourceColor && b != BlendFactor::InvSourceColor &&
                   b != BlendFactor::DestinationColor && b != BlendFactor::InvDestinationColor &&
                   b != BlendFactor::SourceAlphaSaturate;
        };
        if (p.raster.alphaBlend &&
            (!alphaFactor(p.separateAlpha ? p.sourceAlpha : p.raster.sourceBlend) ||
             !alphaFactor(p.separateAlpha ? p.destinationAlpha : p.raster.destinationBlend)))
            return fail("unsupported_alpha_blend");
        auto v = get(d.vertices, ReplayResourceKind::Vertices);
        if (!v)
            return fail("missing_or_stale_geometry");
        if (v->vertexLayout != p.vertexLayout)
            return fail("vertex_layout_mismatch");
        if (d.indexed) {
            auto i = get(d.indices, ReplayResourceKind::Indices);
            if (!i)
                return fail("missing_or_stale_geometry");
            if (d.firstVertex || d.drawVertexCount)
                return fail("mixed_draw_ranges");
            const unsigned step = i->indexFormat == IndexFormat::UInt16 ? 2 : 4;
            if (!d.vertexCount || d.vertexCount > v->bytes.size() / stride || !d.indexCount ||
                d.indexCount % arity || d.firstIndex > i->bytes.size() / step ||
                d.indexCount > i->bytes.size() / step - d.firstIndex)
                return fail("invalid_draw_range");
            if (!i->bytes.IndicesFit(d.firstIndex, d.indexCount, step, d.vertexCount))
                return fail("index_out_of_range");
        } else {
            if (d.indices.id || d.indices.version || d.firstIndex || d.indexCount || d.vertexCount)
                return fail("mixed_draw_ranges");
            if (!d.drawVertexCount || d.drawVertexCount % arity ||
                d.firstVertex > v->bytes.size() / stride ||
                d.drawVertexCount > v->bytes.size() / stride - d.firstVertex)
                return fail("invalid_draw_range");
        }
        for (unsigned slot = 0; slot < 3; ++slot) {
            const auto& target = d.targetTextures[slot];
            if (target.target || target.width || target.height) {
                if (!frameTargets)
                    return fail("render_target_requires_frame");
                if (!target.target || !target.width || !target.height || d.textures[slot].id ||
                    d.textures[slot].version ||
                    s.layout.stages[slot].role == MaterialTextureRole::None || s.srgbSample[slot])
                    return fail("invalid_target_texture_binding");
            }
            if (s.layout.stages[slot].role != MaterialTextureRole::None) {
                const auto& sampler = s.samplers[slot];
                if (p.vertexLayout == VertexLayout::PositionColor &&
                    s.layout.stages[slot].coordinates != MaterialCoordinates::MeshUv0)
                    return fail("position_color_has_no_normal_coordinates");
                if (s.layout.stages[slot].coordinates == MaterialCoordinates::MeshUv1 ||
                    sampler.maxAnisotropy > 16 ||
                    (sampler.mip == SamplerFilter::Anisotropic &&
                     sampler.min != SamplerFilter::Anisotropic &&
                     sampler.mag != SamplerFilter::Anisotropic))
                    return fail("unsupported_sampling");
                auto texture =
                    target.target ? nullptr : get(d.textures[slot], ReplayResourceKind::Texture);
                if (!target.target && (!texture || texture->texture.srgb != s.srgbSample[slot]))
                    return fail("missing_stale_or_mismatched_texture");
            }
        }
        ++out.draws;
    }
    // A deliberately empty scene is one clear and no resources. Do not admit
    // an accidentally truncated draw stream that still carries publications.
    if (!out.draws && (stream.events.size() != 1 || !stream.resources.empty()))
        return fail("empty_draw_stream");
    out.success = true;
    out.reason = "validated";
    return out;
}
ModelReplayResult ValidateModelReplay(const ModelReplayStream& stream) {
    return ValidateReplay(stream, false);
}
ModelReplayResult detail::ValidateFrameReplay(const ModelReplayStream& stream) {
    return ValidateReplay(stream, true);
}
ModelReplayResult ComposeModelReplay(const ModelReplayStream& prefix,
                                     const ModelReplayStream& suffix, ModelReplayStream& output) {
    auto result = ValidateModelReplay(prefix);
    if (!result)
        return result;
    result = ValidateModelReplay(suffix);
    if (!result)
        return result;
    const auto& a = prefix.target;
    const auto& b = suffix.target;
    if (a.width != b.width || a.height != b.height || a.samples != b.samples ||
        a.quality != b.quality || a.representation != b.representation || a.resolve != b.resolve ||
        a.color != b.color || a.depth != b.depth || a.stencil != b.stencil ||
        a.samplePattern != b.samplePattern)
        return {false, 0, 0, "history_target_mismatch"};
    if (prefix.source.scene != suffix.source.scene || prefix.source.frame != suffix.source.frame)
        return {false, 0, 0, "history_scene_or_frame_mismatch"};
    // Suffix's initial clear is deliberately discarded: preceding depth and
    // stencil must be rebuilt from commands, never a resolved color image.
    auto composed = prefix;
    std::set<std::uint64_t> occupied;
    std::map<std::uint64_t, std::uint64_t> ids;
    for (const auto& resource : prefix.resources)
        occupied.insert(resource.key.id);
    for (const auto& resource : suffix.resources)
        ids.emplace(resource.key.id, 0);
    std::uint64_t candidate = 1;
    for (auto& id : ids) {
        while (occupied.count(candidate)) {
            if (candidate == UINT64_MAX)
                return {false, 0, 0, "history_resource_ids_exhausted"};
            ++candidate;
        }
        id.second = candidate;
        occupied.insert(candidate);
    }
    const auto remap = [&](ReplayResourceKey key) {
        if (key.id) {
            const auto found = ids.find(key.id);
            if (found == ids.end())
                return ReplayResourceKey{};
            key.id = found->second;
        }
        return key;
    };
    for (auto resource : suffix.resources) {
        resource.key = remap(resource.key);
        composed.resources.push_back(std::move(resource));
    }
    for (std::size_t n = 1; n < suffix.events.size(); ++n) {
        auto event = suffix.events[n];
        event.sequence = composed.events.size() + 1;
        if (event.op == ModelReplayOp::PublishResource)
            event.resource = remap(event.resource);
        else if (event.op == ModelReplayOp::Draw) {
            auto& d = event.draw;
            d.vertices = remap(d.vertices);
            d.indices = remap(d.indices);
            for (auto& key : d.textures)
                key = remap(key);
            if (d.shader)
                for (auto& texture : d.shader->textures)
                    texture.texture = remap(texture.texture);
            // A suffix default viewport refers to the same target. The final
            // validator rejects any draw outside the prefix's known region.
        }
        composed.events.push_back(std::move(event));
    }
    result = ValidateModelReplay(composed);
    if (result)
        output = std::move(composed);
    return result;
}
}
