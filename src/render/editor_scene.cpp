#include "legacy_model_cpu_math.hpp"
#include "mikudancestudio/editor_scene.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/model_input_bridge.hpp"
#include "mikudancestudio/runtime_cpu_model.hpp"
#include "mikudancestudio/runtime_cpu_accessory.hpp"
#include "mikudancestudio/render_scene_frame.hpp"
#include "mikudancestudio/render_self_shadow.hpp"
#include "mikudancestudio/rhi_post_process.hpp"
#include "mikudancestudio/rhi_screen_texture.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/mme_portable.hpp"
#include "mme_portable_api.h"
#include <algorithm>
#include <cstring>
#include <set>

namespace mikudancestudio {
void BuildLiveAccessoryPlacement(MMDApp*, mdl::AccessoryRecord*, float*);
namespace {
using namespace render;
using namespace rhi;
const ModelMatrix Identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
CallbackAssetSource Builtins(MMDApp& app) {
    return CallbackAssetSource([&app](AssetId id, std::vector<std::uint8_t>& bytes) {
        const auto module = static_cast<HMODULE>(app.HInstance());
        auto resource = FindResourceW(module, MAKEINTRESOURCEW(unsigned(id)), L"PNG");
        const auto size = resource ? SizeofResource(module, resource) : 0;
        const auto loaded = resource ? LoadResource(module, resource) : nullptr;
        const auto data = static_cast<const std::uint8_t*>(loaded ? LockResource(loaded) : nullptr);
        if (!data || !size)
            return false;
        bytes.assign(data, data + size);
        return true;
    });
}
std::wstring MaterialSignature(const mdl::ModelRecord& model) {
    std::wstring result = model.modelDirectory;
    for (unsigned i = 0; i < model.materialCount; ++i) {
        const auto& m = model.materials[i];
        for (const auto text : {m.texturePath, m.spherePath, m.toonPath}) {
            result += L'\0';
            result += text;
        }
        result += wchar_t(m.toonReference + 1);
    }
    if (model.physicsMode != 2)
        for (unsigned n = 0; n < 10; ++n) {
            result += L'\0';
            const auto* text = model.pmdToonFileNames[n];
            for (; *text; ++text)
                result += wchar_t(static_cast<unsigned char>(*text));
        }
    return result;
}
bool ShadowInputs(const ModelFrameInputs& frame, int mode, float distance,
                  BuiltinShadowState& shadow) {
    ModelPipeline camera;
    if (!BuildModelInputs(frame, {}, {}, camera, LegacyCpuRenderMath()))
        return false;
    auto& math = d3dx::Get();
    using Matrix = d3dx::D3DXMATRIXF;
    Matrix world, view, projection, temp, inverse, wvp;
    std::memcpy(&world, frame.world.data(), 64);
    std::memcpy(&view, camera.shader.view.data(), 64);
    std::memcpy(&projection, camera.shader.projection.data(), 64);
    math.multiply(&temp, &world, &view);
    math.multiply(&wvp, &temp, &projection);
    std::memcpy(shadow.worldViewProjection.data(), &wvp, 64);
    shadow.mode = mode ? BuiltinShadowMode::Material : BuiltinShadowMode::None;
    shadow.perspective = mode == 2;
    shadow.rotation = frame.lightRotation;
    auto eye = frame.view.editorNavigation
                   ? frame.view.editorEye
                   : std::array<float, 3>{0, 0, frame.scene.camera.distance};
    if (!frame.view.editorNavigation && frame.scene.camera.parentModel >= 0)
        eye = frame.scene.camera.position;
    if (!math.inverse(&inverse, nullptr, &world))
        return false;
    math.vec3Transform(shadow.eye.data(), eye.data(), &inverse);
    shadow.eye[3] = 1;
    std::copy_n(frame.scene.lightState.begin() + 3, 3, shadow.lightDirection.begin());
    math.vec3Normalize(shadow.lightDirection.data(), shadow.lightDirection.data());
    shadow.lightDirection[3] = 1;
    return !mode ||
           BuildSelfShadowTransform(frame.world, eye, camera.shader.lights[0].direction, distance,
                                    mode, shadow.lightViewProjection, LegacyCpuRenderMath());
}
}
struct EditorSceneProducer::Impl {
    struct Model {
        const mdl::ModelRecord* record = nullptr;
        std::uint64_t geometry = 0;
        std::wstring signature;
        RuntimeCpuModel view;
    };
    struct Accessory {
        std::filesystem::path path;
        RuntimeCpuAccessory object;
    };
    struct Overlay {
        std::vector<std::uint8_t> bytes;
        unsigned stride = 0;
    };
    std::map<unsigned, Model> models;
    std::map<unsigned, Accessory> accessories;
    std::array<Overlay, 3> overlays;
    CpuSceneFrameAssembler assembler;
    ScreenTextureHistory history;
    std::vector<MmePortableEffect> pendingEffects;
    AtlasAsset helper, sprite, font;
    std::uint64_t fontSourceRevision = 0;
    RuntimeCpuAccessory axis;
    bool axisLoaded = false;

    bool AppendOverlay(MMDApp& app, const HistoryDesc& target, ClearColor clear,
                       std::uint32_t frame, EditorOverlay kind) {
        const auto& source = overlays[unsigned(kind)];
        if (source.bytes.empty())
            return true;
        const bool lines = kind == EditorOverlay::Lines;
        const unsigned stride = lines ? 20 : 28, arity = lines ? 2 : 3;
        if (source.stride != stride || source.bytes.size() % (stride * arity))
            return false;
        auto& atlas = kind == EditorOverlay::Text ? font : sprite;
        if (!lines) {
            if (kind == EditorOverlay::Text) {
                const auto native = GetSceneFontRgba(&app);
                if (!native.pixels || native.bytes != 512u * 512u * 4u)
                    return false;
                if (fontSourceRevision != native.revision) {
                    atlas.image = {512, 512, {native.pixels, native.pixels + native.bytes}};
                    atlas.revision = ImageRevision(atlas.image);
                    atlas.image.pixels.Seal();
                    fontSourceRevision = native.revision;
                }
            } else if (!atlas.revision && !LoadAtlas(Builtins(app), AssetId::HudSprites, atlas))
                return false;
            atlas.image.pixels.Seal();
        }
        const auto view = app.ViewportRect();
        const auto count = source.bytes.size() / stride;
        unsigned batch = 0;
        for (std::size_t start = 0; start < count;) {
            std::uint32_t color;
            std::memcpy(&color, source.bytes.data() + start * stride + 16, 4);
            auto end = start + arity;
            for (; end < count; end += arity) {
                bool same = true;
                for (unsigned c = 0; c < arity; ++c) {
                    std::uint32_t next;
                    std::memcpy(&next, source.bytes.data() + (end + c) * stride + 16, 4);
                    same = same && next == color;
                }
                if (!same)
                    break;
            }
            SceneDrawReplacement fragment;
            fragment.family = SceneDrawFamily::ModelMaterial;
            fragment.model = 0x10000 + unsigned(kind);
            fragment.material = batch++;
            auto& stream = fragment.cpu;
            stream.target = target;
            stream.clear = clear;
            stream.source.frame = frame;
            stream.complete = true;
            stream.events.push_back({1, ModelReplayOp::Clear});
            ModelReplayResource vertices, indices;
            vertices.key = {1 + std::uint64_t(batch) * 3, 1};
            indices.key = {vertices.key.id + 1, 1};
            indices.kind = ReplayResourceKind::Indices;
            indices.indexFormat = IndexFormat::UInt32;
            for (auto n = start; n < end; ++n) {
                float position[3], uv[2]{};
                std::memcpy(position, source.bytes.data() + n * stride, 12);
                if (!lines)
                    std::memcpy(uv, source.bytes.data() + n * stride + 20, 8);
                // Preserve native screen pixel centres while changing API clip space.
                const float vertex[]{2 * (position[0] - view.left) / target.width - 1,
                                     1 - 2 * (position[1] - view.top) / target.height,
                                     position[2],
                                     0,
                                     0,
                                     -1,
                                     uv[0],
                                     uv[1]};
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(vertex);
                vertices.bytes.insert(vertices.bytes.end(), bytes, bytes + sizeof(vertex));
                const auto index = std::uint32_t(n - start);
                const auto* indexBytes = reinterpret_cast<const std::uint8_t*>(&index);
                indices.bytes.insert(indices.bytes.end(), indexBytes, indexBytes + 4);
            }
            stream.resources = {std::move(vertices), std::move(indices)};
            ModelReplayDraw draw;
            draw.vertices = stream.resources[0].key;
            draw.indices = stream.resources[1].key;
            draw.vertexCount = draw.indexCount = unsigned(end - start);
            auto& p = draw.pipeline;
            p.topology = lines ? PrimitiveTopology::LineList : PrimitiveTopology::TriangleList;
            p.raster.depthTest = p.raster.depthWrite = false;
            p.raster.alphaBlend = true;
            p.raster.cull = BatchCull::None;
            p.viewport = ModelViewport{0, 0, target.width, target.height};
            p.shader.world = p.shader.view = p.shader.projection = p.shader.normalView = Identity;
            p.shader.nativeDiffuse =
                p.shader.diffuse = {((color >> 16) & 255) / 255.f, ((color >> 8) & 255) / 255.f,
                                    (color & 255) / 255.f, ((color >> 24) & 255) / 255.f};
            if (!lines) {
                ModelReplayResource texture;
                texture.key = {stream.resources[0].key.id + 2, atlas.revision};
                texture.kind = ReplayResourceKind::Texture;
                texture.texture.levels = {atlas.image};
                draw.textures[0] = texture.key;
                stream.resources.push_back(std::move(texture));
                fragment.textureIdentities[0] =
                    kind == EditorOverlay::Text ? "editor:font" : "editor:sprites";
                auto& stage = p.shader.layout.stages[0];
                stage.role = MaterialTextureRole::Base;
                stage.color = MaterialCombine::Modulate;
                stage.colorArg2 = MaterialColorSource::LitDiffuse;
                stage.alpha = MaterialCombine::Modulate;
                p.shader.samplers[0] = {SamplerFilter::Linear, SamplerFilter::Linear,
                                        SamplerFilter::Point,  SamplerAddress::Clamp,
                                        SamplerAddress::Clamp, 1};
            }
            for (const auto& resource : stream.resources)
                stream.events.push_back(
                    {stream.events.size() + 1, ModelReplayOp::PublishResource, resource.key});
            ModelReplayEvent event;
            event.sequence = stream.events.size() + 1;
            event.op = ModelReplayOp::Draw;
            event.draw = std::move(draw);
            stream.events.push_back(std::move(event));
            assembler.Append(std::move(fragment));
            start = end;
        }
        return assembler.Recording();
    }
};
EditorSceneProducer::EditorSceneProducer() : impl(std::make_unique<Impl>()) {
}
EditorSceneProducer::~EditorSceneProducer() = default;
void EditorSceneProducer::Capture(EditorOverlay kind, const void* data, std::size_t count,
                                  unsigned stride) {
    auto& out = impl->overlays.at(unsigned(kind));
    out.stride = stride;
    if (!data || !count || count > 65536 || (stride != 20 && stride != 28)) {
        out.bytes.clear();
        return;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out.bytes.assign(bytes, bytes + count * stride);
}
void EditorSceneProducer::Clear(EditorOverlay kind) {
    impl->overlays.at(unsigned(kind)).bytes.clear();
}
void EditorSceneProducer::Commit() {
    impl->history.Commit();
    for (const auto& effect : impl->pendingEffects)
        MmePortableCommitParameters(effect);
    impl->pendingEffects.clear();
}
void EditorSceneProducer::ResetHistory() {
    impl->history.Reset();
    impl->pendingEffects.clear();
}
void EditorSceneProducer::Reset() {
    impl = std::make_unique<Impl>();
}
bool EditorSceneProducer::Build(MMDApp& app, unsigned width, unsigned height, RenderFrame& output,
                                std::string& reason) {
    impl->pendingEffects.clear();
    const auto fail = [&](const std::string& why) {
        reason = why;
        return false;
    };
    if (!app.CpuRenderFrameReady() || !width || !height)
        return fail("live_frame_unavailable");
    auto frame = app.CpuRenderFrame();
    frame.view.viewport = {0, 0, width, height};
    HistoryDesc target;
    target.width = width;
    target.height = height;
    const ClearColor clear =
        app.state.blackBackgroundEnabled ? ClearColor{0, 0, 0, 0} : ClearColor{1, 1, 1, 0};
    impl->assembler.Begin({target, clear, 1, frame.scene.frame});
    const int mode = UseEffectModelRenderer(&app) ? app.SelfShadowMode() : 0;
    BuiltinShadowState shadow;
    if (!ShadowInputs(frame, mode, app.ShadowDistance(), shadow))
        return fail("live_shadow_inputs");
    auto builtin = Builtins(app);
    ToonTextureResolver toon(std::filesystem::path(app.ExeDir()) / L"Data", builtin);
    ScenePassSettings settings;
    settings.shadowBlend = app.ProjectedShadowBlendEnabled() != 0;
    const auto nativeShadow = app.ProjectedShadowMaterial();
    settings.shadowMaterial.diffuse[3] = nativeShadow.Diffuse.a;
    settings.shadowMaterial.ambient = {nativeShadow.Ambient.r, nativeShadow.Ambient.g,
                                       nativeShadow.Ambient.b, nativeShadow.Ambient.a};
    settings.shadowMaterial.specular[3] = nativeShadow.Specular.a;
    const bool projected = app.GroundShadowEnabled() &&
                           (app.PlaybackActive() || app.UsesViewportTool()) &&
                           app.LightDirection()[1] < 0;
    if (projected && !impl->helper.revision &&
        !LoadAtlas(builtin, AssetId::AccessoryHelper, impl->helper))
        return fail("live_shadow_helper");
    std::set<unsigned> slots;
    for (unsigned slot = 0; slot < kModelSlotCount; ++slot) {
        auto* model = app.ModelSlot(slot);
        if (!model || !model->loadComplete || !model->vertexCount || !model->indexCount)
            continue;
        const auto cpu = app.CpuModelFrames().find(model);
        if (cpu == app.CpuModelFrames().end() || !cpu->second->ready)
            return fail("live_model_geometry_unavailable");
        if (model->displayState || model->postLoadFlag2)
            return fail("model_display_override");
        auto& entry = impl->models[slot];
        const auto identity = cpu->second->geometry.Snapshot().main.key.id;
        const auto signature = MaterialSignature(*model);
        if (entry.record != model || entry.geometry != identity) {
            if (!entry.view.AttachLive(app, *model, reason))
                return false;
            entry.record = model;
            entry.geometry = identity;
            entry.signature.clear();
        }
        if (entry.signature != signature) {
            if (!entry.view.PrepareMaterials(toon, reason))
                return false;
            entry.signature = signature;
        }
        slots.insert(slot);
    }
    for (auto i = impl->models.begin(); i != impl->models.end();)
        if (!slots.count(i->first))
            i = impl->models.erase(i);
        else
            ++i;
    std::vector<MmePortableEffect> assigned;
    MmePortablePrepareParameters();
    if (!MmePortableEffects(assigned, reason))
        return false;
    const MmePortableEffect* postEffect = nullptr;
    for (const auto& effect : assigned)
        if (effect.post)
            postEffect = &effect;
    std::vector<std::pair<int, MmePortableEffect>> effects;
    const auto append = [&](unsigned slot, SceneDrawReplacement draw) {
        MmePortableEffect effect;
        int selection = 0;
        const bool accessory = draw.family == SceneDrawFamily::AccessoryMaterial;
        const ModelReplayDraw* command = nullptr;
        for (const auto& event : draw.cpu.events)
            if (event.op == ModelReplayOp::Draw)
                command = &event.draw;
        const bool hostObject =
            accessory ? slot < mdl::kAccessorySlotCount : slot < kModelSlotCount;
        if (!assigned.empty() && hostObject && command &&
            (accessory || draw.family == SceneDrawFamily::ModelMaterial ||
             draw.family == SceneDrawFamily::ModelOutline ||
             draw.family == SceneDrawFamily::ProjectedModelShadow)) {
            const auto object = accessory ? static_cast<const void*>(app.AccessorySlot(slot))
                                          : static_cast<const void*>(app.ModelSlot(slot));
            unsigned flags = 0;
            for (const auto& stage : command->pipeline.shader.layout.stages)
                flags |= stage.role == MaterialTextureRole::Base     ? 1u
                         : stage.role == MaterialTextureRole::Sphere ? 2u
                         : stage.role == MaterialTextureRole::Toon   ? 4u
                                                                     : 0u;
            const int pass = draw.family == SceneDrawFamily::ModelOutline           ? 3
                             : draw.family == SceneDrawFamily::ProjectedModelShadow ? 2
                             : mode                                                 ? 1
                                                                                    : 0;
            selection = MmePortableDraw(reinterpret_cast<std::uintptr_t>(object),
                                        int(draw.material), pass, flags, effect, reason);
            if (selection == 1 && !accessory && effect.legacy)
                effect.counts[0] = int(app.ModelSlot(slot)->vertexCount);
            if (selection == 1 && effect.legacy)
                effect.transparent =
                    UseEffectModelRenderer(&app) && app.state.characterTransparentMode != 0;
            if (selection < 0)
                impl->assembler.Block(reason.c_str());
        }
        effects.emplace_back(selection, std::move(effect));
        draw.model = slot;
        impl->assembler.Append(std::move(draw));
    };
    if (app.GroundGridEnabled())
        for (auto family : {SceneDrawFamily::GroundLines, SceneDrawFamily::GroundPlane}) {
            SceneDrawReplacement draw;
            draw.family = family;
            if (!BuildGroundDraw(frame, family, settings, target, clear, draw.cpu,
                                 LegacyCpuRenderMath()))
                return fail("live_ground_inputs");
            append(0, std::move(draw));
        }
    std::vector<ScreenTextureConsumer> consumers;
    std::size_t drawOrdinal = 0;
    std::vector<std::size_t> screens;
    // The final assembler preserves the native accessory split and model order.
    const auto accessoryRange = [&](bool after) {
        for (unsigned order = 0; order < mdl::kAccessorySlotCount; ++order)
            for (unsigned slot = 0; slot < mdl::kAccessorySlotCount; ++slot) {
                auto* a = app.AccessorySlot(slot);
                if (!a || a->order != order)
                    continue;
                if ((int(order) >= app.AccessoryRenderSplitOrder()) != after || !a->visible)
                    break;
                const bool carrier =
                    postEffect && postEffect->object == reinterpret_cast<std::uintptr_t>(a);
                if (!carrier && a->shadowEnabled && (projected || mode))
                    return fail("accessory_shadows");
                auto& cached = impl->accessories[slot];
                const std::filesystem::path path = a->sourcePath;
                if (cached.path != path) {
                    const auto id = app.AllocateCpuGeometryIds();
                    if (!id || !cached.object.Load(path, id, reason))
                        return false;
                    cached.path = path;
                }
                ModelMatrix placement;
                BuildLiveAccessoryPlacement(&app, a, placement.data());
                format::PmmEvaluatedAccessory pose;
                pose.slot = std::uint8_t(slot);
                pose.visible = a->visible;
                pose.opacity = a->opacity;
                if (!cached.object.Build(
                        frame, placement, pose, a->additiveBlend != 0, after, target, clear,
                        [&](SceneDrawReplacement draw) {
                            if (draw.screenTexture)
                                screens.push_back(drawOrdinal);
                            ++drawOrdinal;
                            append(slot, std::move(draw));
                        },
                        reason))
                    return false;
                break;
            }
        return true;
    };
    // Count already appended ground draws for screen consumer positions.
    drawOrdinal = app.GroundGridEnabled() ? 2 : 0;
    if (!accessoryRange(false))
        return false;
    std::vector<unsigned> order;
    for (unsigned display = 0; display < kModelSlotCount; ++display)
        for (auto slot : slots)
            if (app.ModelSlot(slot)->comboSelIndex == display) {
                order.push_back(slot);
                break;
            }
    const auto modelAppend = [&](unsigned slot) {
        return [&, slot](SceneDrawReplacement draw) {
            ++drawOrdinal;
            append(slot, std::move(draw));
        };
    };
    if (projected)
        for (auto slot : order)
            if (!impl->models.at(slot).view.BuildEdges(frame, SceneDrawFamily::ProjectedModelShadow,
                                                       settings, &impl->helper, target, clear,
                                                       modelAppend(slot), reason, &shadow))
                return false;
    for (auto slot : order)
        if (!impl->models.at(slot).view.Build(frame, target, clear, modelAppend(slot), reason,
                                              &shadow))
            return false;
    for (auto slot : order)
        if ((app.PlaybackActive() || app.UsesViewportTool()) &&
            !(mode && app.ModelNonDisplayMode()) &&
            !impl->models.at(slot).view.BuildEdges(frame, SceneDrawFamily::ModelOutline, settings,
                                                   nullptr, target, clear, modelAppend(slot),
                                                   reason, &shadow))
            return false;
    if (!accessoryRange(true))
        return false;
    for (auto i = impl->accessories.begin(); i != impl->accessories.end();)
        if (!app.AccessorySlot(i->first))
            i = impl->accessories.erase(i);
        else
            ++i;
    const auto sceneDraws = drawOrdinal;
    // The editor's operation mesh uses the same CPU transform as DX9 and has
    // its own depth clear. Screen capture deliberately precedes this pass.
    ModelMatrix axisWorld;
    unsigned axisDraws = 0;
    if (app.CameraMode() == 0 && !app.PlaybackActive() && !app.FrameCopyOrPhysicsDialog() &&
        BuildBoneOperationAxisTransform(&app, app.SelectedModel(), frame.world.data(),
                                        axisWorld.data())) {
        if (!impl->axisLoaded) {
            const auto module = static_cast<HMODULE>(app.HInstance());
            const auto resource = FindResourceW(module, MAKEINTRESOURCEW(0x73), L"XFILE");
            const auto loaded = resource ? LoadResource(module, resource) : nullptr;
            const auto bytes = loaded ? LockResource(loaded) : nullptr;
            const auto size = resource ? SizeofResource(module, resource) : 0;
            if (!impl->axis.LoadMemory(bytes, size, {}, app.AllocateCpuGeometryIds(), reason, true))
                return false;
            impl->axisLoaded = true;
        }
        auto axisFrame = frame;
        axisFrame.world = Identity;
        format::PmmEvaluatedAccessory pose;
        pose.visible = 1;
        pose.opacity = 1;
        if (!impl->axis.Build(
                axisFrame, axisWorld, pose, false, true, target, clear,
                [&](SceneDrawReplacement draw) {
                    ++axisDraws;
                    append(0x20000, std::move(draw));
                },
                reason, true))
            return false;
    }
    for (auto kind : {EditorOverlay::Text, EditorOverlay::Lines, EditorOverlay::Sprites})
        if (!impl->AppendOverlay(app, target, clear, frame.scene.frame, kind))
            return fail("editor_overlay_inputs");
    ModelReplayStream stream;
    const auto assembled = impl->assembler.Finish(stream);
    if (!assembled)
        return fail(assembled.reason);
    RenderFrame result;
    result.source = stream.source;
    result.resources = std::move(stream.resources);
    const auto access =
        TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
    result.targets = {{1, {width, height, TextureFormat::Rgba8, access}},
                      {2,
                       {width, height, TextureFormat::Depth24Stencil8,
                        std::uint32_t(TextureAccess::DepthStencilAttachment)}}};
    result.finalColor = 1;
    if (!RegisterPortableMmeTargets(result, assigned, reason))
        return false;
    FramePass main;
    main.name = "editor_scene";
    main.color = FrameAttachment{1};
    main.color->clear = clear;
    main.depthStencil = FrameAttachment{2};
    FramePass generate;
    generate.name = "self_shadow";
    if (mode) {
        result.targets.push_back({3, {1024, 1024, TextureFormat::R32Float, access}});
        result.targets.push_back({4,
                                  {1024, 1024, TextureFormat::Depth24Stencil8,
                                   std::uint32_t(TextureAccess::DepthStencilAttachment)}});
        generate.color = FrameAttachment{3};
        generate.color->clear = {1, 1, 1, 1};
        generate.depthStencil = FrameAttachment{4};
        main.sampled = {3};
    }
    FramePass overlay;
    overlay.name = "editor_overlays";
    FramePass axes;
    axes.name = "editor_operation_axis";
    std::size_t ordinal = 0;
    for (auto& e : stream.events)
        if (e.op == ModelReplayOp::Draw) {
            if (ordinal >= sceneDraws) {
                if (ordinal < sceneDraws + axisDraws)
                    axes.draws.push_back(std::move(e.draw));
                else
                    overlay.draws.push_back(std::move(e.draw));
                ++ordinal;
                continue;
            }
            const auto& effect = effects.at(ordinal);
            if (effect.first == 2) {
                ++ordinal;
                continue;
            }
            if (mode && e.draw.shadowCaster) {
                auto caster = e.draw;
                caster.pipeline.shadow = shadow;
                caster.pipeline.shadow.mode = BuiltinShadowMode::Depth;
                caster.pipeline.raster.cull = *caster.shadowCaster;
                caster.pipeline.raster.alphaBlend = false;
                caster.pipeline.stencil = false;
                caster.pipeline.shader.alphaTest = false;
                caster.pipeline.shader.layout = {};
                caster.pipeline.viewport = ModelViewport{0, 0, 1024, 1024};
                caster.textures = {};
                generate.draws.push_back(std::move(caster));
            }
            if (e.draw.pipeline.shadow.mode == BuiltinShadowMode::Material)
                e.draw.shadowTarget = 3;
            if (effect.first == 1 && !ApplyPortableMmeMaterial(e.draw, effect.second, reason))
                return false;
            if (std::find(screens.begin(), screens.end(), ordinal++) != screens.end())
                consumers.push_back({mode ? 1u : 0u, main.draws.size()});
            if (e.draw.shader)
                for (const auto& binding : e.draw.shader->textures)
                    if (binding.target.target &&
                        std::find(main.sampled.begin(), main.sampled.end(),
                                  binding.target.target) == main.sampled.end())
                        main.sampled.push_back(binding.target.target);
            main.draws.push_back(std::move(e.draw));
        }
    if (mode)
        result.passes.push_back(std::move(generate));
    result.passes.push_back(std::move(main));
    if (postEffect) {
        if (!screens.empty())
            return fail("MME_screen_history_not_supported");
        result.finalColor = 1;
        if (!AppendPortableMmePost(result, *postEffect, reason))
            return false;
    }
    // Capture before editor gizmos/HUD, matching the native screen.bmp timing.
    if (!AppendPostProcess(result, result.finalColor ? result.finalColor : 1, 5, width, height) ||
        !impl->history.Apply(result, consumers, ScreenCaptureKind(app.CaptureMode()), 5))
        return fail("editor_screen_history");
    if (!axes.draws.empty()) {
        axes.color = FrameAttachment{result.finalColor};
        axes.color->load = AttachmentLoad::Load;
        axes.depthStencil = FrameAttachment{2};
        result.passes.push_back(std::move(axes));
    }
    if (!overlay.draws.empty()) {
        overlay.color = FrameAttachment{result.finalColor};
        overlay.color->load = AttachmentLoad::Load;
        result.passes.push_back(std::move(overlay));
    }
    const auto valid = ValidateRenderFrame(result);
    if (!valid)
        return fail(valid.reason);
    output = std::move(result);
    impl->pendingEffects = std::move(assigned);
    reason.clear();
    return true;
}
}
