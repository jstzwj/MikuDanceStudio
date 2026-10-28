#include "legacy_model_cpu_math.hpp"
#include "mikudancestudio/runtime_cpu_scene_producer.hpp"
#include <algorithm>
namespace mikudancestudio {
using namespace render;
bool RuntimeCpuSceneProducer::Load(const RuntimeCpuScene& scene,
                                   format::SceneResourceResolver& resolver, bool passes,
                                   const rhi::AssetSource& builtins, std::string& reason) {
    std::map<std::uint8_t, std::unique_ptr<RuntimeCpuAccessory>> loaded;
    rhi::AtlasAsset helper;
    std::map<std::uint8_t, std::array<char, 256>> paths;
    ModelFrameInputs checked;
    if (!scene.FrameInputs(checked, reason))
        return false;
    if (scene.Session())
        for (const auto& model : scene.Session()->Document().models)
            if (model.postLoadFlag && std::find(scene.Order().begin(), scene.Order().end(),
                                                model.slot) != scene.Order().end()) {
                reason = "cpu_model_material_pass_override_not_supported";
                return false;
            }
    if (passes && scene.Session()) {
        const auto& doc = scene.Session()->Document();
        if (doc.avi.enabled || doc.picture.enabled) {
            reason = "cpu_scene_multitarget_or_background_not_supported";
            return false;
        }
        for (const auto& item : doc.accessories) {
            std::filesystem::path path;
            if (!resolver.ResolveAccessory(item.slot, item.path, path)) {
                reason = "cpu_scene_missing_accessory_" + std::to_string(item.slot);
                return false;
            }
            auto object = std::make_unique<RuntimeCpuAccessory>();
            if (!object->Load(path, 1 + std::uint64_t(item.slot) * 3, reason))
                return false;
            loaded.emplace(item.slot, std::move(object));
            paths.emplace(item.slot, item.path);
        }
        if (doc.view.groundShadow &&
            !rhi::LoadAtlas(builtins, rhi::AssetId::AccessoryHelper, helper)) {
            reason = "cpu_scene_shadow_helper_asset";
            return false;
        }
    }
    accessories_ = std::move(loaded);
    accessoryPaths_ = std::move(paths);
    helper_ = std::move(helper);
    scenePasses_ = passes;
    assembler_.Reset();
    reason.clear();
    return true;
}
bool RuntimeCpuSceneProducer::Build(RuntimeCpuScene& scene, ModelFrameInputs frame,
                                    const rhi::HistoryDesc& target, rhi::ModelReplayStream& output,
                                    std::string& reason) {
    return BuildScene(scene, frame, target, output, reason, nullptr);
}
bool RuntimeCpuSceneProducer::BuildScene(RuntimeCpuScene& scene, ModelFrameInputs frame,
                                         const rhi::HistoryDesc& target,
                                         rhi::ModelReplayStream& output, std::string& reason,
                                         const rhi::BuiltinShadowState* shadow) {
    if (!scene.FrameInputs(frame, reason))
        return false;
    const auto* session = scene.Session();
    const auto* doc = session ? &session->Document() : nullptr;
    SceneFrameDesc desc;
    desc.target = target;
    desc.frame = frame.scene.frame;
    desc.clear = doc && doc->appearance.blackBackground ? rhi::ClearColor{0, 0, 0, 0}
                                                        : rhi::ClearColor{1, 1, 1, 0};
    assembler_.Begin(desc);
    const auto fail = [&](const std::string& why) {
        reason = why;
        assembler_.Block(reason.c_str());
        return false;
    };
    const auto append = [&](SceneDrawReplacement draw) { assembler_.Append(std::move(draw)); };
    if (doc)
        for (const auto& model : doc->models)
            if (model.postLoadFlag && scene.Visible(model.slot))
                return fail("cpu_model_material_pass_override_not_supported");
    const auto modelAppend = [&](std::uint8_t slot) {
        return [&, slot](SceneDrawReplacement draw) {
            draw.model = slot;
            append(std::move(draw));
        };
    };
    // Solver dependency order and renderer display order are separate PMM fields.
    auto drawOrder = scene.Order();
    if (doc) {
        const auto order = [&](std::uint8_t slot) {
            return std::find_if(doc->models.begin(), doc->models.end(),
                                [&](const auto& m) { return m.slot == slot; })
                ->selection.comboIndex;
        };
        drawOrder.erase(std::remove_if(drawOrder.begin(), drawOrder.end(),
                                       [&](auto slot) { return order(slot) == 255; }),
                        drawOrder.end());
        std::stable_sort(drawOrder.begin(), drawOrder.end(), [&](auto a, auto b) {
            const auto x = order(a), y = order(b);
            return x == y ? a < b : x < y;
        });
        // The native loop selects the first slot at each display index.
        drawOrder.erase(std::unique(drawOrder.begin(), drawOrder.end(),
                                    [&](auto a, auto b) { return order(a) == order(b); }),
                        drawOrder.end());
    }
    ScenePassSettings settings;
    if (scenePasses_ && !scene.PrepareEdges(frame, reason))
        return fail(reason);
    if (scenePasses_ && doc) {
        if ((!shadow && (doc->selfShadowMode || session->Pose().selfShadowMode)) ||
            doc->avi.enabled || doc->picture.enabled)
            return fail("cpu_scene_multitarget_or_background_not_supported");
        settings.shadowBlend = doc->physics.projectedShadowBlend != 0;
        const auto gray = doc->physics.projectedShadowAmbient;
        settings.shadowMaterial.diffuse[3] = .699999988079071f;
        settings.shadowMaterial.ambient = {gray, gray, gray, gray};
        settings.shadowMaterial.specular[3] = 1;
        if (doc->view.groundGrid)
            for (auto family : {SceneDrawFamily::GroundLines, SceneDrawFamily::GroundPlane}) {
                SceneDrawReplacement draw;
                draw.family = family;
                if (!BuildGroundDraw(frame, family, settings, target, desc.clear, draw.cpu,
                                     LegacyCpuRenderMath()))
                    return fail("cpu_scene_ground_producer");
                append(std::move(draw));
            }
    }
    const auto accessories = [&](bool after) {
        if (!scenePasses_ || !doc)
            return true;
        std::vector<const format::PmmSceneAccessory*> order;
        for (const auto& item : doc->accessories)
            order.push_back(&item);
        order.erase(std::remove_if(order.begin(), order.end(),
                                   [](auto* item) { return item->order == 255; }),
                    order.end());
        std::stable_sort(order.begin(), order.end(), [](auto* a, auto* b) {
            return a->order == b->order ? a->slot < b->slot : a->order < b->order;
        });
        order.erase(std::unique(order.begin(), order.end(),
                                [](auto* a, auto* b) { return a->order == b->order; }),
                    order.end());
        for (const auto* item : order) {
            if ((item->order >= doc->physics.accessoryRenderOrder) != after)
                continue;
            const auto found = accessories_.find(item->slot);
            if (found == accessories_.end() || accessoryPaths_.at(item->slot) != item->path)
                return fail("cpu_scene_accessory_resource_changed_reload_required");
            const auto& poses = session->Pose().accessories;
            const auto pose = std::find_if(poses.begin(), poses.end(),
                                           [&](const auto& p) { return p.slot == item->slot; });
            if (pose == poses.end())
                return fail("cpu_scene_accessory_pose_missing");
            if (!pose->visible)
                continue;
            ModelMatrix placement;
            if (!scene.AccessoryPlacement(item->slot, placement, reason))
                return fail(reason);
            if (!found->second->Build(frame, placement, *pose, item->state.additiveBlend != 0,
                                      after, target, desc.clear, append, reason))
                return fail(reason);
        }
        return true;
    };
    if (!accessories(false))
        return false;
    const bool shadows =
        scenePasses_ && doc && doc->view.groundShadow && frame.scene.lightState[4] < 0;
    if (shadows) {
        for (const auto& item : session->Pose().accessories)
            if (item.visible && item.shadowEnabled)
                return fail("cpu_accessory_projected_shadow_not_supported");
        for (auto slot : drawOrder)
            if (scene.Visible(slot) &&
                !scene.Model(slot)->BuildEdges(frame, SceneDrawFamily::ProjectedModelShadow,
                                               settings, &helper_, target, desc.clear,
                                               modelAppend(slot), reason, shadow))
                return fail(reason);
    }
    if (shadow && shadow->mode == rhi::BuiltinShadowMode::Material) {
        // Native casters are visited in slot order, independently of display
        // order and physics dependency order. The assembler shares their CPU
        // geometry versions with the material pass, including caster-only slots.
        auto casters = scene.Order();
        std::sort(casters.begin(), casters.end());
        for (auto slot : casters)
            if (scene.Visible(slot)) {
                const auto cast = [&](SceneDrawReplacement draw) {
                    auto& command = draw.cpu.events.back().draw;
                    if (!command.shadowCaster)
                        return;
                    command.pipeline.shadow.mode = rhi::BuiltinShadowMode::Depth;
                    command.pipeline.raster.cull = *command.shadowCaster;
                    command.pipeline.raster.alphaBlend = false;
                    command.pipeline.stencil = false;
                    command.pipeline.shader.alphaTest = false;
                    command.pipeline.shader.layout = {};
                    command.textures = {};
                    draw.model = slot;
                    append(std::move(draw));
                };
                if (!scene.Model(slot)->Build(frame, target, desc.clear, cast, reason, shadow))
                    return fail(reason);
            }
    }
    for (auto slot : drawOrder)
        if (scene.Visible(slot) &&
            !scene.Model(slot)->Build(frame, target, desc.clear, modelAppend(slot), reason, shadow))
            return fail(reason);
    if (scenePasses_)
        for (auto slot : drawOrder)
            if (scene.Visible(slot) && !scene.Model(slot)->BuildEdges(
                                           frame, SceneDrawFamily::ModelOutline, settings, nullptr,
                                           target, desc.clear, modelAppend(slot), reason, shadow))
                return fail(reason);
    if (!accessories(true))
        return false;
    const auto result = assembler_.Finish(output);
    if (!result) {
        reason = result.reason;
        return false;
    }
    reason.clear();
    return true;
}
bool RuntimeCpuSceneProducer::BuildFrame(RuntimeCpuScene& scene, ModelFrameInputs frame,
                                         const rhi::HistoryDesc& target,
                                         const SelfShadowSettings& settings,
                                         rhi::RenderFrame& output, std::string& reason) {
    const auto fail = [&](const char* why) {
        reason = why;
        return false;
    };
    if (!rhi::ValidHistoryDesc(target) || target.samples != 1 || !target.color || !target.depth ||
        !target.stencil || target.width > 4096 || target.height > 4096 || settings.mode < -1 ||
        settings.mode > 2 || settings.mapSize < 16 || settings.mapSize > 4096 ||
        !std::isfinite(settings.distance))
        return fail("invalid_self_shadow_frame_settings");
    if (!scene.FrameInputs(frame, reason))
        return false;
    const auto* session = scene.Session();
    const int mode =
        settings.mode < 0 ? (session ? session->Pose().selfShadowMode : 0) : settings.mode;
    const float distance = settings.distance > 0 ? settings.distance
                           : session             ? session->Pose().selfShadowDistance
                                                 : .01125f;
    if (mode && session)
        for (const auto& a : session->Pose().accessories)
            if (scenePasses_ && a.visible)
                return fail("cpu_self_shadow_accessories_not_migrated");
    rhi::BuiltinShadowState shadow;
    shadow.mode = mode ? rhi::BuiltinShadowMode::Material : rhi::BuiltinShadowMode::None;
    shadow.perspective = mode == 2;
    ModelMaterialInputs neutral;
    rhi::ModelPipeline camera;
    if (!BuildModelInputs(frame, neutral, {}, camera, LegacyCpuRenderMath()))
        return fail("cpu_shadow_camera");
    using M = d3dx::D3DXMATRIXF;
    auto& math = d3dx::Get();
    M world, view, projection, wvp, temporary, inverse;
    std::memcpy(&world, frame.world.data(), 64);
    std::memcpy(&view, camera.shader.view.data(), 64);
    std::memcpy(&projection, camera.shader.projection.data(), 64);
    math.multiply(&temporary, &world, &view);
    math.multiply(&wvp, &temporary, &projection);
    std::memcpy(shadow.worldViewProjection.data(), &wvp, 64);
    shadow.rotation = frame.lightRotation;
    auto eye = frame.view.editorNavigation
                   ? frame.view.editorEye
                   : std::array<float, 3>{0, 0, frame.scene.camera.distance};
    if (!frame.view.editorNavigation && frame.scene.camera.parentModel >= 0)
        eye = frame.scene.camera.position;
    if (!math.inverse(&inverse, nullptr, &world))
        return fail("cpu_shadow_singular_world");
    math.vec3Transform(shadow.eye.data(), eye.data(), &inverse);
    shadow.eye[3] = 1;
    for (unsigned c = 0; c < 3; ++c)
        shadow.lightDirection[c] = frame.scene.lightState[3 + c];
    math.vec3Normalize(shadow.lightDirection.data(), shadow.lightDirection.data());
    shadow.lightDirection[3] = 1;
    if (mode &&
        !BuildSelfShadowTransform(frame.world, eye, camera.shader.lights[0].direction, distance,
                                  mode, shadow.lightViewProjection, LegacyCpuRenderMath()))
        return fail("cpu_self_shadow_transform");
    rhi::ModelReplayStream sceneStream;
    if (!BuildScene(scene, frame, target, sceneStream, reason, &shadow))
        return false;
    rhi::RenderFrame result;
    result.source = sceneStream.source;
    result.resources = std::move(sceneStream.resources);
    const auto access = rhi::TextureAccess::ColorAttachment | rhi::TextureAccess::Sampled |
                        rhi::TextureAccess::CopySource;
    result.targets = {{1, {target.width, target.height, rhi::TextureFormat::Rgba8, access}},
                      {2,
                       {target.width, target.height, rhi::TextureFormat::Depth24Stencil8,
                        std::uint32_t(rhi::TextureAccess::DepthStencilAttachment)}}};
    rhi::FramePass main;
    main.name = "main_scene";
    main.color = rhi::FrameAttachment{1};
    main.color->clear = sceneStream.clear;
    main.depthStencil = rhi::FrameAttachment{2};
    rhi::FramePass generate;
    generate.name = "self_shadow";
    if (mode) {
        result.targets.push_back(
            {3, {settings.mapSize, settings.mapSize, rhi::TextureFormat::R32Float, access}});
        result.targets.push_back(
            {4,
             {settings.mapSize, settings.mapSize, rhi::TextureFormat::Depth24Stencil8,
              std::uint32_t(rhi::TextureAccess::DepthStencilAttachment)}});
        generate.color = rhi::FrameAttachment{3};
        generate.color->clear = {1, 1, 1, 1};
        generate.depthStencil = rhi::FrameAttachment{4};
        main.sampled = {3};
    }
    for (auto& e : sceneStream.events)
        if (e.op == rhi::ModelReplayOp::Draw) {
            if (e.draw.pipeline.shadow.mode == rhi::BuiltinShadowMode::Depth) {
                e.draw.pipeline.viewport =
                    rhi::ModelViewport{0, 0, settings.mapSize, settings.mapSize, 0, 1};
                generate.draws.push_back(std::move(e.draw));
            } else {
                if (e.draw.pipeline.shadow.mode == rhi::BuiltinShadowMode::Material)
                    e.draw.shadowTarget = 3;
                main.draws.push_back(std::move(e.draw));
            }
        }
    if (mode)
        result.passes.push_back(std::move(generate));
    result.passes.push_back(std::move(main));
    const auto valid = rhi::ValidateRenderFrame(result);
    if (!valid) {
        reason = valid.reason;
        return false;
    }
    output = std::move(result);
    reason.clear();
    return true;
}
}
