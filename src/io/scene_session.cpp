#include "mikudancestudio/scene_session.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace mikudancestudio::format {
namespace {
struct Bytes {
    std::vector<std::uint8_t> data;
    std::size_t offset = 0;
};
int ReadBytes(void* context, void* destination, std::size_t length) {
    auto& bytes = *static_cast<Bytes*>(context);
    const auto count = std::min(length, bytes.data.size() - bytes.offset);
    if (count)
        std::memcpy(destination, bytes.data.data() + bytes.offset, count);
    bytes.offset += count;
    return static_cast<int>(count);
}
bool AppendBytes(void* context, const void* source, std::size_t length) {
    auto& bytes = *static_cast<Bytes*>(context);
    const auto* first = static_cast<const std::uint8_t*>(source);
    bytes.data.insert(bytes.data.end(), first, first + length);
    return true;
}
bool PmdLayout(const ModelDocument& model, PmmModelLayout& layout) {
    if (model.kind != ModelKind::Pmd)
        return false;
    layout.boneCount = static_cast<std::uint32_t>(model.pmd.bones.size());
    layout.morphCount = static_cast<std::uint32_t>(model.pmd.morphs.size());
    layout.ikCount = static_cast<std::uint32_t>(model.pmd.ikChains.size());
    return true;
}
struct ModelResolver {
    ResolveSceneModel resolve = nullptr;
    void* context = nullptr;
    std::vector<std::pair<std::uint8_t, ModelDocument>> loaded;
};
bool ResolveV1(void* context, std::uint8_t slot, const std::array<char, 20>&,
               const std::array<char, 256>& path, PmmModelLayout& layout) {
    auto& resolver = *static_cast<ModelResolver*>(context);
    if (!resolver.resolve)
        return false;
    ModelDocument model;
    if (!resolver.resolve(resolver.context, slot, path, model) || !PmdLayout(model, layout))
        return false;
    resolver.loaded.emplace_back(slot, std::move(model));
    return true;
}
bool ResolveStoredV1(void* context, std::uint8_t slot, const std::array<char, 20>&,
                     const std::array<char, 256>&, PmmModelLayout& layout) {
    const auto& session = *static_cast<const SceneSession*>(context);
    const auto& scene = session.Document();
    const auto& models = session.Models();
    for (std::size_t i = 0; i < scene.models.size(); ++i)
        if (scene.models[i].slot == slot && models[i])
            return PmdLayout(*models[i], layout);
    return false;
}
PmmSceneEvaluation CurrentPose(const PmmSceneDocument& scene) {
    PmmSceneEvaluation pose;
    pose.frame = static_cast<std::uint32_t>(scene.timeline.currentFrame);
    pose.lightState = scene.lightState;
    pose.selfShadowMode = scene.selfShadowMode;
    pose.selfShadowDistance = scene.physicsInterval;
    pose.gravity = {scene.physics.gravityNoiseEnabled, scene.physics.gravityNoise,
                    scene.physics.gravityMagnitude, scene.physics.gravityDirection};
    std::copy_n(scene.cameraState.begin(), 3, pose.camera.position.begin());
    std::copy_n(scene.cameraState.begin() + 6, 3, pose.camera.rotation.begin());
    pose.camera.distance = scene.cameraState[5];
    pose.camera.fov = scene.envelope.settings.cameraFov;
    pose.camera.perspective = scene.cameraPerspective;
    if (!scene.cameraKeys.dense.empty()) {
        pose.camera.parentModel = scene.cameraKeys.dense[0].parentModel;
        pose.camera.parentBone = scene.cameraKeys.dense[0].parentBone;
    }
    pose.accessories.reserve(scene.accessories.size());
    for (const auto& accessory : scene.accessories) {
        PmmEvaluatedAccessory value;
        value.slot = accessory.slot;
        value.visible = accessory.state.visible;
        value.shadowEnabled = accessory.state.shadowEnabled;
        value.opacity = accessory.state.opacity;
        value.scale = accessory.state.scale;
        value.parentModel = accessory.state.parentModel;
        value.parentBone = accessory.state.parentBone;
        value.position = accessory.state.position;
        value.rotation = accessory.state.rotation;
        pose.accessories.push_back(value);
    }
    pose.models.reserve(scene.models.size());
    for (const auto& model : scene.models) {
        PmmEvaluatedModel evaluated;
        evaluated.slot = model.slot;
        evaluated.visible = model.selection.loadComplete;
        evaluated.morphValues = model.morphPose;
        evaluated.ikEnabled = model.ikPose;
        evaluated.selectors.resize(model.rigidCount);
        evaluated.bones.reserve(model.bonePose.size());
        for (const auto& saved : model.bonePose) {
            PmmEvaluatedBone bone;
            std::copy_n(saved.components.begin(), 3, bone.translation.begin());
            std::copy_n(saved.components.begin() + 3, 4, bone.rotation.begin());
            bone.physicsDisabled = saved.flags[0];
            evaluated.bones.push_back(bone);
        }
        pose.models.push_back(std::move(evaluated));
    }
    return pose;
}
void StorePose(PmmSceneDocument& scene, const PmmSceneEvaluation& pose) {
    scene.timeline.currentFrame = static_cast<std::int32_t>(pose.frame);
    scene.lightState = pose.lightState;
    scene.selfShadowMode = pose.selfShadowMode;
    scene.physicsInterval = pose.selfShadowDistance;
    scene.physics.gravityNoiseEnabled = pose.gravity.noiseEnabled;
    scene.physics.gravityNoise = pose.gravity.noise;
    scene.physics.gravityMagnitude = pose.gravity.acceleration;
    scene.physics.gravityDirection = pose.gravity.direction;
    std::copy(pose.camera.position.begin(), pose.camera.position.end(), scene.cameraState.begin());
    std::copy(pose.camera.rotation.begin(), pose.camera.rotation.end(),
              scene.cameraState.begin() + 6);
    scene.cameraState[5] = pose.camera.distance;
    scene.cameraPerspective = pose.camera.perspective;
    scene.envelope.settings.cameraFov = pose.camera.fov;
    for (std::size_t i = 0; i < scene.accessories.size(); ++i) {
        auto& state = scene.accessories[i].state;
        const auto& value = pose.accessories[i];
        state.visible = value.visible;
        state.shadowEnabled = value.shadowEnabled;
        state.opacity = value.opacity;
        state.scale = value.scale;
        state.parentModel = value.parentModel;
        state.parentBone = value.parentBone;
        state.position = value.position;
        state.rotation = value.rotation;
    }
    for (std::size_t m = 0; m < scene.models.size(); ++m) {
        auto& model = scene.models[m];
        const auto& evaluated = pose.models[m];
        for (std::size_t i = 0; i < model.bonePose.size(); ++i) {
            auto& current = model.bonePose[i];
            const auto& bone = evaluated.bones[i];
            std::copy(bone.translation.begin(), bone.translation.end(), current.components.begin());
            std::copy(bone.rotation.begin(), bone.rotation.end(), current.components.begin() + 3);
            current.flags[0] = bone.physicsDisabled != 0;
        }
        model.morphPose = evaluated.morphValues;
        model.ikPose = evaluated.ikEnabled;
    }
}
}

bool SceneSession::Load(ReadPmmBytesResult read, void* readContext, ResolveSceneModel resolveModel,
                        void* modelContext, SceneSession& output) {
    if (!read)
        return false;
    SceneSession candidate;
    ModelResolver resolver{resolveModel, modelContext, {}};
    if (!ReadPmmSceneDocument(read, readContext, ResolveV1, &resolver, candidate.scene_) ||
        candidate.scene_.timeline.currentFrame < 0)
        return false;
    candidate.models_.resize(candidate.scene_.models.size());
    if (candidate.scene_.envelope.header.version == PmmVersion::V1) {
        for (std::size_t i = 0; i < candidate.models_.size(); ++i) {
            const auto slot = candidate.scene_.models[i].slot;
            const auto found =
                std::find_if(resolver.loaded.begin(), resolver.loaded.end(),
                             [slot](const auto& item) { return item.first == slot; });
            if (found == resolver.loaded.end())
                return false;
            candidate.models_[i] = std::move(found->second);
        }
    } else if (resolveModel) {
        for (std::size_t i = 0; i < candidate.models_.size(); ++i) {
            ModelDocument model;
            const auto& source = candidate.scene_.models[i];
            if (!resolveModel(modelContext, source.slot, source.path, model) ||
                model.kind == ModelKind::Unknown)
                return false;
            candidate.models_[i] = std::move(model);
        }
    }
    candidate.pose_ = CurrentPose(candidate.scene_);
    candidate.loaded_ = true;
    output = std::move(candidate);
    return true;
}

bool SceneSession::SetRuntimeAppliedLight(const std::array<float, 6>& light) {
    if (!loaded_)
        return false;
    for (auto value : light)
        if (!std::isfinite(value))
            return false;
    runtimeAppliedLight_ = light;
    return true;
}
bool SceneSession::SetRuntimeAppliedCamera(const PmmEvaluatedCamera& camera) {
    if (!loaded_ || !std::isfinite(camera.distance) || !std::isfinite(camera.fov) ||
        camera.perspective > 1)
        return false;
    for (auto value : camera.position)
        if (!std::isfinite(value))
            return false;
    for (auto value : camera.rotation)
        if (!std::isfinite(value))
            return false;
    runtimeAppliedCamera_ = camera;
    return true;
}

bool SceneSession::Seek(std::uint32_t frame) {
    if (!loaded_ || frame > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return false;
    PmmSceneEvaluation evaluated;
    if (!EvaluatePmmSceneFrame(scene_, frame, evaluated))
        return false;
    // Windows seek stores the double-fraction light result, while playback
    // uses float-fraction arithmetic. Rendering observes those few ULP.
    if (!EvaluatePmmLightTrack(scene_.lightKeys, frame, PmmLightInterpolation::FrameSeek,
                               evaluated.lightState))
        return false;
    StorePose(scene_, evaluated);
    pose_ = std::move(evaluated);
    playback_ = {};
    playing_ = false;
    return true;
}

bool SceneSession::StartPlayback() {
    if (!loaded_)
        return false;
    PmmScenePlaybackState started;
    if (!StartPmmScenePlayback(scene_, started))
        return false;
    playback_ = std::move(started);
    pose_ = playback_.pose;
    StorePose(scene_, pose_);
    playing_ = true;
    return true;
}

bool SceneSession::Advance(float seconds) {
    if (!loaded_ || !playing_)
        return false;
    PmmScenePlaybackState advanced = playback_;
    if (!AdvancePmmScenePlayback(scene_, seconds, advanced))
        return false;
    playback_ = std::move(advanced);
    pose_ = playback_.pose;
    StorePose(scene_, pose_);
    return true;
}

bool SceneSession::KinematicWorld(std::uint8_t slot,
                                  std::vector<std::optional<std::array<float, 16>>>& world) const {
    if (!loaded_)
        return false;
    for (std::size_t index = 0; index < scene_.models.size(); ++index)
        if (scene_.models[index].slot == slot && models_[index] && index < pose_.models.size() &&
            pose_.models[index].slot == slot)
            return EvaluatePmmKinematicWorld(*models_[index], pose_.models[index], world);
    return false;
}

bool SceneSession::CameraAttachmentPoint(std::array<float, 3>& point) const {
    if (!loaded_ || pose_.camera.parentModel < 0 || pose_.camera.parentModel > 255)
        return false;
    const auto slot = static_cast<std::uint8_t>(pose_.camera.parentModel);
    std::vector<std::optional<PmmWorldMatrix>> world;
    if (!KinematicWorld(slot, world))
        return false;
    for (std::size_t index = 0; index < scene_.models.size(); ++index)
        if (scene_.models[index].slot == slot && models_[index])
            return PmmAttachedBonePoint(*models_[index], world, pose_.camera.parentBone, point);
    return false;
}

bool SceneSession::AccessoryWorld(std::uint8_t slot, PmmWorldMatrix& world) const {
    if (!loaded_)
        return false;
    const auto accessory =
        std::find_if(pose_.accessories.begin(), pose_.accessories.end(),
                     [slot](const PmmEvaluatedAccessory& pose) { return pose.slot == slot; });
    if (accessory == pose_.accessories.end())
        return false;
    if (accessory->parentModel < 0)
        return PmmAccessoryWorld(*accessory, nullptr, nullptr, world);
    if (accessory->parentModel > 255)
        return false;
    const auto modelSlot = static_cast<std::uint8_t>(accessory->parentModel);
    std::vector<std::optional<PmmWorldMatrix>> bones;
    if (!KinematicWorld(modelSlot, bones))
        return false;
    for (std::size_t index = 0; index < scene_.models.size(); ++index)
        if (scene_.models[index].slot == modelSlot && models_[index])
            return PmmAccessoryWorld(*accessory, &*models_[index], &bones, world);
    return false;
}

bool SceneSession::ReplaceRuntimeModelTracks(std::uint8_t slot, PmmKeyTrack<PmmBoneKey> bones,
                                             PmmKeyTrack<PmmMorphKey> morphs,
                                             PmmKeyTrack<PmmDisplayKey> displays) {
    if (!loaded_)
        return false;
    const auto found =
        std::find_if(scene_.models.begin(), scene_.models.end(),
                     [slot](const PmmSceneModel& model) { return model.slot == slot; });
    if (found == scene_.models.end() || bones.dense.size() != found->layout.boneCount ||
        morphs.dense.size() != found->layout.morphCount || displays.dense.size() != 1)
        return false;
    auto& model = *found;
    auto oldBones = std::move(model.boneKeys);
    auto oldMorphs = std::move(model.morphKeys);
    auto oldDisplays = std::move(model.displayKeys);
    model.boneKeys = std::move(bones);
    model.morphKeys = std::move(morphs);
    model.displayKeys = std::move(displays);
    PmmSceneEvaluation checked;
    if (!EvaluatePmmSceneFrame(scene_, pose_.frame, checked)) {
        model.boneKeys = std::move(oldBones);
        model.morphKeys = std::move(oldMorphs);
        model.displayKeys = std::move(oldDisplays);
        return false;
    }
    model.bones.sparse = static_cast<std::uint32_t>(model.boneKeys.sparse.size());
    model.morphs.sparse = static_cast<std::uint32_t>(model.morphKeys.sparse.size());
    model.displays.sparse = static_cast<std::uint32_t>(model.displayKeys.sparse.size());
    runtimeTracksChanged_ = true;
    playback_ = {};
    playing_ = false;
    return true;
}

bool SceneSession::ReplaceRuntimeCameraTrack(PmmKeyTrack<PmmCameraKey> camera) {
    if (!loaded_ || camera.dense.size() != 1)
        return false;
    auto old = std::move(scene_.cameraKeys);
    scene_.cameraKeys = std::move(camera);
    PmmEvaluatedCamera checked;
    if (!EvaluatePmmCameraTrack(scene_.cameraKeys, pose_.frame, PmmCameraInterpolation::Seek,
                                checked)) {
        scene_.cameraKeys = std::move(old);
        return false;
    }
    scene_.camera.sparse = static_cast<std::uint32_t>(scene_.cameraKeys.sparse.size());
    runtimeTracksChanged_ = true;
    playback_ = {};
    playing_ = false;
    return true;
}

bool SceneSession::ReplaceRuntimeLightTrack(PmmKeyTrack<PmmLightKey> light) {
    if (!loaded_ || light.dense.size() != 1 || scene_.lightKeys.dense.empty())
        return false;
    std::array<float, 6> checked;
    if (!EvaluatePmmLightTrack(light, pose_.frame, PmmLightInterpolation::FrameSeek, checked))
        return false;
    scene_.lightKeys = std::move(light);
    scene_.light.sparse = static_cast<std::uint32_t>(scene_.lightKeys.sparse.size());
    runtimeTracksChanged_ = true;
    playback_ = {};
    playing_ = false;
    return true;
}

bool SceneSession::ReplaceRuntimeAccessoryTrack(std::uint8_t slot,
                                                PmmKeyTrack<PmmAccessoryKey> accessory) {
    if (!loaded_ || accessory.dense.size() != 1)
        return false;
    const auto found =
        std::find_if(scene_.accessories.begin(), scene_.accessories.end(),
                     [slot](const PmmSceneAccessory& item) { return item.slot == slot; });
    if (found == scene_.accessories.end())
        return false;
    auto old = std::move(found->keyRecords);
    found->keyRecords = std::move(accessory);
    PmmEvaluatedAccessory checked;
    if (!EvaluatePmmAccessoryTrack(found->keyRecords, slot, pose_.frame, checked)) {
        found->keyRecords = std::move(old);
        return false;
    }
    found->keys.sparse = static_cast<std::uint32_t>(found->keyRecords.sparse.size());
    runtimeTracksChanged_ = true;
    playback_ = {};
    playing_ = false;
    return true;
}

bool SceneSession::ReplaceRuntimeSelfShadowTrack(PmmKeyTrack<PmmSelfShadowKey> shadow) {
    if (!loaded_ || shadow.dense.size() != 1 || scene_.selfShadowKeys.dense.empty())
        return false;
    auto old = std::move(scene_.selfShadowKeys);
    scene_.selfShadowKeys = std::move(shadow);
    std::uint8_t mode = 0;
    float distance = 0;
    if (!EvaluatePmmSelfShadowTrack(scene_.selfShadowKeys, pose_.frame, mode, distance)) {
        scene_.selfShadowKeys = std::move(old);
        return false;
    }
    scene_.selfShadow.sparse = static_cast<std::uint32_t>(scene_.selfShadowKeys.sparse.size());
    runtimeTracksChanged_ = true;
    playback_ = {};
    playing_ = false;
    return true;
}

bool SceneSession::ReplaceRuntimeGravityTrack(PmmKeyTrack<PmmGravityKey> gravity) {
    if (!loaded_ || gravity.dense.size() != 1 || scene_.gravityKeys.dense.empty())
        return false;
    auto old = std::move(scene_.gravityKeys);
    scene_.gravityKeys = std::move(gravity);
    PmmEvaluatedGravity checked;
    if (!EvaluatePmmGravityTrack(scene_.gravityKeys, pose_.frame, checked)) {
        scene_.gravityKeys = std::move(old);
        return false;
    }
    scene_.gravity.sparse = static_cast<std::uint32_t>(scene_.gravityKeys.sparse.size());
    runtimeTracksChanged_ = true;
    playback_ = {};
    playing_ = false;
    return true;
}

bool SceneSession::Edit(const std::function<void(PmmSceneDocument&)>& change) {
    if (!loaded_ || runtimeTracksChanged_ || !change)
        return false;
    PmmSceneDocument edited = scene_;
    change(edited);
    Bytes bytes;
    const auto v1 = scene_.envelope.header.version == PmmVersion::V1;
    if (!WritePmmSceneDocument(AppendBytes, &bytes, edited, v1 ? ResolveStoredV1 : nullptr,
                               v1 ? this : nullptr))
        return false;
    PmmSceneDocument checked;
    if (!ReadPmmSceneDocument(ReadBytes, &bytes, v1 ? ResolveStoredV1 : nullptr,
                              v1 ? this : nullptr, checked) ||
        checked.timeline.currentFrame < 0)
        return false;
    scene_ = std::move(checked);
    pose_ = CurrentPose(scene_);
    playback_ = {};
    playing_ = false;
    return true;
}

bool SceneSession::Save(WritePmmBytes write, void* context) const {
    if (!loaded_ || runtimeTracksChanged_ || !write)
        return false;
    Bytes bytes;
    const auto v1 = scene_.envelope.header.version == PmmVersion::V1;
    if (!WritePmmSceneDocument(AppendBytes, &bytes, scene_, v1 ? ResolveStoredV1 : nullptr,
                               v1 ? const_cast<SceneSession*>(this) : nullptr))
        return false;
    return write(context, bytes.data.data(), bytes.data.size());
}
}
