#include "mikudancestudio/runtime_cpu_scene.hpp"
#include "mikudancestudio/runtime_cpu_tracks.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/model_cpu_bridge.hpp"
#include "btBulletDynamicsCommon.h"
#include <algorithm>
#include <cmath>

namespace mikudancestudio {
struct RuntimeCpuScene::Impl {
    std::unique_ptr<MMDApp> cpu = std::make_unique<MMDApp>();
    PhysicsScene physics{};
    std::array<std::unique_ptr<RuntimeCpuModel>, 255> models;
    std::array<mdl::ModelRecord*, 255> slots{};
    std::vector<std::uint8_t> order;
    std::optional<format::SceneSession> session;
    CpuSimulationState simulation;
    bool playing = false, failed = false;
    ~Impl() {
        for (auto i = models.rbegin(); i != models.rend(); ++i)
            i->reset();
        DisposePhysicsWorld(&physics);
    }
    CpuSimulationContext Context() {
        return {{slots.data(), slots.size()}, physics.world, LegacyCpuSimulationOps()};
    }
    bool Apply() {
        if (!session)
            return true;
        const auto& document = session->Document();
        const auto& poses = session->Pose().models;
        if (poses.size() != document.models.size())
            return false;
        for (std::size_t n = 0; n < poses.size(); ++n)
            if (slots[poses[n].slot] &&
                !ApplyMappedCpuPmmPose(slots[poses[n].slot], document.models[n], poses[n],
                                       document.envelope.header.version))
                return false;
        return true;
    }
    void Gravity() {
        if (session) {
            const auto& g = session->Pose().gravity;
            simulation.gravityMagnitude = g.acceleration;
            simulation.gravityDirection = g.direction;
            simulation.gravityNoise = g.noise;
            simulation.gravityNoiseEnabled = g.noiseEnabled != 0;
        }
    }
    static void AdvanceTracks(void* pointer, CpuSimulationState& state, bool substep) {
        auto& self = *static_cast<Impl*>(pointer);
        for (auto* raw : self.slots)
            if (raw)
                AdvanceModelKeyframes(raw, state.cursorSeconds, state.physicsMode);
        if (self.session) {
            // The portable track state advances at every model tick; gravity
            // becomes applied state only at the final (non-substep) callback.
            if (!self.session->Advance(state.cursorSeconds) || !self.Apply())
                self.failed = true;
            if (!substep)
                self.Gravity();
        }
    }
    bool Validate(const format::SceneSession& value, std::string& reason) {
        if (value.Document().physics.playbackMode > 3) {
            reason = "unsupported_cpu_physics_mode";
            return false;
        }
        const auto version = value.Document().envelope.header.version;
        for (const auto& model : value.Document().models)
            if (slots[model.slot]) {
                if (!ValidateCpuPmmTracks(slots[model.slot], model, version, reason))
                    return false;
                for (const auto& key : model.displayKeys.dense)
                    if (!Bindings(key, reason))
                        return false;
                for (const auto& key : model.displayKeys.sparse)
                    if (!Bindings(key.value, reason))
                        return false;
            }
        return true;
    }
    bool Bindings(const format::PmmDisplayKey& key, std::string& reason) {
        for (const auto& ref : key.rigidStates)
            if (ref.modelIndex >= 0) {
                if (ref.modelIndex >= int(slots.size()) || !slots[ref.modelIndex] ||
                    ref.boneIndex < 0 ||
                    unsigned(ref.boneIndex) >= slots[ref.modelIndex]->boneCount) {
                    reason = "cpu_scene_missing_selector_parent";
                    return false;
                }
            }
        return true;
    }
    bool Publish(std::string& reason) {
        for (const auto& model : session->Document().models)
            if (slots[model.slot] &&
                !PublishCpuPmmTracks(slots[model.slot], model,
                                     session->Document().envelope.header.version, reason))
                return false;
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
            return slots[a]->comboSelIndex2 < slots[b]->comboSelIndex2;
        });
        return true;
    }
    bool Geometry(std::string& reason) {
        for (auto slot : order) {
            auto& model = *slots[slot];
            if (!model.loadComplete)
                continue;
            std::vector<render::ModelMatrix> matrices(model.boneCount);
            std::vector<float> morphs(model.morphCount);
            for (unsigned n = 0; n < model.boneCount; ++n)
                std::copy_n(model.boneTable[n].matInit, 16, matrices[n].begin());
            for (unsigned n = 0; n < model.morphCount; ++n)
                morphs[n] = model.morphs[n].value;
            if (!models[slot]->Deform(matrices, morphs, 0, false, reason))
                return false;
        }
        return true;
    }
    bool Frame(float delta, std::string& reason) {
        CpuSimulationFrame frame;
        frame.deltaSeconds = delta;
        frame.playbackActive = playing;
        if (failed || !RunCpuSimulationFrame(simulation, Context(), frame)) {
            failed = true;
            reason = "cpu_scene_simulation_failed";
            return false;
        }
        if (!Geometry(reason)) {
            failed = true;
            return false;
        }
        reason.clear();
        return true;
    }
};
RuntimeCpuScene::RuntimeCpuScene() = default;
RuntimeCpuScene::~RuntimeCpuScene() = default;
bool RuntimeCpuScene::Load(format::SceneSession session,
                           const std::map<std::uint8_t, std::filesystem::path>& paths,
                           std::string& reason, int selected) {
    if (!session.Loaded() || selected < -1 || selected > 254) {
        reason = "invalid_cpu_scene";
        return false;
    }
    auto next = std::make_unique<Impl>();
    if (!SceneConstruct(&next->physics, nullptr)) {
        reason = "cpu_scene_world_creation";
        return false;
    }
    next->session = std::move(session);
    next->simulation.physicsMode = next->session->Document().physics.playbackMode;
    if (next->simulation.physicsMode > 3) {
        reason = "unsupported_cpu_physics_mode";
        return false;
    }
    for (const auto& model : next->session->Document().models) {
        const auto slot = model.slot;
        if (selected >= 0 && selected != slot)
            continue;
        const auto path = paths.find(slot);
        if (path == paths.end() || next->slots[slot]) {
            reason = "cpu_scene_model_resolution";
            return false;
        }
        auto owner = std::make_unique<RuntimeCpuModel>();
        if (!owner->Load(path->second, next->cpu.get(), &next->physics, reason))
            return false;
        next->slots[slot] = owner->Record();
        next->models[slot] = std::move(owner);
        next->order.push_back(slot);
    }
    if (next->order.empty()) {
        reason = "cpu_scene_no_models";
        return false;
    }
    if (!next->Validate(*next->session, reason) || !next->Publish(reason))
        return false;
    // Validate/evaluate a private candidate before replacing a loaded scene.
    const auto frame = next->session->Pose().frame;
    if (!next->session->Seek(frame)) {
        reason = "cpu_scene_initial_seek";
        return false;
    }
    for (auto* raw : next->slots)
        if (raw)
            SeekModelFrame(*raw, int(frame), next->simulation.physicsMode);
    if (!next->Apply()) {
        reason = "cpu_scene_initial_pose";
        return false;
    }
    next->Gravity();
    next->simulation.cursorSeconds = float(frame) / 30.f;
    next->simulation.resetPending = 1;
    if (!next->Frame(0, reason))
        return false;
    impl = std::move(next);
    reason.clear();
    return true;
}
bool RuntimeCpuScene::LoadModel(const std::filesystem::path& path, std::string& reason) {
    auto next = std::make_unique<Impl>();
    if (!SceneConstruct(&next->physics, nullptr)) {
        reason = "cpu_scene_world_creation";
        return false;
    }
    auto model = std::make_unique<RuntimeCpuModel>();
    if (!model->Load(path, next->cpu.get(), &next->physics, reason))
        return false;
    next->slots[0] = model->Record();
    next->models[0] = std::move(model);
    next->order.push_back(0);
    next->simulation.physicsMode = 2;
    next->simulation.resetPending = 1;
    SeekModelFrame(*next->slots[0], 0, 2);
    if (!next->Frame(0, reason))
        return false;
    impl = std::move(next);
    reason.clear();
    return true;
}
bool RuntimeCpuScene::Seek(std::uint32_t frame, std::string& reason) {
    if (!impl || impl->failed || frame > std::uint32_t(INT_MAX)) {
        reason = "cpu_scene_invalid_seek";
        return false;
    }
    if (impl->session && !impl->session->Seek(frame)) {
        reason = "cpu_scene_track_seek";
        return false;
    }
    impl->playing = false;
    for (auto* raw : impl->slots)
        if (raw)
            SeekModelFrame(*raw, int(frame), impl->simulation.physicsMode);
    if (!impl->Apply()) {
        impl->failed = true;
        reason = "cpu_scene_pose_shape";
        return false;
    }
    impl->Gravity();
    impl->simulation.cursorSeconds = float(frame) / 30.f;
    impl->simulation.remainingSeconds = 0;
    impl->simulation.resetPending = 1;
    return impl->Frame(0, reason);
}
bool RuntimeCpuScene::StartPlayback(std::string& reason) {
    if (!impl || impl->failed) {
        reason = "cpu_scene_not_ready";
        return false;
    }
    if (impl->playing)
        return true;
    if (impl->session && (!impl->session->StartPlayback() ||
                          !impl->session->Advance(impl->simulation.cursorSeconds))) {
        reason = "cpu_scene_start_tracks";
        return false;
    }
    for (auto* raw : impl->slots)
        if (raw)
            InitModelTrackCursors(raw, impl->simulation.cursorSeconds,
                                  impl->simulation.physicsMode);
    if (!impl->Apply()) {
        impl->failed = true;
        reason = "cpu_scene_start_pose";
        return false;
    }
    impl->Gravity();
    impl->playing = true;
    reason.clear();
    return true;
}
void RuntimeCpuScene::Pause() {
    if (impl)
        impl->playing = false;
}
bool RuntimeCpuScene::Advance(float target, float delta, std::string& reason) {
    if (!impl || impl->failed || !impl->playing || !std::isfinite(target) ||
        target < impl->simulation.cursorSeconds || double(target) * 30 > INT_MAX ||
        !std::isfinite(delta) || delta < 0) {
        reason = "cpu_scene_invalid_advance";
        return false;
    }
    impl->simulation.remainingSeconds = delta;
    if (!RunCpuSimulationCatchup(impl->simulation, impl->Context(), target, -1,
                                 {impl.get(), Impl::AdvanceTracks, nullptr}) ||
        impl->failed) {
        impl->failed = true;
        reason = "cpu_scene_track_advance";
        return false;
    }
    return impl->Frame(delta, reason);
}
bool RuntimeCpuScene::Tick(float delta, std::string& reason) {
    if (!impl || impl->failed || !std::isfinite(delta) || delta < 0) {
        reason = "cpu_scene_invalid_delta";
        return false;
    }
    impl->simulation.remainingSeconds = delta;
    return impl->Frame(delta, reason);
}
bool RuntimeCpuScene::Edit(const std::function<void(format::PmmSceneDocument&)>& change,
                           std::string& reason) {
    if (!impl || impl->failed || !impl->session) {
        reason = "cpu_scene_edit_requires_pmm";
        return false;
    }
    auto candidate = *impl->session;
    if (!candidate.Edit(change) || !impl->Validate(candidate, reason)) {
        if (reason.empty())
            reason = "cpu_scene_edit_rejected";
        return false;
    }
    const auto frame = candidate.Pose().frame;
    format::PmmSceneEvaluation checked;
    if (!format::EvaluatePmmSceneFrame(candidate.Document(), frame, checked)) {
        reason = "cpu_scene_edit_evaluation";
        return false;
    }
    impl->session = std::move(candidate);
    impl->simulation.physicsMode = impl->session->Document().physics.playbackMode;
    if (!impl->Publish(reason)) {
        impl->failed = true;
        return false;
    }
    return Seek(frame, reason);
}
const format::SceneSession* RuntimeCpuScene::Session() const {
    return impl && impl->session ? &*impl->session : nullptr;
}
RuntimeCpuModel* RuntimeCpuScene::Model(std::uint8_t slot) {
    return impl && slot < impl->models.size() ? impl->models[slot].get() : nullptr;
}
bool RuntimeCpuScene::Visible(std::uint8_t slot) const {
    return impl && slot < impl->slots.size() && impl->slots[slot] &&
           impl->slots[slot]->loadComplete != 0;
}
const std::vector<std::uint8_t>& RuntimeCpuScene::Order() const {
    static const std::vector<std::uint8_t> empty;
    return impl ? impl->order : empty;
}
const CpuSimulationState& RuntimeCpuScene::Simulation() const {
    static const CpuSimulationState empty;
    return impl ? impl->simulation : empty;
}
std::size_t RuntimeCpuScene::BodyCount() const {
    return impl ? std::size_t(impl->physics.world->getNumCollisionObjects() - 1) : 0;
}
bool RuntimeCpuScene::BoneAttachment(std::int32_t slot, std::int32_t index,
                                     render::ModelMatrix& output, std::string& reason) const {
    if (!impl || impl->failed || slot < 0 || slot >= int(impl->slots.size()) ||
        !impl->slots[slot]) {
        reason = "attachment_model_not_loaded";
        return false;
    }
    const auto& model = *impl->slots[slot];
    if (index < 0 || unsigned(index) >= model.boneCount) {
        reason = "attachment_bone_range";
        return false;
    }
    const auto& bone = model.boneTable[index];
    using M = d3dx::D3DXMATRIXF;
    M rest, result, skin;
    auto& math = d3dx::Get();
    math.translation(&rest, bone.position[0], bone.position[1], bone.position[2]);
    std::memcpy(&skin, bone.matInit, 64);
    math.multiply(&result, &rest, &skin);
    render::ModelMatrix next;
    std::memcpy(next.data(), &result, 64);
    for (float v : next)
        if (!std::isfinite(v)) {
            reason = "attachment_nonfinite_matrix";
            return false;
        }
    output = next;
    reason.clear();
    return true;
}
bool RuntimeCpuScene::AccessoryPlacement(std::uint8_t slot, render::ModelMatrix& output,
                                         std::string& reason) const {
    if (!impl || impl->failed || !impl->session) {
        reason = "accessory_requires_pmm";
        return false;
    }
    const auto& poses = impl->session->Pose().accessories;
    const auto found =
        std::find_if(poses.begin(), poses.end(), [&](const auto& p) { return p.slot == slot; });
    if (found == poses.end()) {
        reason = "accessory_slot_not_loaded";
        return false;
    }
    const auto& p = *found;
    using M = d3dx::D3DXMATRIXF;
    M world, part;
    auto& math = d3dx::Get();
    if (!std::isfinite(p.scale)) {
        reason = "accessory_nonfinite_scale";
        return false;
    }
    for (float v : p.position)
        if (!std::isfinite(v)) {
            reason = "accessory_nonfinite_position";
            return false;
        }
    for (float v : p.rotation)
        if (!std::isfinite(v)) {
            reason = "accessory_nonfinite_rotation";
            return false;
        }
    math.scaling(&world, p.scale * 10, p.scale * 10, p.scale * 10);
    math.rotZ(&part, p.rotation[2]);
    math.multiply(&world, &world, &part);
    math.rotX(&part, p.rotation[0]);
    math.multiply(&world, &world, &part);
    math.rotY(&part, p.rotation[1]);
    math.multiply(&world, &world, &part);
    math.translation(&part, p.position[0], p.position[1], p.position[2]);
    math.multiply(&world, &world, &part);
    if (p.parentModel >= 0) {
        render::ModelMatrix attachment;
        if (!BoneAttachment(p.parentModel, std::max(0, p.parentBone), attachment, reason))
            return false;
        // Preserve the native sequence of two multiplies (rest then skin).
        const auto& bone = impl->slots[p.parentModel]->boneTable[std::max(0, p.parentBone)];
        math.translation(&part, bone.position[0], bone.position[1], bone.position[2]);
        math.multiply(&world, &world, &part);
        std::memcpy(&part, bone.matInit, 64);
        math.multiply(&world, &world, &part);
    }
    render::ModelMatrix next;
    std::memcpy(next.data(), &world, 64);
    for (float v : next)
        if (!std::isfinite(v)) {
            reason = "accessory_nonfinite_matrix";
            return false;
        }
    output = next;
    reason.clear();
    return true;
}
bool RuntimeCpuScene::FrameInputs(render::ModelFrameInputs& output, std::string& reason) const {
    if (!impl || impl->failed) {
        reason = "cpu_scene_not_ready";
        return false;
    }
    auto frame = output;
    frame.world = frame.lightRotation = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    frame.scene.camera.distance = -45;
    frame.scene.camera.fov = 45;
    frame.scene.camera.parentModel = -1;
    frame.scene.lightState = {.6f, .6f, .6f, -.5f, -1, -.5f};
    if (impl->session) {
        const auto& session = *impl->session;
        const auto& doc = session.Document();
        frame.scene.frame = session.Pose().frame;
        frame.scene.camera = session.Pose().camera;
        frame.scene.lightState = session.Pose().lightState;
        if (doc.envelope.header.version == format::PmmVersion::V1) {
            // V1 parent pairs live in an optional tail indexed by disk key.
            auto& camera = frame.scene.camera;
            camera.parentModel = -1;
            camera.parentBone = 0;
            std::uint32_t keyIndex = 0;
            const auto* key = &doc.cameraKeys.dense[0];
            for (std::size_t steps = 0; key->next && steps <= doc.cameraKeys.sparse.size();
                 ++steps) {
                const auto it =
                    std::find_if(doc.cameraKeys.sparse.begin(), doc.cameraKeys.sparse.end(),
                                 [&](const auto& k) { return k.index == int(key->next); });
                if (it == doc.cameraKeys.sparse.end()) {
                    reason = "camera_parent_key_link";
                    return false;
                }
                if (it->value.frame > session.Pose().frame)
                    break;
                keyIndex = unsigned(it->index);
                key = &it->value;
            }
            for (const auto& parent : doc.v1CameraParents)
                if (parent.keyIndex == int(keyIndex)) {
                    camera.parentModel = parent.model;
                    camera.parentBone = parent.bone;
                    break;
                }
        }
        auto& c = frame.scene.camera;
        std::array<float, 3> focus = c.position;
        using M = d3dx::D3DXMATRIXF;
        M t, x, y, z, rotation, attachment, world;
        auto& math = d3dx::Get();
        std::memcpy(&attachment, frame.world.data(), 64);
        if (c.parentModel >= 0) {
            render::ModelMatrix bone;
            if (!BoneAttachment(c.parentModel, c.parentBone, bone, reason))
                return false;
            std::copy_n(bone.begin() + 12, 3, focus.begin());
            if ((doc.appearance.followCamera || doc.timeline.cameraReferenceMode) &&
                !doc.appearance.suppressCameraTransform) {
                const auto* raw = impl->slots[c.parentModel]->boneTable[c.parentBone].matInit;
                std::memcpy(&attachment, raw, 64);
                attachment.m[0][3] = attachment.m[1][3] = attachment.m[2][3] = 0;
                attachment.m[3][0] = attachment.m[3][1] = attachment.m[3][2] = 0;
                attachment.m[3][3] = 1;
            }
        }
        if (!doc.appearance.followCamera && !doc.timeline.cameraReferenceMode) {
            // Absent older tails have no saved basis; identity is the native default.
            const auto& basis = doc.appearance.cameraAttachmentBasis;
            if (std::any_of(basis.begin(), basis.end(), [](float v) { return v != 0; }))
                std::memcpy(&attachment, basis.data(), 64);
        }
        math.translation(&t, -focus[0], -focus[1], -focus[2]);
        math.rotY(&y, c.rotation[1]);
        math.rotX(&x, c.rotation[0]);
        math.rotZ(&z, c.rotation[2]);
        math.multiply(&rotation, &y, &x);
        math.multiply(&rotation, &rotation, &z);
        math.multiply(&rotation, &attachment, &rotation);
        math.multiply(&world, &t, &rotation);
        std::memcpy(frame.world.data(), &world, 64);
        std::memcpy(frame.lightRotation.data(), &rotation, 64);
    }
    for (const auto* matrix : {&frame.world, &frame.lightRotation})
        for (float v : *matrix)
            if (!std::isfinite(v)) {
                reason = "camera_nonfinite_matrix";
                return false;
            }
    output = std::move(frame);
    reason.clear();
    return true;
}
bool RuntimeCpuScene::PrepareEdges(const render::ModelFrameInputs& frame, std::string& reason) {
    if (!impl || impl->failed) {
        reason = "cpu_scene_not_ready";
        return false;
    }
    const auto eye = frame.view.editorNavigation
                         ? frame.view.editorEye
                         : std::array<float, 3>{0, 0, frame.scene.camera.distance};
    for (auto slot : impl->order) {
        auto* raw = impl->slots[slot];
        if (!impl->models[slot]->Geometry())
            continue;
        if (!UpdateCpuOnlyModelGeometry(
                impl->cpu.get(), raw,
                CpuModelEdgeDistance(raw, frame.world.data(), eye.data(), frame.scene.camera.fov),
                false)) {
            reason = "cpu_scene_edge_deformation";
            return false;
        }
    }
    reason.clear();
    return true;
}
bool RuntimeCpuScene::Snapshot(std::uint8_t slot, CpuSolvedModel& output) const {
    if (!impl || slot >= impl->slots.size() || !impl->slots[slot])
        return false;
    const auto& m = *impl->slots[slot];
    CpuSolvedModel result;
    result.slot = slot;
    result.visible = m.loadComplete;
    result.displayCursor = m.displayKeyCursor;
    result.displayActive = m.displayTrackActive;
    for (unsigned n = 0; n < m.boneCount; ++n) {
        const auto& source = m.boneTable[n];
        CpuSolvedBone bone;
        std::copy_n(source.matInit, 16, bone.skin.begin());
        std::copy_n(source.matLocal, 16, bone.local.begin());
        std::copy_n(source.matWorld, 16, bone.world.begin());
        std::copy_n(source.matExtra, 16, bone.extra.begin());
        std::copy_n(source.trans, 3, bone.pose.begin());
        std::copy_n(source.rotQuat, 4, bone.pose.begin() + 3);
        std::copy_n(source.ikBackup, 7, bone.backup.begin());
        std::copy_n(source.ikWorkingPos, 3, bone.working.begin());
        std::copy_n(source.ikWorkingQuat, 4, bone.working.begin() + 3);
        bone.cursor = m.boneKeyCursors[n];
        bone.active = m.boneTrackActive[n];
        bone.armedFrame = source.rigidIdx;
        bone.physicsDisabled = source.physicsDisabled;
        result.bones.push_back(bone);
    }
    for (unsigned n = 0; n < m.morphCount; ++n)
        result.morphs.push_back(m.morphs[n].value);
    for (unsigned n = 0; n < m.ikChainCount; ++n)
        result.ikEnabled.push_back(m.ikChains[n].enabled);
    for (unsigned n = 0; n < m.rigidCount; ++n) {
        const auto* body = static_cast<btRigidBody*>(m.rigidTable[n].body);
        std::array<float, 38> state{};
        body->getWorldTransform().getOpenGLMatrix(state.data());
        body->getInterpolationWorldTransform().getOpenGLMatrix(state.data() + 16);
        for (unsigned c = 0; c < 3; ++c) {
            state[32 + c] = body->getLinearVelocity()[c];
            state[35 + c] = body->getAngularVelocity()[c];
        }
        result.bodies.push_back(state);
    }
    output = std::move(result);
    return true;
}
}
