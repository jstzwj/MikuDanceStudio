#include "mikudancestudio/rhi_frame_passes.hpp"
#include <algorithm>
#include <set>
namespace mikudancestudio::rhi {
namespace {
const FrameTarget* Target(const RenderFrame& f, std::uint64_t id) {
    for (const auto& t : f.targets)
        if (t.id == id)
            return &t;
    return nullptr;
}
ModelReplayStream Stream(const RenderFrame& f, const FramePass* pass) {
    ModelReplayStream s;
    s.complete = true;
    s.source = f.source;
    s.resources = f.resources;
    if (pass) {
        if (!PrimaryAttachment(*pass))
            return {};
        const auto* t = Target(f, PrimaryAttachment(*pass)->target);
        if (!t)
            return {};
        s.target.width = t->description.width;
        s.target.height = t->description.height;
    } else
        for (const auto& t : f.targets) {
            s.target.width = std::max(s.target.width, t.description.width);
            s.target.height = std::max(s.target.height, t.description.height);
        }
    s.events.push_back({1, ModelReplayOp::Clear});
    // A pass with no casters still clears/stores a valid shadow map. Global
    // material resources belong to the other passes, not this clear-only stream.
    if (pass && pass->draws.empty()) {
        s.resources.clear();
        return s;
    }
    for (const auto& r : f.resources) {
        ModelReplayEvent e;
        e.sequence = s.events.size() + 1;
        e.op = ModelReplayOp::PublishResource;
        e.resource = r.key;
        s.events.push_back(e);
    }
    for (const auto& p : f.passes) {
        if (p.generateMips)
            continue;
        if (pass && &p != pass)
            continue;
        if (!PrimaryAttachment(p))
            return {};
        const auto* target = Target(f, PrimaryAttachment(p)->target);
        if (!target)
            return {};
        for (const auto& d : p.draws) {
            ModelReplayEvent e;
            e.sequence = s.events.size() + 1;
            e.op = ModelReplayOp::Draw;
            e.draw = d;
            if (!e.draw.pipeline.viewport)
                e.draw.pipeline.viewport = ModelViewport{
                    0, 0, target->description.width, target->description.height, 0, 1};
            if (e.draw.shader && !e.draw.shader->pipeline.viewport)
                e.draw.shader->pipeline.viewport = ModelViewport{
                    0, 0, target->description.width, target->description.height, 0, 1};
            s.events.push_back(e);
        }
    }
    return s;
}
}
ModelReplayStream FlattenRenderFrame(const RenderFrame& f) {
    return Stream(f, nullptr);
}
ModelReplayResult ValidateRenderFrame(const RenderFrame& f) {
    const auto fail = [](const char* why) { return ModelReplayResult{false, 0, 0, why}; };
    if (f.targets.empty() || f.targets.size() > 64 || f.passes.empty() || f.passes.size() > 64)
        return fail("invalid_frame_target_or_pass_count");
    std::map<std::uint64_t, Handle> handles;
    RenderPassState state;
    for (const auto& t : f.targets) {
        Handle h{std::uint32_t(handles.size()), 1, 1};
        if (!t.id || !handles.emplace(t.id, h).second || !state.Register(h, t.description))
            return fail("invalid_or_duplicate_frame_target");
        if (t.history) {
            if (t.description.mips != 1 || t.description.format != TextureFormat::Rgba8 ||
                !(t.description.access & unsigned(TextureAccess::Sampled)))
                return fail("invalid_history_target");
            RenderPassDesc initialize;
            initialize.color = PassAttachment{h};
            initialize.color->clear = *t.history;
            if (!state.Begin(initialize) || !state.End())
                return fail("invalid_history_clear");
        }
    }
    std::set<std::uint64_t> resources;
    for (const auto& r : f.resources)
        if (!resources.insert(r.key.id).second)
            return fail("multiple_frame_resource_versions");
    std::size_t draws = 0;
    for (const auto& p : f.passes) {
        if (p.generateMips) {
            const auto target = handles.find(p.generateMips);
            if (p.color || p.depthStencil || !p.colors.empty() || !p.draws.empty() ||
                !p.sampled.empty() || target == handles.end() ||
                !state.GenerateMips(target->second))
                return fail("invalid_frame_mip_generation");
            continue;
        }
        RenderPassDesc native;
        const auto attachment = [&](const std::optional<FrameAttachment>& a,
                                    std::optional<PassAttachment>& out) {
            if (!a)
                return true;
            const auto i = handles.find(a->target);
            if (i == handles.end())
                return false;
            if (Target(f, a->target)->history && a->store != AttachmentStore::Store)
                return false;
            out = PassAttachment{i->second, a->load,       a->store,
                                 a->clear,  a->clearDepth, a->clearStencil};
            return true;
        };
        if (!attachment(p.color, native.color) || !attachment(p.depthStencil, native.depthStencil))
            return fail("unknown_frame_attachment");
        for (const auto& a : p.colors) {
            std::optional<PassAttachment> out;
            if (!attachment(a, out))
                return fail("unknown_frame_attachment");
            native.colors.push_back(*out);
        }
        for (auto id : p.sampled) {
            const auto i = handles.find(id);
            if (i == handles.end())
                return fail("unknown_frame_sample");
            native.sampled.push_back(i->second);
        }
        if (!state.Begin(native))
            return {false, 0, draws, state.Reason()};
        for (const auto& d : p.draws) {
            if (d.shader) {
                const auto layout =
                    ValidateShaderAttachments(d.shader->pipeline, state.ColorFormats());
                if (!layout)
                    return fail(layout.reason.c_str());
                const auto& raster = d.shader->pipeline.raster;
                if ((raster.depthTest || raster.depthWrite) && !p.depthStencil)
                    return fail("draw_requires_depth_attachment");
                for (const auto& binding : d.shader->textures) {
                    const auto& ref = binding.target;
                    if (!ref.target)
                        continue;
                    const auto* t = Target(f, ref.target);
                    if (!t ||
                        (t->description.format != TextureFormat::Rgba8 &&
                         t->description.format != TextureFormat::R32Float &&
                         t->description.format != TextureFormat::Rgba16Float) ||
                        t->description.width != ref.width || t->description.height != ref.height ||
                        !state.CanSample(handles.at(t->id)))
                        return fail("invalid_or_undeclared_target_texture");
                }
                continue;
            }
            if (ColorAttachments(p).size() != 1)
                return fail("legacy_draw_requires_single_color");
            for (const auto& ref : d.targetTextures)
                if (ref.target) {
                    const auto* t = Target(f, ref.target);
                    if (!t || t->description.format != TextureFormat::Rgba8 ||
                        t->description.width != ref.width || t->description.height != ref.height ||
                        !state.CanSample(handles.at(t->id)))
                        return fail("invalid_or_undeclared_target_texture");
                }
            if (d.pipeline.postProcess.mode != PostProcessMode::None &&
                Target(f, PrimaryAttachment(p)->target)->description.format != TextureFormat::Rgba8)
                return fail("postprocess_requires_rgba8");
            if ((d.pipeline.raster.depthTest || d.pipeline.stencil) && !p.depthStencil)
                return fail("draw_requires_depth_attachment");
            if (d.pipeline.shadow.mode == BuiltinShadowMode::Depth &&
                Target(f, PrimaryAttachment(p)->target)->description.format !=
                    TextureFormat::R32Float)
                return fail("shadow_depth_requires_r32_target");
            if (d.pipeline.shadow.mode == BuiltinShadowMode::Material) {
                const auto* t = Target(f, d.shadowTarget);
                if (!t || t->description.format != TextureFormat::R32Float ||
                    !state.CanSample(handles.at(t->id)))
                    return fail("undeclared_shadow_target");
            } else if (d.shadowTarget)
                return fail("unexpected_shadow_target");
        }
        const auto result = detail::ValidateFrameReplay(Stream(f, &p));
        if (!result)
            return result;
        draws += result.draws;
        state.End();
    }
    if (f.finalColor) {
        const auto* t = Target(f, f.finalColor);
        if (!t || t->description.mips != 1 || t->description.format != TextureFormat::Rgba8 ||
            !state.CanRead(handles.at(t->id)))
            return fail("invalid_final_color");
    }
    if (!draws && !f.resources.empty())
        return fail("unused_frame_resources");
    return {true, 0, draws, "valid_frame"};
}
}
