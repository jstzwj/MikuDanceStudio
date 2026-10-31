#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>

namespace mikudancestudio::format {
namespace {
template <class Key> struct TrackLookup {
    std::unordered_map<std::uint32_t, const Key*> keys;
    bool valid = true;
    explicit TrackLookup(const PmmKeyTrack<Key>& track) {
        keys.reserve(track.dense.size() + track.sparse.size());
        for (std::size_t i = 0; i < track.dense.size(); ++i)
            keys.emplace(static_cast<std::uint32_t>(i), &track.dense[i]);
        for (const auto& sparse : track.sparse)
            if (sparse.index < 0 ||
                !keys.emplace(static_cast<std::uint32_t>(sparse.index), &sparse.value).second)
                valid = false;
    }
    const Key* Get(std::uint32_t index) const {
        const auto found = keys.find(index);
        return found == keys.end() ? nullptr : found->second;
    }
    bool BracketAt(std::uint32_t head, double frame, const Key*& left, const Key*& right) const {
        if (!valid)
            return false;
        std::uint32_t index = head;
        const Key* key = Get(index);
        if (!key)
            return false;
        if (frame <= static_cast<double>(key->frame)) {
            left = right = key;
            return true;
        }
        for (std::size_t steps = 0; steps < keys.size(); ++steps) {
            if (key->next == 0) {
                left = right = key;
                return true;
            }
            const Key* next = Get(key->next);
            if (!next || next->previous != index || next->frame <= key->frame)
                return false;
            if (frame <= static_cast<double>(next->frame)) {
                left = frame == static_cast<double>(next->frame) ? next : key;
                right = next;
                return true;
            }
            index = key->next;
            key = next;
        }
        return false;
    }
};

template <class Key>
bool AdvanceCursor(const TrackLookup<Key>& track, double frame, PmmTrackCursor& cursor,
                   const Key*& key, bool& exhausted) {
    exhausted = false;
    if (!track.valid || !cursor.active)
        return false;
    key = track.Get(cursor.index);
    if (!key)
        return false;
    for (std::size_t steps = 0; static_cast<double>(key->frame) < frame; ++steps) {
        if (steps >= track.keys.size())
            return false;
        if (key->next == 0) {
            cursor.active = false;
            exhausted = true;
            return true;
        }
        const Key* next = track.Get(key->next);
        if (!next || next->previous != cursor.index || next->frame <= key->frame)
            return false;
        cursor.index = key->next;
        key = next;
    }
    return true;
}

PmmEvaluatedBone CopyBone(const PmmBoneKey& key) {
    return {key.position, key.rotation, key.physicsDisabled};
}

bool AdvanceDisplay(const PmmSceneModel& source, TrackLookup<PmmDisplayKey>& track, double frame,
                    PmmTrackCursor& cursor, PmmEvaluatedModel& pose) {
    if (!track.valid)
        return false;
    if (!cursor.active)
        return true;
    const auto readKey = [](void* context, std::uint32_t index, PmmDisplayKeyLink& output) -> bool {
        const auto* key = static_cast<const TrackLookup<PmmDisplayKey>*>(context)->Get(index);
        if (!key)
            return false;
        output = {key->frame, key->previous, key->next, key->visible};
        return true;
    };
    const auto readPair = [](void* context, std::uint32_t keyIndex, std::uint32_t selectorIndex,
                             PmmRigidState& output) -> bool {
        const auto* key = static_cast<const TrackLookup<PmmDisplayKey>*>(context)->Get(keyIndex);
        if (!key || selectorIndex >= key->rigidStates.size())
            return false;
        output = key->rigidStates[selectorIndex];
        return true;
    };
    PmmDisplayCursor display{cursor.index, cursor.active};
    PmmDisplaySelection selected;
    if (!SelectPmmDisplayKey(readKey, &track, frame, PmmDisplayTraversal::AdvancePlayback, display,
                             selected))
        return false;
    cursor = {display.index, display.active};
    if (selected.kind == PmmDisplaySelectionKind::None)
        return true;
    const auto* held = track.Get(selected.heldIndex);
    if (!held || held->ikEnabled.size() != source.layout.ikCount)
        return false;
    pose.visible = selected.visible;
    pose.ikEnabled = held->ikEnabled;
    pose.selectors.clear();
    pose.selectors.reserve(source.rigidCount);
    for (std::uint32_t i = 0; i < source.rigidCount; ++i) {
        PmmEvaluatedSelector selector;
        if (!EvaluatePmmSelectorWindow(readKey, readPair, &track, selected, i, selector))
            return false;
        pose.selectors.push_back(selector);
    }
    return true;
}

bool AdvanceModel(const PmmSceneModel& source, double frame, PmmModelPlaybackCursors& cursors,
                  PmmEvaluatedModel& pose) {
    if (source.slot != pose.slot || source.layout.boneCount != pose.bones.size() ||
        source.layout.morphCount != pose.morphValues.size() ||
        source.layout.ikCount != pose.ikEnabled.size() ||
        source.rigidCount != pose.selectors.size() ||
        source.rigidSources.size() != source.rigidCount ||
        cursors.bones.size() != pose.bones.size() ||
        cursors.morphs.size() != pose.morphValues.size())
        return false;
    TrackLookup<PmmDisplayKey> display(source.displayKeys);
    if (!AdvanceDisplay(source, display, frame, cursors.display, pose))
        return false;
    std::vector<int> selectorForBone(source.layout.boneCount, -1);
    for (std::size_t i = 0; i < source.rigidSources.size(); ++i) {
        const int bone = source.rigidSources[i];
        if (bone < 0)
            continue;
        if (static_cast<std::size_t>(bone) >= selectorForBone.size() || selectorForBone[bone] >= 0)
            return false;
        selectorForBone[bone] = static_cast<int>(i);
    }
    TrackLookup<PmmMorphKey> morphs(source.morphKeys);
    if (!morphs.valid)
        return false;
    for (std::size_t i = 0; i < cursors.morphs.size(); ++i) {
        auto& cursor = cursors.morphs[i];
        if (!cursor.active)
            continue;
        const PmmMorphKey* key = nullptr;
        bool exhausted = false;
        if (!AdvanceCursor(morphs, frame, cursor, key, exhausted))
            return false;
        if (exhausted || frame == static_cast<double>(key->frame)) {
            pose.morphValues[i] = key->value;
            continue;
        }
        const auto* previous = morphs.Get(key->previous);
        if (!previous || previous->next != cursor.index || previous->frame >= key->frame)
            return false;
        pose.morphValues[i] = InterpolatePmmMorphValue(previous->value, key->value, previous->frame,
                                                       key->frame, frame);
    }
    TrackLookup<PmmBoneKey> bones(source.boneKeys);
    if (!bones.valid)
        return false;
    for (std::size_t i = 0; i < cursors.bones.size(); ++i) {
        auto& cursor = cursors.bones[i];
        if (!cursor.active)
            continue;
        const PmmBoneKey* key = nullptr;
        bool exhausted = false;
        if (!AdvanceCursor(bones, frame, cursor, key, exhausted))
            return false;
        auto& current = pose.bones[i];
        const int selectorIndex = selectorForBone[i];
        if (exhausted) {
            current = CopyBone(*key);
            if (selectorIndex >= 0) {
                const auto decision =
                    SelectPmmBoneTerminalPose(key->frame, frame, pose.selectors[selectorIndex]);
                ApplyPmmBoneSelectorPose(decision, *key, *key, true, current);
            }
            continue;
        }
        if (frame == static_cast<double>(key->frame)) {
            current = CopyBone(*key);
            continue;
        }
        const auto* previous = bones.Get(key->previous);
        if (!previous || previous->next != cursor.index || previous->frame >= key->frame)
            return false;
        if (selectorIndex >= 0) {
            const auto decision = SelectPmmBoneSegmentPose(previous->frame, key->frame, frame,
                                                           pose.selectors[selectorIndex]);
            if (ApplyPmmBoneSelectorPose(decision, *previous, *key, true, current))
                continue;
        }
        if (!InterpolatePmmBonePlaybackSegment(*previous, *key, frame, current))
            return false;
    }
    return true;
}
}

bool StartPmmScenePlayback(const PmmSceneDocument& scene, PmmScenePlaybackState& state) {
    PmmScenePlaybackState initial;
    if (!EvaluatePmmSceneFrame(scene, 0, initial.pose))
        return false;
    initial.models.reserve(scene.models.size());
    for (const auto& source : scene.models) {
        if (source.boneKeys.dense.size() != source.layout.boneCount ||
            source.morphKeys.dense.size() != source.layout.morphCount ||
            source.displayKeys.dense.size() != 1)
            return false;
        PmmModelPlaybackCursors cursors;
        cursors.bones.reserve(source.layout.boneCount);
        cursors.morphs.reserve(source.layout.morphCount);
        for (std::uint32_t i = 0; i < source.layout.boneCount; ++i)
            cursors.bones.push_back({i, true});
        for (std::uint32_t i = 0; i < source.layout.morphCount; ++i)
            cursors.morphs.push_back({i, true});
        initial.models.push_back(std::move(cursors));
    }
    initial.accessories.resize(scene.accessories.size());
    initial.initialized = true;
    state = std::move(initial);
    return true;
}

bool PmmPlaybackFrame(float cursorSeconds, double& output) {
    if (!std::isfinite(cursorSeconds) || cursorSeconds < 0.0f)
        return false;
    double frame = static_cast<double>(cursorSeconds) * 30.0;
    const double scaled = frame * 1000.0;
    if (scaled >= static_cast<double>(std::numeric_limits<int>::max()))
        return false;
    if (scaled - static_cast<double>(static_cast<int>(scaled)) >= 0.5)
        frame = static_cast<double>(static_cast<int>(scaled) + 1) / 1000.0;
    output = frame;
    return true;
}

bool AdvancePmmScenePlayback(const PmmSceneDocument& scene, float cursorSeconds,
                             PmmScenePlaybackState& state) {
    if (!state.initialized || !std::isfinite(cursorSeconds) || cursorSeconds < 0.0f ||
        state.models.size() != scene.models.size() ||
        state.pose.models.size() != scene.models.size() ||
        state.accessories.size() != scene.accessories.size() ||
        state.pose.accessories.size() != scene.accessories.size())
        return false;
    double frame;
    if (!PmmPlaybackFrame(cursorSeconds, frame))
        return false;
    if (frame < state.frame)
        return false;
    PmmScenePlaybackState next = state;
    TrackLookup<PmmLightKey> lights(scene.lightKeys);
    if (!EvaluatePmmCameraTrack(scene.cameraKeys, frame, PmmCameraInterpolation::Playback,
                                next.pose.camera))
        return false;
    const PmmLightKey *left = nullptr, *right = nullptr;
    if (!lights.BracketAt(0, frame, left, right))
        return false;
    if (left == right) {
        for (int i = 0; i < 3; ++i) {
            next.pose.lightState[i] = right->color[i];
            next.pose.lightState[i + 3] = right->direction[i];
        }
    } else if (!InterpolatePmmLightSegment(*left, *right, frame, PmmLightInterpolation::Playback,
                                           next.pose.lightState))
        return false;
    if (!scene.selfShadowKeys.dense.empty() &&
        !EvaluatePmmSelfShadowTrack(scene.selfShadowKeys, frame, next.pose.selfShadowMode,
                                    next.pose.selfShadowDistance))
        return false;
    if (!scene.gravityKeys.dense.empty() &&
        !EvaluatePmmGravityTrack(scene.gravityKeys, frame, next.pose.gravity))
        return false;
    for (std::size_t i = 0; i < scene.models.size(); ++i)
        if (!AdvanceModel(scene.models[i], frame, next.models[i], next.pose.models[i]))
            return false;
    for (std::size_t i = 0; i < scene.accessories.size(); ++i) {
        auto& cursor = next.accessories[i];
        if (!cursor.active)
            continue;
        TrackLookup<PmmAccessoryKey> track(scene.accessories[i].keyRecords);
        const PmmAccessoryKey* key = nullptr;
        bool exhausted = false;
        if (!AdvanceCursor(track, frame, cursor, key, exhausted) ||
            !EvaluatePmmAccessoryTrack(scene.accessories[i].keyRecords, scene.accessories[i].slot,
                                       frame, next.pose.accessories[i]))
            return false;
    }
    next.frame = frame;
    next.pose.frame = static_cast<std::uint32_t>(frame);
    state = std::move(next);
    return true;
}
}
