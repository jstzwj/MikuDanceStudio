#include "mikudancestudio/rhi_scene_renderer.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <variant>

namespace mikudancestudio::rhi {
namespace {
bool Equal(const ModelReplayResource& a, const ModelReplayResource& b) {
    if (a.kind != b.kind || a.vertexLayout != b.vertexLayout || a.indexFormat != b.indexFormat ||
        a.bytes != b.bytes || a.texture.srgb != b.texture.srgb ||
        a.texture.generateMips != b.texture.generateMips ||
        a.texture.levels.size() != b.texture.levels.size())
        return false;
    for (std::size_t n = 0; n < a.texture.levels.size(); ++n) {
        const auto& x = a.texture.levels[n];
        const auto& y = b.texture.levels[n];
        if (x.width != y.width || x.height != y.height || x.pixels != y.pixels)
            return false;
    }
    return true;
}
std::uint64_t Bytes(const ModelReplayResource& r) {
    std::uint64_t size = r.bytes.size();
    for (const auto& m : r.texture.levels)
        size += m.pixels.size();
    return size;
}
struct Draw {
    ModelPipeline pipeline;
    std::array<Handle, 3> textures{};
    std::variant<IndexedDraw, NonIndexedDraw> command;
    std::uint64_t sequence;
    Handle shadow;
};
struct PreparedPass {
    RenderPassDesc description;
    std::size_t firstDraw = 0, draws = 0;
};
}
struct PreparedModelFrame::Data {
    std::uint64_t owner = 0, scene = 0, generation = 0;
    SubmissionIdentity submission;
    ClearColor clear;
    std::vector<Draw> draws;
    std::vector<PreparedPass> passes;
    Handle finalColor;
};
PreparedModelFrame::PreparedModelFrame() = default;
PreparedModelFrame::~PreparedModelFrame() = default;
PreparedModelFrame::PreparedModelFrame(PreparedModelFrame&&) noexcept = default;
PreparedModelFrame& PreparedModelFrame::operator=(PreparedModelFrame&&) noexcept = default;
struct SceneRenderer::Impl {
    struct Entry {
        ModelReplayResource cpu;
        Handle handle;
        CompletionToken lastUse;
    };
    struct Scene {
        std::uint64_t generation = 0;
        std::map<ReplayResourceKey, Entry> resources;
        std::map<std::uint64_t, std::uint64_t> versions;
        std::map<std::uint64_t, ReplayResourceKind> kinds;
        struct Target {
            RenderTextureDesc desc;
            Handle handle;
            CompletionToken lastUse;
        };
        std::map<std::uint64_t, Target> targets;
    };
    struct Retired {
        Handle handle;
        CompletionToken completion;
    };
    Device* device;
    const std::uint64_t owner = detail::NextDeviceIdentity();
    std::uint64_t generation = 0;
    bool unknownWork = false;
    std::map<std::uint64_t, Scene> scenes;
    std::vector<Retired> retired;
    SceneRendererStats stats;
    explicit Impl(Device& d) : device(&d) {}
    void Retire(Entry& e) {
        if (e.handle)
            retired.push_back({e.handle, e.lastUse});
        e.handle = {};
    }
    Handle Upload(const ModelReplayResource& r) {
        if (r.kind == ReplayResourceKind::Texture)
            return device->CreateModelTexture(r.texture);
        BufferDesc desc{r.kind == ReplayResourceKind::Vertices ? BufferUsage::Vertex
                                                               : BufferUsage::Index,
                        r.bytes.size()};
        desc.indexFormat = r.indexFormat;
        return device->CreateBuffer(desc, r.bytes.data());
    }
    bool Drain(std::uint32_t timeout) {
        if (!device)
            return true;
        // Confirmed native removal terminates old-device execution. A timeout
        // or reset-pending device is not equivalent and cannot bypass fences.
        if (device->Status().state == DeviceState::Removed)
            return true;
        if (unknownWork)
            return false;
        for (auto& s : scenes)
            for (auto& t : s.second.targets)
                if (t.second.lastUse && !device->WaitCompletion(t.second.lastUse, timeout))
                    return false;
        for (auto& s : scenes)
            for (auto& r : s.second.resources)
                if (r.second.lastUse && !device->WaitCompletion(r.second.lastUse, timeout))
                    return false;
        for (const auto& r : retired)
            if (r.completion && !device->WaitCompletion(r.completion, timeout))
                return false;
        return true;
    }
};
SceneRenderer::SceneRenderer(Device& d) : impl(std::make_unique<Impl>(d)) {
}
SceneRenderer::~SceneRenderer() {
    Detach();
}
ModelReplayResult SceneRenderer::Prepare(std::uint64_t id, const ModelReplayStream& stream,
                                         PreparedModelFrame& out, FrameOutput output) {
    for (const auto& e : stream.events)
        if (e.op == ModelReplayOp::Draw &&
            (e.draw.shadowTarget || e.draw.pipeline.shadow.mode != BuiltinShadowMode::None))
            return {false, e.sequence, 0, "shadow_requires_explicit_passes"};
    return PrepareImpl(id, stream, out, output, nullptr);
}
ModelReplayResult SceneRenderer::Prepare(std::uint64_t id, const RenderFrame& frame,
                                         PreparedModelFrame& out, FrameOutput output) {
    const auto valid = ValidateRenderFrame(frame);
    if (!valid)
        return valid;
    if (impl->device && !impl->device->Features().textureRenderTargets)
        return {false, 0, 0, "explicit_passes_unsupported"};
    if (output == FrameOutput::Presentation && !frame.finalColor)
        return {false, 0, 0, "missing_final_color"};
    return PrepareImpl(id, FlattenRenderFrame(frame), out, output, &frame);
}
ModelReplayResult SceneRenderer::PrepareImpl(std::uint64_t id, const ModelReplayStream& stream,
                                             PreparedModelFrame& out, FrameOutput output,
                                             const RenderFrame* frame) {
    if (output != FrameOutput::Offscreen && output != FrameOutput::Presentation)
        return {false, 0, 0, "invalid_frame_output"};
    auto result = frame ? detail::ValidateFrameReplay(stream) : ValidateModelReplay(stream);
    if (!result)
        return result;
    if (!id)
        return {false, 0, 0, "invalid_scene_namespace"};
    if (impl->unknownWork)
        return {false, 0, 0, "untracked_gpu_work"};
    if (!impl->device)
        return {false, 0, 0, "device_not_bound"};
    auto& d = *impl->device;
    if (d.Status().state != DeviceState::Ready)
        return {false, 0, 0, "device_not_ready"};
    const auto caps = d.ModelFeatures();
    if (!caps.supported ||
        (output == FrameOutput::Offscreen ? !caps.offscreen : !caps.presentation))
        return {false, 0, 0, "unsupported_frame_output"};
    if (result.draws > caps.maxDraws)
        return {false, 0, 0, "backend_draw_limit"};
    for (const auto& e : stream.events)
        if (e.op == ModelReplayOp::Draw) {
            const auto& draw = e.draw;
            if ((draw.indexed ? !caps.indexed : !caps.nonIndexed) ||
                (draw.pipeline.topology == PrimitiveTopology::LineList && !caps.lines) ||
                (draw.pipeline.vertexLayout == VertexLayout::PositionColor
                     ? !caps.positionColor
                     : !caps.positionNormalUv))
                return {false, e.sequence, 0, "unsupported_draw_contract"};
        }
    for (const auto& r : stream.resources)
        if (r.kind == ReplayResourceKind::Texture &&
            ((r.texture.generateMips && !caps.generateMips) ||
             (r.texture.levels.size() > 1 && !caps.explicitMips) || (r.texture.srgb && !caps.srgb)))
            return {false, 0, 0, "unsupported_texture_contract"};
    auto existing = impl->scenes.find(id);
    // Validate across frames before any upload, target change or cache mutation.
    if (existing != impl->scenes.end())
        for (const auto& r : stream.resources) {
            const auto& s = existing->second;
            const auto found = s.resources.find(r.key);
            const auto kind = s.kinds.find(r.key.id);
            if (kind != s.kinds.end() && kind->second != r.kind)
                return {false, 0, 0, "resource_kind_changed"};
            if (found != s.resources.end()) {
                if (!Equal(found->second.cpu, r))
                    return {false, 0, 0, "immutable_version_content_changed"};
            } else {
                const auto v = s.versions.find(r.key.id);
                if (v != s.versions.end() && r.key.version <= v->second)
                    return {false, 0, 0, "retired_resource_version"};
            }
        }
    std::map<ReplayResourceKey, Handle> pending;
    struct Rollback {
        Device& device;
        std::map<ReplayResourceKey, Handle>& handles;
        ~Rollback() {
            for (const auto& h : handles)
                device.Destroy(h.second);
        }
    } rollback{d, pending};
    for (const auto& r : stream.resources) {
        const bool missing = existing == impl->scenes.end() ||
                             !existing->second.resources.count(r.key) ||
                             !existing->second.resources.at(r.key).handle;
        if (missing) {
            const auto h = impl->Upload(r);
            if (!h)
                return {false, 0, 0, "resource_upload_failed"};
            pending.emplace(r.key, h);
        }
    }
    const auto before = d.Status();
    auto outputTarget = stream.target;
    if (frame && frame->finalColor)
        for (const auto& t : frame->targets)
            if (t.id == frame->finalColor) {
                outputTarget.width = t.description.width;
                outputTarget.height = t.description.height;
            }
    if (!(output == FrameOutput::Offscreen ? d.EnsureModelTarget(outputTarget)
                                           : d.EnsurePresentationTarget(outputTarget)))
        return {false, 0, 0, "target_prepare_failed"};
    if (d.Status().epoch != before.epoch)
        ++impl->stats.targetRebuilds;
    // Allocate targets only after legacy output preparation, which can reset
    // the target epoch. New allocations are rolled back on partial failure.
    std::map<ReplayResourceKey, Handle> targetAllocations;
    Rollback targetRollback{d, targetAllocations};
    if (frame)
        for (const auto& t : frame->targets) {
            bool missing = true;
            if (existing != impl->scenes.end()) {
                const auto old = existing->second.targets.find(t.id);
                missing = old == existing->second.targets.end() || !old->second.handle ||
                          !EqualRenderTextureDesc(old->second.desc, t.description);
            }
            if (missing) {
                const auto h = d.CreateRenderTexture(t.description);
                if (!h)
                    return {false, 0, 0, "render_target_allocation_failed"};
                targetAllocations.emplace(ReplayResourceKey{t.id, 1}, h);
            }
        }
    auto& scene = impl->scenes[id];
    std::set<std::uint64_t> wantedTargets;
    if (frame)
        for (const auto& t : frame->targets) {
            wantedTargets.insert(t.id);
            auto allocation = targetAllocations.find({t.id, 1});
            if (allocation != targetAllocations.end()) {
                auto& old = scene.targets[t.id];
                if (old.handle)
                    impl->retired.push_back({old.handle, old.lastUse});
                old = {t.description, allocation->second, {}};
                targetAllocations.erase(allocation);
            }
        }
    for (auto i = scene.targets.begin(); i != scene.targets.end();)
        if (!wantedTargets.count(i->first)) {
            if (i->second.handle)
                impl->retired.push_back({i->second.handle, i->second.lastUse});
            i = scene.targets.erase(i);
        } else
            ++i;
    std::set<ReplayResourceKey> wanted;
    for (const auto& r : stream.resources) {
        wanted.insert(r.key);
        auto p = pending.find(r.key);
        auto found = scene.resources.find(r.key);
        if (found == scene.resources.end())
            found = scene.resources.emplace(r.key, Impl::Entry{r, {}, {}}).first;
        if (p != pending.end()) {
            found->second.handle = p->second;
            ++impl->stats.uploads;
            impl->stats.uploadBytes += Bytes(r);
            impl->stats.textureUploads += r.kind == ReplayResourceKind::Texture;
            pending.erase(p);
        }
        scene.versions[r.key.id] = (std::max)(scene.versions[r.key.id], r.key.version);
    }
    for (const auto& r : stream.resources)
        scene.kinds[r.key.id] = r.kind;
    for (auto i = scene.resources.begin(); i != scene.resources.end();)
        if (!wanted.count(i->first)) {
            impl->Retire(i->second);
            i = scene.resources.erase(i);
        } else
            ++i;
    auto data = std::make_unique<PreparedModelFrame::Data>();
    data->owner = impl->owner;
    data->scene = id;
    scene.generation = ++impl->generation;
    data->generation = scene.generation;
    const auto status = d.Status();
    data->submission = {status.identity, status.epoch};
    data->clear = stream.clear;
    if (frame && frame->finalColor)
        data->finalColor = scene.targets.at(frame->finalColor).handle;
    for (const auto& e : stream.events)
        if (e.op == ModelReplayOp::Draw) {
            const auto& draw = e.draw;
            Draw prepared;
            prepared.pipeline = draw.pipeline;
            if (draw.shadowTarget)
                prepared.shadow = scene.targets.at(draw.shadowTarget).handle;
            prepared.sequence = e.sequence;
            for (unsigned slot = 0; slot < 3; ++slot)
                if (draw.pipeline.shader.layout.stages[slot].role != MaterialTextureRole::None)
                    prepared.textures[slot] =
                        draw.targetTextures[slot].target
                            ? scene.targets.at(draw.targetTextures[slot].target).handle
                            : scene.resources.at(draw.textures[slot]).handle;
            const auto vb = scene.resources.at(draw.vertices).handle;
            if (draw.indexed)
                prepared.command = d.Record(
                    IndexedDraw{vb, scene.resources.at(draw.indices).handle, draw.firstIndex,
                                draw.indexCount, draw.vertexCount, draw.pipeline.vertexLayout});
            else {
                NonIndexedDraw cmd{vb, draw.firstVertex, draw.drawVertexCount};
                cmd.layout = draw.pipeline.vertexLayout;
                prepared.command = d.Record(cmd);
            }
            data->draws.push_back(std::move(prepared));
        }
    if (frame)
        for (const auto& p : frame->passes) {
            PreparedPass pass;
            pass.firstDraw = data->passes.empty()
                                 ? 0
                                 : data->passes.back().firstDraw + data->passes.back().draws;
            pass.draws = p.draws.size();
            const auto attachment = [&](const std::optional<FrameAttachment>& a,
                                        std::optional<PassAttachment>& out) {
                if (a)
                    out = PassAttachment{scene.targets.at(a->target).handle,
                                         a->load,
                                         a->store,
                                         a->clear,
                                         a->clearDepth,
                                         a->clearStencil};
            };
            attachment(p.color, pass.description.color);
            attachment(p.depthStencil, pass.description.depthStencil);
            for (auto id : p.sampled)
                pass.description.sampled.push_back(scene.targets.at(id).handle);
            data->passes.push_back(std::move(pass));
        }
    out.data = std::move(data);
    Collect();
    return {true, 0, result.draws, "prepared"};
}
ModelReplayResult SceneRenderer::Submit(const PreparedModelFrame& frame,
                                        CompletionToken& completion) {
    completion = {};
    if (!impl->device)
        return {false, 0, 0, "device_not_bound"};
    auto& d = *impl->device;
    const auto* f = frame.data.get();
    if (impl->unknownWork)
        return {false, 0, 0, "untracked_gpu_work"};
    if (!f || f->owner != impl->owner)
        return {false, 0, 0, "foreign_prepared_frame"};
    const auto scene = impl->scenes.find(f->scene);
    if (scene == impl->scenes.end() || scene->second.generation != f->generation)
        return {false, 0, 0, "superseded_prepared_frame"};
    if (!IsCurrentSubmission(d.Status(), f->submission))
        return {false, 0, 0, "stale_prepared_frame"};
    Collect();
    if (!d.BeginFrame(f->clear))
        return {false, 0, 0, "begin_frame_failed"};
    ModelReplayResult result{true, 0, 0, "submitted"};
    const auto submitDraw = [&](const Draw& draw) {
        result.sequence = draw.sequence;
        if (!d.SetModelPipeline(draw.pipeline)) {
            result.success = false;
            result.reason = "pipeline_failed";
            return false;
        }
        bool bound = true;
        for (unsigned slot = 0; slot < 3; ++slot)
            if (draw.textures[slot])
                bound = d.BindTexture(slot, draw.textures[slot]) && bound;
        if (draw.shadow)
            bound = d.BindShadowTexture(draw.shadow) && bound;
        if (!bound) {
            result.success = false;
            result.reason = "binding_failed";
            return false;
        }
        const bool drawn = std::holds_alternative<IndexedDraw>(draw.command)
                               ? d.DrawIndexed(std::get<IndexedDraw>(draw.command))
                               : d.Draw(std::get<NonIndexedDraw>(draw.command));
        if (!drawn) {
            result.success = false;
            result.reason = "draw_failed";
            return false;
        }
        ++result.draws;
        return true;
    };
    if (f->passes.empty()) {
        for (const auto& draw : f->draws)
            if (!submitDraw(draw))
                break;
    } else
        for (const auto& pass : f->passes) {
            if (!d.BeginRenderPass(pass.description)) {
                result.success = false;
                result.reason = "begin_render_pass_failed";
                break;
            }
            for (std::size_t n = pass.firstDraw; n < pass.firstDraw + pass.draws; ++n)
                if (!submitDraw(f->draws[n]))
                    break;
            if (!d.EndRenderPass()) {
                result.success = false;
                result.reason = "end_render_pass_failed";
            }
            if (!result)
                break;
        }
    if (result && f->finalColor && !d.CopyToFrameOutput(f->finalColor)) {
        result.success = false;
        result.reason = "final_color_copy_failed";
    }
    if (!d.EndFrame()) {
        result.success = false;
        result.reason = "end_frame_failed";
    }
    completion = d.SignalCompletion();
    if (!completion) {
        impl->unknownWork = true;
        result.success = false;
        result.reason = "completion_signal_failed";
        return result;
    }
    for (auto& r : scene->second.resources)
        r.second.lastUse = completion;
    for (auto& t : scene->second.targets)
        t.second.lastUse = completion;
    ++impl->stats.submissions;
    return result;
}
bool SceneRenderer::Collect() {
    if (!impl->device)
        return impl->retired.empty();
    if (impl->unknownWork)
        return false;
    bool ok = true;
    auto& entries = impl->retired;
    CompletionToken recent;
    for (const auto& s : impl->scenes)
        for (const auto& r : s.second.resources)
            if (r.second.lastUse.value > recent.value)
                recent = r.second.lastUse;
    if (recent) {
        const auto state = impl->device->PollCompletion(recent);
        if (state == CompletionState::Invalid || state == CompletionState::DeviceLost)
            ok = false;
    }
    for (auto i = entries.begin(); i != entries.end();) {
        const auto state =
            i->completion ? impl->device->PollCompletion(i->completion) : CompletionState::Complete;
        if (state == CompletionState::Complete) {
            if (impl->device->Destroy(i->handle))
                i = entries.erase(i);
            else {
                ok = false;
                ++i;
            }
        } else {
            if (state == CompletionState::Invalid || state == CompletionState::DeviceLost)
                ok = false;
            ++i;
        }
    }
    return ok;
}
bool SceneRenderer::Unload(std::uint64_t id) {
    auto s = impl->scenes.find(id);
    if (s == impl->scenes.end())
        return false;
    for (auto& r : s->second.resources)
        impl->Retire(r.second);
    for (auto& t : s->second.targets)
        if (t.second.handle)
            impl->retired.push_back({t.second.handle, t.second.lastUse});
    impl->scenes.erase(s);
    return Collect();
}
bool SceneRenderer::Detach(std::uint32_t timeout) {
    if (!impl->device)
        return true;
    if (!impl->Drain(timeout))
        return false;
    bool released = true;
    for (auto& s : impl->scenes) {
        ++s.second.generation;
        for (auto& t : s.second.targets) {
            if (t.second.handle && !impl->device->Destroy(t.second.handle)) {
                released = false;
                continue;
            }
            t.second.handle = {};
            t.second.lastUse = {};
        }
        for (auto& r : s.second.resources) {
            if (r.second.handle && !impl->device->Destroy(r.second.handle)) {
                released = false;
                continue;
            }
            r.second.handle = {};
            r.second.lastUse = {};
        }
    }
    for (auto i = impl->retired.begin(); i != impl->retired.end();)
        if (impl->device->Destroy(i->handle))
            i = impl->retired.erase(i);
        else {
            released = false;
            ++i;
        }
    if (!released)
        return false;
    impl->device = nullptr;
    impl->unknownWork = false;
    return true;
}
bool SceneRenderer::Rebind(Device& device, std::uint32_t timeout) {
    if (device.Status().state != DeviceState::Ready)
        return false;
    if (&device == impl->device)
        return true;
    if (!Detach(timeout))
        return false;
    impl->device = &device;
    return true;
}
bool SceneRenderer::ReadTarget(std::uint64_t scene, std::uint64_t target, AttachmentReadback& out) {
    if (!impl->device || impl->unknownWork)
        return false;
    const auto s = impl->scenes.find(scene);
    if (s == impl->scenes.end())
        return false;
    const auto t = s->second.targets.find(target);
    return t != s->second.targets.end() && t->second.handle &&
           impl->device->ReadRenderTexture(t->second.handle, out);
}
SceneRendererStats SceneRenderer::Stats() const {
    auto out = impl->stats;
    for (const auto& s : impl->scenes)
        for (const auto& r : s.second.resources) {
            ++out.cpuResources;
            out.residentResources += bool(r.second.handle);
        }
    out.retiringResources = impl->retired.size();
    return out;
}
}
