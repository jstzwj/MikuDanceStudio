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
        if (doc.selfShadowMode || doc.avi.enabled || doc.picture.enabled) {
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
    if (scenePasses_ && doc) {
        if (doc->selfShadowMode || session->Pose().selfShadowMode || doc->avi.enabled ||
            doc->picture.enabled)
            return fail("cpu_scene_multitarget_or_background_not_supported");
        if (!scene.PrepareEdges(frame, reason))
            return fail(reason);
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
            if (scene.Visible(slot) && !scene.Model(slot)->BuildEdges(
                                           frame, SceneDrawFamily::ProjectedModelShadow, settings,
                                           &helper_, target, desc.clear, modelAppend(slot), reason))
                return fail(reason);
    }
    for (auto slot : drawOrder)
        if (scene.Visible(slot) &&
            !scene.Model(slot)->Build(frame, target, desc.clear, modelAppend(slot), reason))
            return fail(reason);
    if (scenePasses_)
        for (auto slot : drawOrder)
            if (scene.Visible(slot) && !scene.Model(slot)->BuildEdges(
                                           frame, SceneDrawFamily::ModelOutline, settings, nullptr,
                                           target, desc.clear, modelAppend(slot), reason))
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
}
