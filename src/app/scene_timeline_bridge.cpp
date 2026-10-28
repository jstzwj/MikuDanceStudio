#include "mikudancestudio/runtime_cpu_tracks.hpp"
#include "mikudancestudio/scene_timeline_bridge.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mikudancestudio {
namespace {
template <class Live, class Portable, class Convert>
bool CopyLinkedTrack(const Live* keys, std::uint32_t heads, std::uint32_t capacity, Convert convert,
                     format::PmmKeyTrack<Portable>& output) {
    if (heads == 0)
        return true;
    if (!keys || heads > capacity)
        return false;
    output.dense.reserve(heads);
    std::vector<bool> visited(capacity);
    for (std::uint32_t head = 0; head < heads; ++head) {
        visited[head] = true;
        output.dense.push_back(convert(keys[head]));
    }
    for (std::uint32_t head = 0; head < heads; ++head) {
        std::uint32_t index = head;
        for (std::uint32_t steps = 0; steps < capacity; ++steps) {
            const auto next = keys[index].next;
            if (next == 0)
                break;
            if (next >= capacity || visited[next] || keys[next].previous != index ||
                keys[next].frame <= keys[index].frame)
                return false;
            visited[next] = true;
            output.sparse.push_back({static_cast<std::int32_t>(next), 0, convert(keys[next])});
            index = next;
        }
        if (keys[index].next != 0)
            return false;
    }
    return true;
}
format::PmmBoneKey Bone(const mdl::BoneKey& key) {
    format::PmmBoneKey value;
    value.frame = key.frame;
    value.previous = key.previous;
    value.next = key.next;
    std::copy_n(key.interpolation, 16, value.interpolation.begin());
    std::copy_n(key.position, 3, value.position.begin());
    std::copy_n(key.rotation, 4, value.rotation.begin());
    value.allocated = key.allocated;
    value.physicsDisabled = key.physicsDisabled;
    return value;
}
format::PmmMorphKey Morph(const mdl::MorphKey& key) {
    return {key.frame, key.previous, key.next, key.value, key.allocated};
}
format::PmmCameraKey Camera(const mdl::CameraKey& key) {
    format::PmmCameraKey value;
    value.frame = key.frame;
    value.previous = key.previous;
    value.next = key.next;
    value.distance = key.distance;
    std::copy_n(key.eye, 3, value.eye.begin());
    std::copy_n(key.target, 3, value.target.begin());
    for (int row = 0; row < 4; ++row)
        std::copy_n(key.interpolation[row], 6, value.interpolation[row].begin());
    value.perspective = key.perspective;
    value.fov = key.fov;
    value.parentModel = key.parentModel;
    value.parentBone = key.parentBone;
    value.selected = key.selected;
    return value;
}
format::PmmAccessoryKey Accessory(const mdl::AccessoryKey& key) {
    format::PmmAccessoryKey value;
    value.frame = key.frame;
    value.previous = key.previous;
    value.next = key.next;
    value.visible = key.visible;
    value.shadowEnabled = key.shadowEnabled;
    value.selected = key.selected;
    value.opacity = key.opacity;
    value.parentModel = key.parentModel;
    value.parentBone = key.parentBone;
    std::copy_n(key.position, 3, value.position.begin());
    std::copy_n(key.rotation, 3, value.rotation.begin());
    value.scale = key.scale;
    return value;
}
format::PmmLightKey Light(const mdl::LightKey& key) {
    format::PmmLightKey value;
    value.frame = key.frame;
    value.previous = key.previous;
    value.next = key.next;
    value.selected = key.selected;
    std::copy_n(key.direction, 3, value.direction.begin());
    std::copy_n(key.color, 3, value.color.begin());
    return value;
}
format::PmmSelfShadowKey SelfShadow(const mdl::SelfShadowKey& key) {
    return {key.frame, key.previous, key.next, key.mode, key.selected, key.distance};
}
format::PmmGravityKey Gravity(const mdl::GravityKey& key) {
    format::PmmGravityKey result;
    result.frame = key.frame;
    result.previous = key.previous;
    result.next = key.next;
    result.noiseEnabled = key.noiseEnabled;
    result.noise = key.noise;
    result.acceleration = key.acceleration;
    std::copy_n(key.direction, 3, result.direction.begin());
    result.selected = key.selected;
    return result;
}
struct DisplayConverter {
    std::uint32_t ikCount = 0, selectorCount = 0;
    format::PmmDisplayKey operator()(const mdl::DisplayKey& key) const {
        format::PmmDisplayKey value;
        value.frame = key.frame;
        value.previous = key.previous;
        value.next = key.next;
        value.visible = key.visible;
        value.allocated = key.allocated;
        if (ikCount && mdl::IkStates(key))
            value.ikEnabled.assign(mdl::IkStates(key), mdl::IkStates(key) + ikCount);
        if (selectorCount && mdl::SelectorStates(key)) {
            value.rigidStates.reserve(selectorCount);
            for (std::uint32_t i = 0; i < selectorCount; ++i) {
                const auto& source = mdl::SelectorStates(key)[i];
                value.rigidStates.push_back({source.modelIndex, source.boneIndex});
            }
        }
        return value;
    }
};
bool Active(MMDApp* app, mdl::ModelRecord* model, format::SceneSession*& session,
            std::size_t& index) {
    if (!app || !model)
        return false;
    session = app->PortableSceneSession();
    if (!session)
        return false;
    const auto& models = session->Document().models;
    for (std::size_t i = 0; i < models.size(); ++i)
        if (models[i].slot < kModelSlotCount && app->ModelSlot(models[i].slot) == model) {
            index = i;
            return true;
        }
    return false;
}
bool SyncLive(format::SceneSession& session, std::size_t index, mdl::ModelRecord* raw) {
    const auto& live = *raw;
    const auto& source = session.Document().models[index];
    if (live.boneCount != source.layout.boneCount || live.morphCount != source.layout.morphCount ||
        live.ikChainCount != source.layout.ikCount ||
        (session.Document().envelope.header.version == format::PmmVersion::V2 &&
         live.boneOrderCount != source.rigidCount))
        return false;
    const auto sameBone = [](const mdl::BoneKey& live, const format::PmmBoneKey& saved) {
        return live.frame == saved.frame && live.previous == saved.previous &&
               live.next == saved.next &&
               std::equal(saved.interpolation.begin(), saved.interpolation.end(),
                          live.interpolation) &&
               std::equal(saved.position.begin(), saved.position.end(), live.position) &&
               std::equal(saved.rotation.begin(), saved.rotation.end(), live.rotation) &&
               live.physicsDisabled == saved.physicsDisabled;
    };
    const auto sameMorph = [](const mdl::MorphKey& live, const format::PmmMorphKey& saved) {
        return live.frame == saved.frame && live.previous == saved.previous &&
               live.next == saved.next && live.value == saved.value;
    };
    const auto sameDisplay = [&](const mdl::DisplayKey& key, const format::PmmDisplayKey& saved) {
        if (key.frame != saved.frame || key.previous != saved.previous || key.next != saved.next ||
            key.visible != saved.visible || saved.ikEnabled.size() != live.ikChainCount ||
            saved.rigidStates.size() != source.rigidCount)
            return false;
        if (live.ikChainCount &&
            (!mdl::IkStates(key) ||
             !std::equal(saved.ikEnabled.begin(), saved.ikEnabled.end(), mdl::IkStates(key))))
            return false;
        if (source.rigidCount && !mdl::SelectorStates(key))
            return false;
        for (std::uint32_t i = 0; i < source.rigidCount; ++i) {
            const auto& pair = mdl::SelectorStates(key)[i];
            if (pair.modelIndex != saved.rigidStates[i].modelIndex ||
                pair.boneIndex != saved.rigidStates[i].boneIndex)
                return false;
        }
        return true;
    };
    const auto sameTrack = [](const auto* live, const auto& track, auto equal,
                              std::size_t capacity) {
        if (track.dense.empty() || !live || track.dense.size() > capacity)
            return track.dense.empty();
        for (std::size_t i = 0; i < track.dense.size(); ++i)
            if (!equal(live[i], track.dense[i]))
                return false;
        for (const auto& key : track.sparse)
            if (key.index < 0 || static_cast<std::size_t>(key.index) >= capacity ||
                !equal(live[key.index], key.value))
                return false;
        return true;
    };
    if (sameTrack((raw)->boneKeys, source.boneKeys, sameBone, mdl::kBoneKeyCapacity) &&
        sameTrack((raw)->morphKeys, source.morphKeys, sameMorph, mdl::kMorphKeyCapacity) &&
        sameTrack((raw)->displayKeys, source.displayKeys, sameDisplay, mdl::kDisplayKeyCapacity))
        return true;
    format::PmmKeyTrack<format::PmmBoneKey> bones;
    format::PmmKeyTrack<format::PmmMorphKey> morphs;
    format::PmmKeyTrack<format::PmmDisplayKey> displays;
    if (!CopyLinkedTrack((raw)->boneKeys, live.boneCount, mdl::kBoneKeyCapacity, Bone, bones) ||
        !CopyLinkedTrack((raw)->morphKeys, live.morphCount, mdl::kMorphKeyCapacity, Morph,
                         morphs) ||
        !CopyLinkedTrack((raw)->displayKeys, 1, mdl::kDisplayKeyCapacity,
                         DisplayConverter{live.ikChainCount, source.rigidCount}, displays))
        return false;
    return session.ReplaceRuntimeModelTracks(source.slot, std::move(bones), std::move(morphs),
                                             std::move(displays));
}
bool AllModelsAvailable(MMDApp* app, const format::SceneSession& session) {
    const auto& models = session.Document().models;
    return !models.empty() && std::all_of(models.begin(), models.end(), [app](const auto& model) {
        return model.slot < kModelSlotCount && app->ModelSlot(model.slot) != nullptr;
    });
}
bool SyncAllLive(MMDApp* app, format::SceneSession& session) {
    if (!AllModelsAvailable(app, session))
        return false;
    const auto& models = session.Document().models;
    for (std::size_t i = 0; i < models.size(); ++i)
        if (!SyncLive(session, i, app->ModelSlot(models[i].slot)))
            return false;
    return true;
}
bool SyncLiveCamera(MMDApp* app, format::SceneSession& session) {
    const auto* live = app->CameraKeys();
    if (!live)
        return false;
    const auto& saved = session.Document().cameraKeys;
    const auto same = [](const mdl::CameraKey& key, const format::PmmCameraKey& source) {
        const auto copy = Camera(key);
        return copy.frame == source.frame && copy.previous == source.previous &&
               copy.next == source.next && copy.distance == source.distance &&
               copy.eye == source.eye && copy.target == source.target &&
               copy.interpolation == source.interpolation &&
               copy.perspective == source.perspective && copy.fov == source.fov &&
               copy.parentModel == source.parentModel && copy.parentBone == source.parentBone;
    };
    bool unchanged = saved.dense.size() == 1 && same(live[0], saved.dense[0]);
    if (unchanged)
        for (const auto& key : saved.sparse)
            if (key.index < 0 || key.index >= mdl::kTimelineKeyCapacity ||
                !same(live[key.index], key.value)) {
                unchanged = false;
                break;
            }
    if (unchanged)
        return true;
    format::PmmKeyTrack<format::PmmCameraKey> camera;
    if (!CopyLinkedTrack(live, 1, mdl::kTimelineKeyCapacity, Camera, camera))
        return false;
    return session.ReplaceRuntimeCameraTrack(std::move(camera));
}
bool SyncLiveLight(MMDApp* app, format::SceneSession& session) {
    const auto* live = app->LightKeys();
    const auto& saved = session.Document().lightKeys;
    if (!live || saved.dense.empty())
        return true;
    const auto same = [](const mdl::LightKey& source, const format::PmmLightKey& item) {
        const auto key = Light(source);
        return key.frame == item.frame && key.previous == item.previous && key.next == item.next &&
               key.direction == item.direction && key.color == item.color;
    };
    bool unchanged = saved.dense.size() == 1 && same(live[0], saved.dense[0]);
    if (unchanged)
        for (const auto& key : saved.sparse)
            if (key.index < 0 || key.index >= mdl::kTimelineKeyCapacity ||
                !same(live[key.index], key.value)) {
                unchanged = false;
                break;
            }
    if (unchanged)
        return true;
    format::PmmKeyTrack<format::PmmLightKey> track;
    if (!CopyLinkedTrack(live, 1, mdl::kTimelineKeyCapacity, Light, track))
        return false;
    return session.ReplaceRuntimeLightTrack(std::move(track));
}
bool SyncLiveAccessory(MMDApp* app, format::SceneSession& session,
                       const format::PmmSceneAccessory& source) {
    const auto* live = app->AccessoryKeys(source.slot);
    if (!live || !app->AccessorySlot(source.slot))
        return true;
    const auto& saved = source.keyRecords;
    const auto same = [](const mdl::AccessoryKey& key, const format::PmmAccessoryKey& item) {
        const auto copy = Accessory(key);
        return copy.frame == item.frame && copy.previous == item.previous &&
               copy.next == item.next && copy.visible == item.visible &&
               copy.shadowEnabled == item.shadowEnabled && copy.opacity == item.opacity &&
               copy.parentModel == item.parentModel && copy.parentBone == item.parentBone &&
               copy.position == item.position && copy.rotation == item.rotation &&
               copy.scale == item.scale;
    };
    bool unchanged = saved.dense.size() == 1 && same(live[0], saved.dense[0]);
    if (unchanged)
        for (const auto& key : saved.sparse)
            if (key.index < 0 || key.index >= mdl::kTimelineKeyCapacity ||
                !same(live[key.index], key.value)) {
                unchanged = false;
                break;
            }
    if (unchanged)
        return true;
    format::PmmKeyTrack<format::PmmAccessoryKey> track;
    if (!CopyLinkedTrack(live, 1, mdl::kTimelineKeyCapacity, Accessory, track))
        return false;
    return session.ReplaceRuntimeAccessoryTrack(source.slot, std::move(track));
}
bool SyncAllAccessories(MMDApp* app, format::SceneSession& session) {
    for (const auto& source : session.Document().accessories)
        if (!SyncLiveAccessory(app, session, source))
            return false;
    return true;
}
bool SyncLiveSelfShadow(MMDApp* app, format::SceneSession& session) {
    const auto& saved = session.Document().selfShadowKeys;
    if (saved.dense.empty())
        return true;
    const auto* live = app->ShadowKeys();
    if (!live || saved.dense.size() != 1)
        return false;
    const auto same = [](const mdl::SelfShadowKey& key, const format::PmmSelfShadowKey& item) {
        const auto copy = SelfShadow(key);
        return copy.frame == item.frame && copy.previous == item.previous &&
               copy.next == item.next && copy.mode == item.mode && copy.distance == item.distance;
    };
    bool unchanged = same(live[0], saved.dense[0]);
    if (unchanged)
        for (const auto& key : saved.sparse)
            if (key.index < 0 || key.index >= mdl::kTimelineKeyCapacity ||
                !same(live[key.index], key.value)) {
                unchanged = false;
                break;
            }
    if (unchanged)
        return true;
    format::PmmKeyTrack<format::PmmSelfShadowKey> shadow;
    if (!CopyLinkedTrack(live, 1, mdl::kTimelineKeyCapacity, SelfShadow, shadow))
        return false;
    return session.ReplaceRuntimeSelfShadowTrack(std::move(shadow));
}
bool SyncLiveGravity(MMDApp* app, format::SceneSession& session) {
    const auto& saved = session.Document().gravityKeys;
    if (saved.dense.empty())
        return true;
    const auto* live = app->GravityKeys();
    if (!live || saved.dense.size() != 1)
        return false;
    const auto same = [](const mdl::GravityKey& key, const format::PmmGravityKey& item) {
        const auto copy = Gravity(key);
        return copy.frame == item.frame && copy.previous == item.previous &&
               copy.next == item.next && copy.noiseEnabled == item.noiseEnabled &&
               copy.noise == item.noise && copy.acceleration == item.acceleration &&
               copy.direction == item.direction;
    };
    bool unchanged = same(live[0], saved.dense[0]);
    if (unchanged)
        for (const auto& key : saved.sparse)
            if (key.index < 0 || key.index >= mdl::kTimelineKeyCapacity ||
                !same(live[key.index], key.value)) {
                unchanged = false;
                break;
            }
    if (unchanged)
        return true;
    format::PmmKeyTrack<format::PmmGravityKey> gravity;
    if (!CopyLinkedTrack(live, 1, mdl::kTimelineKeyCapacity, Gravity, gravity))
        return false;
    return session.ReplaceRuntimeGravityTrack(std::move(gravity));
}
void ApplyAllPoses(MMDApp* app, const format::SceneSession& session) {
    const auto& pose = session.Pose().models;
    if (pose.size() != session.Document().models.size())
        return;
    const bool applySelectors =
        session.Document().envelope.header.version == format::PmmVersion::V2;
    for (const auto& model : pose)
        if (model.slot < kModelSlotCount)
            if (auto* raw = app->ModelSlot(model.slot))
                ApplyCpuPmmPose(raw, model, applySelectors);
}
void ApplyAccessoryPose(MMDApp* app, const format::PmmEvaluatedAccessory& pose) {
    auto* target = app->AccessorySlot(pose.slot);
    if (!target)
        return;
    target->visible = pose.visible;
    target->shadowEnabled = pose.shadowEnabled;
    target->opacity = pose.opacity;
    target->scale = pose.scale;
    target->parentModel = pose.parentModel;
    target->parentBone = pose.parentBone;
    std::copy(pose.position.begin(), pose.position.end(), target->position);
    std::copy(pose.rotation.begin(), pose.rotation.end(), target->rotation);
}
}

void SeekPortableSceneModel(mdl::ModelRecord* model, int frame) {
    auto* app = g_Block;
    format::SceneSession* session = nullptr;
    std::size_t index = 0;
    if (frame < 0 || !Active(app, model, session, index) || !SyncAllLive(app, *session) ||
        !SyncLiveLight(app, *session) || !session->Seek(static_cast<std::uint32_t>(frame)))
        return;
    // SeekModelFrame is called separately for each slot. Applying every
    // evaluated pose here changes models whose legacy seek/transform pass has
    // not run yet, leaving their local bones ahead of their skinning matrices.
    // Playback uses ApplyAllPoses after all slots have advanced.
    const auto& poses = session->Pose().models;
    if (index >= poses.size() || session->Document().models[index].slot != poses[index].slot)
        return;
    const bool applySelectors =
        session->Document().envelope.header.version == format::PmmVersion::V2;
    ApplyCpuPmmPose(model, poses[index], applySelectors);
}

void StartPortableScenePlayback(MMDApp* app) {
    auto* session = app ? app->PortableSceneSession() : nullptr;
    // The old Windows light track can stay parked while model playback
    // advances. Accessory ambient adjustment aliases LightColor on x86 and
    // its subtract/add round trip can drift from the applied specular RGB.
    // Publish the canonical CPU light, not that adjusted ambient mirror.
    // ApplyTimelineLightState publishes again when the light gate actually runs.
    if (session)
        session->SetRuntimeAppliedLight({app->SceneLight().Specular.r, app->SceneLight().Specular.g,
                                         app->SceneLight().Specular.b, app->LightDirection()[0],
                                         app->LightDirection()[1], app->LightDirection()[2]});
    if (session) {
        format::PmmEvaluatedCamera applied;
        std::copy_n(app->CameraPosition(), 3, applied.position.begin());
        std::copy_n(app->CameraRotation(), 3, applied.rotation.begin());
        applied.distance = app->CameraDistance();
        applied.fov = app->CameraFov();
        applied.perspective = app->CameraPerspective();
        applied.parentModel = app->CameraParentModel();
        applied.parentBone = app->CameraParentBone();
        session->SetRuntimeAppliedCamera(applied);
    }
    if (!session || !SyncAllLive(app, *session) || !SyncLiveCamera(app, *session) ||
        !SyncLiveLight(app, *session) || !SyncAllAccessories(app, *session) ||
        !SyncLiveSelfShadow(app, *session) || !SyncLiveGravity(app, *session) ||
        !session->StartPlayback())
        return;
    if (app->PlaybackCursorSeconds() > 0 && !session->Advance(app->PlaybackCursorSeconds()))
        return;
    ApplyAllPoses(app, *session);
}

void SeekPortableSceneCamera(MMDApp* app, int frame) {
    auto* session = app ? app->PortableSceneSession() : nullptr;
    if (!session || frame < 0 || !SyncLiveCamera(app, *session) || !SyncLiveLight(app, *session) ||
        !session->Seek(static_cast<std::uint32_t>(frame)))
        return;
    ApplyPortableSceneCamera(app);
}

void ApplyPortableSceneCamera(MMDApp* app) {
    auto* session = app ? app->PortableSceneSession() : nullptr;
    if (!session)
        return;
    const auto& pose = session->Pose().camera;
    std::copy(pose.position.begin(), pose.position.end(), app->CameraPosition());
    std::copy(pose.rotation.begin(), pose.rotation.end(), app->CameraRotation());
    app->CameraDistance() = pose.distance;
    app->CameraFov() = pose.fov;
    app->CameraPerspective() = pose.perspective;
    app->CameraParentModel() = pose.parentModel;
    app->CameraParentBone() = pose.parentBone;
    session->SetRuntimeAppliedCamera(pose);
}

void SeekPortableSceneAccessory(MMDApp* app, int slot, int frame) {
    auto* session = app ? app->PortableSceneSession() : nullptr;
    if (!session || slot < 0 || slot >= 255 || frame < 0)
        return;
    const auto& sources = session->Document().accessories;
    const auto found = std::find_if(sources.begin(), sources.end(),
                                    [slot](const auto& item) { return item.slot == slot; });
    if (found == sources.end() || !SyncLiveAccessory(app, *session, *found) ||
        !session->Seek(static_cast<std::uint32_t>(frame)))
        return;
    const auto& poses = session->Pose().accessories;
    const auto pose = std::find_if(poses.begin(), poses.end(),
                                   [slot](const auto& item) { return item.slot == slot; });
    if (pose != poses.end())
        ApplyAccessoryPose(app, *pose);
}

void ApplyPortableSceneAccessories(MMDApp* app) {
    auto* session = app ? app->PortableSceneSession() : nullptr;
    if (!session || !session->Playing())
        return;
    for (const auto& pose : session->Pose().accessories)
        ApplyAccessoryPose(app, pose);
}

void SeekPortableSceneSelfShadow(MMDApp* app, int frame) {
    auto* session = app ? app->PortableSceneSession() : nullptr;
    if (!session || frame < 0 || session->Document().selfShadowKeys.dense.empty() ||
        !SyncLiveSelfShadow(app, *session) || !session->Seek(static_cast<std::uint32_t>(frame)))
        return;
    ApplyPortableSceneSelfShadow(app);
}

void ApplyPortableSceneSelfShadow(MMDApp* app) {
    auto* session = app ? app->PortableSceneSession() : nullptr;
    if (!session || session->Document().selfShadowKeys.dense.empty())
        return;
    app->ShadowMode() = static_cast<std::int8_t>(session->Pose().selfShadowMode);
    app->ShadowDistance() = session->Pose().selfShadowDistance;
}

void SeekPortableSceneGravity(MMDApp* app, int frame) {
    auto* session = app ? app->PortableSceneSession() : nullptr;
    if (!session || frame < 0 || session->Document().gravityKeys.dense.empty() ||
        !SyncLiveGravity(app, *session) || !session->Seek(static_cast<std::uint32_t>(frame)))
        return;
    ApplyPortableSceneGravity(app);
}

void ApplyPortableSceneGravity(MMDApp* app) {
    auto* session = app ? app->PortableSceneSession() : nullptr;
    if (!session || session->Document().gravityKeys.dense.empty())
        return;
    const auto& gravity = session->Pose().gravity;
    app->GravityNoiseEnabled() = gravity.noiseEnabled;
    app->GravityNoise() = gravity.noise;
    app->GravityMagnitude() = gravity.acceleration;
    std::copy(gravity.direction.begin(), gravity.direction.end(), app->GravityDirection());
}

void AdvancePortableScenePlayback(MMDApp* app) {
    auto* session = app ? app->PortableSceneSession() : nullptr;
    if (!session || !session->Playing() || !AllModelsAvailable(app, *session))
        return;
    const float seconds = app->PlaybackCursorSeconds();
    if (static_cast<double>(seconds) * 30.0 + 0.001 < session->Playback().frame &&
        !session->StartPlayback())
        return;
    if (session->Advance(seconds))
        ApplyAllPoses(app, *session);
}
}
