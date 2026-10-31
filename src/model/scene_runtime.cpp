#include "mikudancestudio/scene_runtime.hpp"
#include "mikudancestudio/model_physics_runtime.hpp"
#include "mikudancestudio/physics_world_state.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "mikudancestudio/vmd_scene_tracks.hpp"
#include "model_timeline.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <cstdio>

namespace mikudancestudio::runtime {
struct SceneRuntime::Impl {
    struct Item {
        std::shared_ptr<const ModelResource> resource;
        std::unique_ptr<ModelInstance> instance;
        std::unique_ptr<ModelPhysics> physics;
        ModelEvaluation evaluation;
        ModelTimeline timeline;
        std::vector<std::size_t> boneMap, morphMap, ikMap, selectorMap;
        bool visible = true;
    };
    PhysicsWorldState world;
    std::map<std::uint8_t, Item> items;
    std::map<std::uint8_t, std::shared_ptr<const AccessoryResource>> accessories;
    std::vector<std::uint8_t> order;
    std::optional<format::SceneSession> session;
    CpuSimulationState simulation;
    SceneCounters counters;
    int constraintId = 0;
    std::uint32_t frame = 0, randomState = 1;
    bool playing = false, failed = false;
    std::string failure;
    Impl() { CreatePhysicsWorld(world); }
    ~Impl() {
        items.clear();
        DestroyPhysicsWorld(world);
    }
    bool Add(std::uint8_t slot, const format::ModelDocument& model, std::string& reason) {
        if (slot == 255 || items.count(slot)) {
            reason = "duplicate-or-invalid-model-slot";
            return false;
        }
        Item item;
        if (!ModelResource::Create(model, item.resource, reason))
            return false;
        item.instance = std::make_unique<ModelInstance>(item.resource);
        item.physics = std::make_unique<ModelPhysics>();
        if (!item.physics->Create(item.resource, world.world, constraintId, reason))
            return false;
        if (!item.timeline.Initialize(*item.instance, *item.physics, reason))
            return false;
        items.emplace(slot, std::move(item));
        order.push_back(slot);
        return true;
    }
    bool MapNames(const std::vector<std::vector<char>>& disk, const std::vector<std::string>& names,
                  std::vector<std::size_t>& map, std::string& reason) {
        map.clear();
        std::vector<bool> used(names.size());
        for (const auto& bytes : disk) {
            const auto end = std::find(bytes.begin(), bytes.end(), 0);
            std::string name;
            if (!text::DecodeCp932(std::string_view(bytes.data(), std::size_t(end - bytes.begin())),
                                   name)) {
                reason = "invalid-pmm-name-encoding";
                return false;
            }
            auto found = names.begin();
            while (found != names.end() && (*found != name || used[found - names.begin()]))
                ++found;
            if (found == names.end()) {
                reason = "pmm-model-name-mismatch";
                return false;
            }
            used[found - names.begin()] = true;
            map.push_back(std::size_t(found - names.begin()));
        }
        if (map.size() != names.size()) {
            reason = "pmm-model-layout-mismatch";
            return false;
        }
        return true;
    }
    bool Validate(std::string& reason) {
        if (!session)
            return true;
        const auto& doc = session->Document();
        if (doc.physics.playbackMode > 3) {
            reason = "unsupported-physics-mode";
            return false;
        }
        for (const auto& source : doc.models) {
            auto found = items.find(source.slot);
            if (found == items.end())
                continue;
            auto& item = found->second;
            if (doc.envelope.header.version == format::PmmVersion::V2) {
                if (!MapNames(source.boneNames, item.resource->BoneNames(), item.boneMap, reason) ||
                    !MapNames(source.morphNames, item.resource->MorphNames(), item.morphMap,
                              reason))
                    return false;
            } else {
                if (source.layout.boneCount != item.resource->BoneNames().size() ||
                    source.layout.morphCount != item.resource->MorphNames().size()) {
                    reason = "pmm-model-layout-mismatch";
                    return false;
                }
                item.boneMap.resize(source.layout.boneCount);
                std::iota(item.boneMap.begin(), item.boneMap.end(), 0);
                item.morphMap.resize(source.layout.morphCount);
                std::iota(item.morphMap.begin(), item.morphMap.end(), 0);
            }
            ModelEvaluation probe;
            if (!item.instance->Prepare(0, probe, reason))
                return false;
            auto view = probe.View();
            item.ikMap.clear();
            item.selectorMap.clear();
            if (doc.envelope.header.version == format::PmmVersion::V1) {
                if (source.layout.ikCount != unsigned(view.ikChainCount)) {
                    reason = "pmm-ik-layout-mismatch";
                    return false;
                }
                item.ikMap.resize(view.ikChainCount);
                std::iota(item.ikMap.begin(), item.ikMap.end(), 0);
            } else {
                for (int index : source.ikSources) {
                    if (index < 0 || std::size_t(index) >= item.boneMap.size()) {
                        reason = "invalid-pmm-ik-source";
                        return false;
                    }
                    int n = 0;
                    for (; n < view.ikChainCount; ++n)
                        if (view.ikChains[n].boneIndex == int(item.boneMap[index]))
                            break;
                    if (n == view.ikChainCount) {
                        reason = "pmm-ik-source-mismatch";
                        return false;
                    }
                    item.ikMap.push_back(n);
                }
                for (int index : source.rigidSources) {
                    if (index < -1 || index >= int(item.boneMap.size())) {
                        reason = "invalid-pmm-selector-source";
                        return false;
                    }
                    const int slot = index < 0 ? 0 : view.boneTable[item.boneMap[index]].slotIndex;
                    if (slot < 0) {
                        reason = "pmm-selector-source-mismatch";
                        return false;
                    }
                    item.selectorMap.push_back(slot);
                }
            }
        }
        const auto bindings = [&](const format::PmmDisplayKey& key) {
            for (const auto& ref : key.rigidStates)
                if (ref.modelIndex >= 0) {
                    const auto parent = items.find(std::uint8_t(ref.modelIndex));
                    if (ref.modelIndex > 254 || parent == items.end() || ref.boneIndex < 0 ||
                        std::size_t(ref.boneIndex) >= parent->second.boneMap.size())
                        return false;
                }
            return true;
        };
        for (const auto& source : doc.models) {
            for (const auto& key : source.displayKeys.dense)
                if (!bindings(key)) {
                    reason = "missing-selector-parent";
                    return false;
                }
            for (const auto& key : source.displayKeys.sparse)
                if (!bindings(key.value)) {
                    reason = "missing-selector-parent";
                    return false;
                }
        }
        const auto parentValid = [&](int model, int bone) {
            if (model == -1)
                return true;
            if (model < 0 || model > 254 || bone < -1)
                return false;
            const auto found = items.find(std::uint8_t(model));
            return found != items.end() &&
                   std::size_t(std::max(0, bone)) < found->second.resource->BoneNames().size();
        };
        const auto trackValid = [&](const auto& track) {
            for (const auto& key : track.dense)
                if (!parentValid(key.parentModel, key.parentBone))
                    return false;
            for (const auto& key : track.sparse)
                if (!parentValid(key.value.parentModel, key.value.parentBone))
                    return false;
            return true;
        };
        if (doc.envelope.header.version == format::PmmVersion::V2 && !trackValid(doc.cameraKeys)) {
            reason = "missing-camera-parent";
            return false;
        }
        for (const auto& parent : doc.v1CameraParents)
            if (!parentValid(parent.model, parent.bone)) {
                reason = "missing-camera-parent";
                return false;
            }
        for (const auto& accessory : doc.accessories)
            if (!parentValid(accessory.state.parentModel, accessory.state.parentBone) ||
                !trackValid(accessory.keyRecords)) {
                reason = "missing-accessory-parent";
                return false;
            }
        for (const auto& source : doc.models) {
            auto found = items.find(source.slot);
            if (found == items.end())
                continue;
            auto& item = found->second;
            item.timeline.Publish(source, item.boneMap, item.morphMap, item.ikMap,
                                  item.selectorMap);
            for (auto& key : item.timeline.displayKeys)
                for (auto& ref : key.selectors)
                    if (ref.modelIndex >= 0)
                        ref.boneIndex =
                            int(items.at(std::uint8_t(ref.modelIndex)).boneMap[ref.boneIndex]);
        }
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
            const auto rank = [&](auto slot) {
                for (const auto& m : doc.models)
                    if (m.slot == slot)
                        return int(doc.envelope.header.version == format::PmmVersion::V2
                                       ? m.previousDisplayOrder
                                       : m.selection.comboIndex);
                return 0;
            };
            return rank(a) < rank(b);
        });
        return true;
    }
    void Gravity() {
        if (!session)
            return;
        const auto& g = session->Pose().gravity;
        simulation.gravityMagnitude = g.acceleration;
        simulation.gravityDirection = g.direction;
        simulation.gravityNoise = g.noise;
        simulation.gravityNoiseEnabled = g.noiseEnabled != 0;
    }
    bool Prepare(std::string& reason) {
        for (auto slot : order) {
            auto& item = items.at(slot);
            {
                auto& timeline = item.timeline;
                std::vector<format::PmmEvaluatedBone> bones(timeline.bones.size());
                std::vector<float> morphs(timeline.morphs.size());
                for (std::size_t n = 0; n < bones.size(); ++n) {
                    std::copy_n(timeline.bones[n].trans, 3, bones[n].translation.begin());
                    std::copy_n(timeline.bones[n].rotQuat, 4, bones[n].rotation.begin());
                    bones[n].physicsDisabled = timeline.bones[n].physicsDisabled;
                }
                for (std::size_t n = 0; n < morphs.size(); ++n)
                    morphs[n] = timeline.morphs[n].value;
                if (!item.instance->PreparePose(frame, bones, morphs, item.evaluation, reason))
                    return false;
                item.evaluation.Bones() = timeline.bones;
                item.visible = timeline.visible != 0;
                std::vector<std::uint8_t> enabled;
                for (const auto& chain : timeline.chains)
                    enabled.push_back(chain.enabled ? 1 : 0);
                if (!item.evaluation.SetIk(enabled, reason) ||
                    !item.evaluation.SetBindings(timeline.bindings, reason))
                    return false;
            }
            item.physics->Restore(item.evaluation);
            ++counters.preparedModels;
        }
        return true;
    }
    static void Pose(CpuModelSlots slots, CpuPosePhase phase, int mode, int skip) {
        auto& self = *static_cast<Impl*>(slots.context);
        if (self.failed)
            return;
        if (phase == CpuPosePhase::BeforePhysics && !self.Prepare(self.failure)) {
            self.failed = true;
            return;
        }
        std::array<cpu_model::Model, 255> views{};
        std::array<cpu_model::Model*, 255> models{};
        for (auto& pair : self.items) {
            views[pair.first] = pair.second.evaluation.View();
            models[pair.first] = &views[pair.first];
        }
        for (auto slot : self.order)
            if (slot != skip) {
                auto& item = self.items.at(slot);
                if (!item.evaluation.Solve(phase == CpuPosePhase::AfterPhysics, models.data(), mode,
                                           self.failure)) {
                    self.failed = true;
                    return;
                }
            }
    }
    static void Physics(CpuModelSlots slots, CpuPhysicsModelPhase phase, CpuSlotOrder order,
                        int skip) {
        auto& self = *static_cast<Impl*>(slots.context);
        if (self.failed)
            return;
        for (unsigned n = 0; n < 255; ++n) {
            auto slot = std::uint8_t(order == CpuSlotOrder::Forward ? n : 254 - n);
            auto item = self.items.find(slot);
            if (item != self.items.end() && slot != skip)
                item->second.physics->Execute(item->second.evaluation, phase);
        }
    }
    CpuSimulationContext Context() {
        return {{nullptr, 0, this},
                world.world,
                {Pose, Physics, [](float* o, const float* i) { cpu_model::Math{}.normalize(o, i); },
                 nullptr,
                 [](void* context) {
                     auto& state = static_cast<Impl*>(context)->randomState;
                     state = state * 214013u + 2531011u;
                     return int((state >> 16) & 32767u);
                 }}};
    }
    static void AdvanceTracks(void* context, CpuSimulationState& state, bool substep) {
        auto& self = *static_cast<Impl*>(context);
        self.frame = std::uint32_t(double(state.cursorSeconds) * 30.0);
        if (self.session && !self.session->Advance(state.cursorSeconds)) {
            self.failed = true;
            self.failure = "scene-track-advance-failed";
        }
        for (auto& pair : self.items)
            pair.second.timeline.Advance(state.cursorSeconds, state.physicsMode,
                                         *pair.second.physics);
        if (!substep)
            self.Gravity();
    }
    bool Frame(float delta, std::string& reason) {
        CpuSimulationFrame frame;
        frame.deltaSeconds = delta;
        frame.playbackActive = playing;
        if (failed || !RunCpuSimulationFrame(simulation, Context(), frame) || failed) {
            reason = failure.empty() ? "scene-simulation-failed" : failure;
            failed = true;
            return false;
        }
        for (auto slot : order)
            if (!items.at(slot).evaluation.Finish(reason)) {
                failed = true;
                return false;
            }
        for (auto slot : order) {
            auto& item = items.at(slot);
            item.timeline.bones = item.evaluation.Bones();
            if (!item.instance->Publish(std::move(item.evaluation), reason)) {
                failed = true;
                return false;
            }
            ++counters.publishedModels;
        }
        ++counters.frames;
        reason.clear();
        return true;
    }
};
SceneRuntime::SceneRuntime() = default;
SceneRuntime::~SceneRuntime() = default;
bool SceneRuntime::Load(format::SceneSession session,
                        const std::map<std::uint8_t, format::ModelDocument>& models,
                        std::string& reason) {
    if (!session.Loaded()) {
        reason = "scene-session-not-loaded";
        return false;
    }
    auto next = std::make_unique<Impl>();
    next->session = std::move(session);
    for (const auto& entry : next->session->Document().models) {
        const auto found = models.find(entry.slot);
        if (found == models.end() || !next->Add(entry.slot, found->second, reason)) {
            if (reason.empty())
                reason = "scene-model-not-resolved";
            return false;
        }
    }
    if (!next->Validate(reason))
        return false;
    next->frame = next->session->Pose().frame;
    if (!next->session->Seek(next->frame)) {
        reason = "scene-initial-seek-failed";
        return false;
    }
    next->simulation.physicsMode = next->session->Document().physics.playbackMode;
    next->simulation.cursorSeconds = float(next->frame) / 30.f;
    next->simulation.resetPending = 1;
    next->Gravity();
    for (auto& pair : next->items)
        pair.second.timeline.Seek(next->frame, next->simulation.physicsMode, *pair.second.physics);
    if (!next->Frame(0, reason))
        return false;
    impl_ = std::move(next);
    return true;
}
bool SceneRuntime::LoadModel(const format::ModelDocument& model, std::string& reason) {
    auto next = std::make_unique<Impl>();
    if (!next->Add(0, model, reason))
        return false;
    next->simulation.physicsMode = 2;
    next->simulation.resetPending = 1;
    for (auto& item : next->items)
        item.second.timeline.Seek(0, 2, *item.second.physics);
    if (!next->Frame(0, reason))
        return false;
    impl_ = std::move(next);
    return true;
}
bool SceneRuntime::BindMotion(std::uint8_t slot, std::shared_ptr<const MotionClip> clip,
                              std::string& reason) {
    if (!impl_ || impl_->failed || impl_->session || !impl_->items.count(slot)) {
        reason = "scene-motion-target-unavailable";
        return false;
    }
    if (!clip) {
        reason = "missing-motion-clip";
        return false;
    }
    auto& item = impl_->items.at(slot);
    if (!item.instance->BindMotion(clip, reason) || !item.timeline.Bind(*clip, reason))
        return false;
    return Seek(impl_->frame, reason);
}
bool SceneRuntime::Seek(std::uint32_t frame, std::string& reason) {
    if (!impl_ || impl_->failed || frame > unsigned(std::numeric_limits<int>::max())) {
        reason = "invalid-scene-seek";
        return false;
    }
    if (impl_->session && !impl_->session->Seek(frame)) {
        reason = "scene-track-seek-failed";
        return false;
    }
    impl_->playing = false;
    impl_->frame = frame;
    impl_->simulation.cursorSeconds = float(frame) / 30.f;
    impl_->simulation.remainingSeconds = 0;
    impl_->simulation.resetPending = 1;
    impl_->Gravity();
    for (auto& pair : impl_->items)
        pair.second.timeline.Seek(frame, impl_->simulation.physicsMode, *pair.second.physics);
    return impl_->Frame(0, reason);
}
bool SceneRuntime::BindGlobalMotion(const format::VmdDocument& motion, std::string& reason) {
    if (!Session() || impl_->failed) {
        reason = "global-motion-requires-pmm";
        return false;
    }
    if (!format::ApplyVmdSceneTracks(*impl_->session, motion, reason))
        return false;
    return Seek(impl_->frame, reason);
}
bool SceneRuntime::SetPhysicsMode(unsigned mode, std::string& reason) {
    if (!impl_ || impl_->failed || mode > 3) {
        reason = "invalid-physics-mode";
        return false;
    }
    if (impl_->session)
        return Edit([&](auto& doc) { doc.physics.playbackMode = std::uint8_t(mode); }, reason);
    impl_->simulation.physicsMode = int(mode);
    return Seek(impl_->frame, reason);
}
bool SceneRuntime::Reset(std::string& reason) {
    return Seek(0, reason);
}
bool SceneRuntime::StartPlayback(std::string& reason) {
    if (!impl_ || impl_->failed) {
        reason = "scene-not-ready";
        return false;
    }
    if (impl_->session && (!impl_->session->StartPlayback() ||
                           !impl_->session->Advance(impl_->simulation.cursorSeconds))) {
        reason = "scene-playback-start-failed";
        return false;
    }
    for (auto& pair : impl_->items)
        pair.second.timeline.Start(impl_->simulation.cursorSeconds, impl_->simulation.physicsMode,
                                   *pair.second.physics);
    impl_->playing = true;
    impl_->Gravity();
    reason.clear();
    return true;
}
void SceneRuntime::Pause() {
    if (impl_)
        impl_->playing = false;
}
bool SceneRuntime::Advance(float target, float delta, std::string& reason) {
    if (!impl_ || impl_->failed || !impl_->playing || !std::isfinite(target) ||
        !std::isfinite(delta) || delta < 0 || target < impl_->simulation.cursorSeconds ||
        double(target) * 30 > std::numeric_limits<int>::max()) {
        reason = "invalid-scene-advance";
        return false;
    }
    impl_->simulation.remainingSeconds = delta;
    if (!RunCpuSimulationCatchup(impl_->simulation, impl_->Context(), target, -1,
                                 {impl_.get(), Impl::AdvanceTracks, nullptr}) ||
        impl_->failed) {
        reason = impl_->failure;
        return false;
    }
    return impl_->Frame(delta, reason);
}
bool SceneRuntime::Tick(float delta, std::string& reason) {
    if (!impl_ || impl_->failed || !std::isfinite(delta) || delta < 0) {
        reason = "invalid-scene-delta";
        return false;
    }
    impl_->simulation.remainingSeconds = delta;
    return impl_->Frame(delta, reason);
}
bool SceneRuntime::Edit(const std::function<void(format::PmmSceneDocument&)>& change,
                        std::string& reason) {
    if (!impl_ || !impl_->session || impl_->failed) {
        reason = "scene-edit-requires-pmm";
        return false;
    }
    auto candidate = *impl_->session;
    if (!candidate.Edit(change)) {
        reason = "scene-edit-rejected";
        return false;
    }
    auto previous = *impl_->session;
    impl_->session = std::move(candidate);
    if (!impl_->Validate(reason)) {
        impl_->session = std::move(previous);
        std::string ignored;
        impl_->Validate(ignored);
        return false;
    }
    impl_->simulation.physicsMode = impl_->session->Document().physics.playbackMode;
    return Seek(impl_->session->Pose().frame, reason);
}
bool SceneRuntime::UnloadModel(std::uint8_t slot, std::string& reason) {
    if (!impl_ || !impl_->items.count(slot)) {
        reason = "model-not-loaded";
        return false;
    }
    if (impl_->session) {
        const auto& document = impl_->session->Document();
        const auto references = [&](const auto& track) {
            for (const auto& key : track.dense)
                if (key.parentModel == slot)
                    return true;
            for (const auto& key : track.sparse)
                if (key.value.parentModel == slot)
                    return true;
            return false;
        };
        if (document.envelope.header.version == format::PmmVersion::V2 &&
            references(document.cameraKeys)) {
            reason = "model-still-referenced";
            return false;
        }
        for (const auto& parent : document.v1CameraParents)
            if (parent.model == slot) {
                reason = "model-still-referenced";
                return false;
            }
        for (const auto& accessory : document.accessories)
            if (accessory.state.parentModel == slot || references(accessory.keyRecords)) {
                reason = "model-still-referenced";
                return false;
            }
        for (const auto& model : impl_->session->Document().models) {
            const auto referenced = [&](const auto& key) {
                for (const auto& ref : key.rigidStates)
                    if (ref.modelIndex == slot)
                        return true;
                return false;
            };
            for (const auto& key : model.displayKeys.dense)
                if (referenced(key)) {
                    reason = "model-still-referenced";
                    return false;
                }
            for (const auto& key : model.displayKeys.sparse)
                if (referenced(key.value)) {
                    reason = "model-still-referenced";
                    return false;
                }
        }
    }
    impl_->items.erase(slot);
    auto& order = impl_->order;
    order.erase(std::remove(order.begin(), order.end(), slot), order.end());
    reason.clear();
    return true;
}
bool SceneRuntime::AttachAccessory(std::uint8_t slot,
                                   std::shared_ptr<const AccessoryResource> resource,
                                   std::string& reason) {
    if (!Session() || !resource) {
        reason = "accessory-resource-unavailable";
        return false;
    }
    const auto& entries = Session()->Document().accessories;
    if (std::none_of(entries.begin(), entries.end(),
                     [&](const auto& entry) { return entry.slot == slot; })) {
        reason = "accessory-not-in-scene";
        return false;
    }
    impl_->accessories[slot] = std::move(resource);
    reason.clear();
    return true;
}
bool SceneRuntime::UnloadAccessory(std::uint8_t slot, std::string& reason) {
    if (!impl_ || !impl_->accessories.erase(slot)) {
        reason = "accessory-not-loaded";
        return false;
    }
    reason.clear();
    return true;
}
std::shared_ptr<const AccessoryResource> SceneRuntime::Accessory(std::uint8_t slot) const {
    if (!impl_)
        return {};
    const auto found = impl_->accessories.find(slot);
    return found == impl_->accessories.end() ? nullptr : found->second;
}
const ModelSnapshot* SceneRuntime::Snapshot(std::uint8_t slot) const {
    return impl_ && impl_->items.count(slot) ? &impl_->items.at(slot).instance->Snapshot()
                                             : nullptr;
}
std::shared_ptr<const ModelResource> SceneRuntime::Resource(std::uint8_t slot) const {
    return impl_ && impl_->items.count(slot) ? impl_->items.at(slot).resource : nullptr;
}
std::vector<std::array<float, 38>> SceneRuntime::Bodies(std::uint8_t slot) const {
    return impl_ && impl_->items.count(slot) ? impl_->items.at(slot).physics->Snapshot()
                                             : std::vector<std::array<float, 38>>{};
}
const format::SceneSession* SceneRuntime::Session() const {
    return impl_ && impl_->session ? &*impl_->session : nullptr;
}
const CpuSimulationState& SceneRuntime::Simulation() const {
    static const CpuSimulationState empty;
    return impl_ ? impl_->simulation : empty;
}
const SceneCounters& SceneRuntime::Counters() const {
    static const SceneCounters empty;
    return impl_ ? impl_->counters : empty;
}
bool SceneRuntime::BoneAttachment(int slot, int index, cpu_math::Matrix& output,
                                  std::string& reason) const {
    if (slot < 0 || slot > 254 || !Snapshot(std::uint8_t(slot)) || index < 0 ||
        std::size_t(index) >= Snapshot(std::uint8_t(slot))->bones.size()) {
        reason = "invalid-bone-attachment";
        return false;
    }
    const auto& bone = Resource(std::uint8_t(slot))->Document().bones[index].fields;
    output = cpu_math::Multiply(
        cpu_math::Translation({bone.position[0], bone.position[1], bone.position[2]}),
        Snapshot(std::uint8_t(slot))->bones[index].skin);
    reason.clear();
    return true;
}
bool SceneRuntime::AccessoryPlacement(std::uint8_t slot, cpu_math::Matrix& output,
                                      std::string& reason) const {
    if (!Session()) {
        reason = "accessory-requires-pmm";
        return false;
    }
    const auto& poses = Session()->Pose().accessories;
    const auto found =
        std::find_if(poses.begin(), poses.end(), [&](const auto& p) { return p.slot == slot; });
    if (found == poses.end()) {
        reason = "accessory-not-loaded";
        return false;
    }
    const auto& p = *found;
    using namespace cpu_math;
    auto matrix = Identity();
    matrix[0] = matrix[5] = matrix[10] = p.scale * 10;
    matrix = Multiply(matrix, RotationZ(p.rotation[2]));
    matrix = Multiply(matrix, RotationX(p.rotation[0]));
    matrix = Multiply(matrix, RotationY(p.rotation[1]));
    matrix = Multiply(matrix, Translation(p.position));
    if (p.parentModel >= 0) {
        const int index = std::max(0, p.parentBone);
        Matrix attachment;
        if (!BoneAttachment(p.parentModel, index, attachment, reason))
            return false;
        const auto& bone = Resource(std::uint8_t(p.parentModel))->Document().bones[index].fields;
        matrix =
            Multiply(matrix, Translation({bone.position[0], bone.position[1], bone.position[2]}));
        matrix = Multiply(matrix, Snapshot(std::uint8_t(p.parentModel))->bones[index].skin);
    }
    for (float value : matrix)
        if (!std::isfinite(value)) {
            reason = "nonfinite-accessory-placement";
            return false;
        }
    output = matrix;
    reason.clear();
    return true;
}
}
