#include "mikudancestudio/pmm_scene_document.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mikudancestudio::format {
namespace {
struct Editor {
    PmmDocument& document;
    bool valid = true;

    bool bytes(std::size_t offset, const void* value, std::size_t length) {
        const auto prefix = document.prefixBytes.size();
        const auto total = prefix + document.remainingBytes.size();
        if (offset > total || length > total - offset)
            return valid = false;
        const auto* source = static_cast<const std::uint8_t*>(value);
        const auto first = offset < prefix ? std::min(length, prefix - offset) : 0;
        if (first)
            std::memcpy(document.prefixBytes.data() + offset, source, first);
        if (first < length)
            std::memcpy(document.remainingBytes.data() + offset + first - prefix, source + first,
                        length - first);
        return true;
    }
    template <class T> void changed(std::size_t offset, const T& before, const T& after) {
        if (std::memcmp(&before, &after, sizeof(T)) != 0)
            bytes(offset, &after, sizeof(T));
    }
};

struct Input {
    const PmmDocument& document;
    std::size_t offset = 0;
};
int Read(void* context, void* destination, std::size_t length) {
    auto& input = *static_cast<Input*>(context);
    const auto prefix = input.document.prefixBytes.size();
    const auto total = prefix + input.document.remainingBytes.size();
    const auto count = std::min(length, total - input.offset);
    auto* out = static_cast<std::uint8_t*>(destination);
    const auto first = input.offset < prefix ? std::min(count, prefix - input.offset) : 0;
    if (first)
        std::memcpy(out, input.document.prefixBytes.data() + input.offset, first);
    if (first < count)
        std::memcpy(out + first,
                    input.document.remainingBytes.data() + input.offset + first - prefix,
                    count - first);
    input.offset += count;
    return static_cast<int>(count);
}
template <class T> bool Same(const T& a, const T& b) {
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}
bool SameBoneKey(const PmmBoneKey& a, const PmmBoneKey& b) {
    return a.frame == b.frame && a.previous == b.previous && a.next == b.next &&
           a.interpolation == b.interpolation && a.position == b.position &&
           a.rotation == b.rotation && a.allocated == b.allocated &&
           a.physicsDisabled == b.physicsDisabled;
}
bool SameMorphKey(const PmmMorphKey& a, const PmmMorphKey& b) {
    return a.frame == b.frame && a.previous == b.previous && a.next == b.next &&
           a.value == b.value && a.allocated == b.allocated;
}
bool SameLightKey(const PmmLightKey& a, const PmmLightKey& b) {
    return a.frame == b.frame && a.previous == b.previous && a.next == b.next &&
           a.color == b.color && a.direction == b.direction && a.selected == b.selected;
}
bool SameCameraKey(const PmmCameraKey& a, const PmmCameraKey& b) {
    return a.frame == b.frame && a.previous == b.previous && a.next == b.next &&
           Same(a.distance, b.distance) && Same(a.eye, b.eye) && Same(a.target, b.target) &&
           a.parentModel == b.parentModel && a.parentBone == b.parentBone &&
           a.interpolation == b.interpolation && a.perspective == b.perspective && a.fov == b.fov &&
           a.selected == b.selected;
}
bool SameSelfShadowKey(const PmmSelfShadowKey& a, const PmmSelfShadowKey& b) {
    return a.frame == b.frame && a.previous == b.previous && a.next == b.next && a.mode == b.mode &&
           a.distance == b.distance && a.selected == b.selected;
}
bool SameGravityKey(const PmmGravityKey& a, const PmmGravityKey& b) {
    return a.frame == b.frame && a.previous == b.previous && a.next == b.next &&
           a.noiseEnabled == b.noiseEnabled && a.noise == b.noise &&
           a.acceleration == b.acceleration && a.direction == b.direction &&
           a.selected == b.selected;
}
bool SameAccessoryKey(const PmmAccessoryKey& a, const PmmAccessoryKey& b) {
    return a.frame == b.frame && a.previous == b.previous && a.next == b.next &&
           a.visible == b.visible && a.shadowEnabled == b.shadowEnabled &&
           a.selected == b.selected && a.opacity == b.opacity && a.parentModel == b.parentModel &&
           a.parentBone == b.parentBone && a.position == b.position && a.rotation == b.rotation &&
           a.scale == b.scale;
}
bool SameAccessoryState(const PmmAccessoryState& a, const PmmAccessoryState& b) {
    return a.visible == b.visible && a.shadowEnabled == b.shadowEnabled &&
           a.additiveBlend == b.additiveBlend && std::abs(a.opacity - b.opacity) <= 0.00501f &&
           a.scale == b.scale && a.parentModel == b.parentModel && a.parentBone == b.parentBone &&
           a.position == b.position && a.rotation == b.rotation;
}
bool SameDisplayKey(const PmmDisplayKey& a, const PmmDisplayKey& b) {
    if (a.frame != b.frame || a.previous != b.previous || a.next != b.next ||
        a.visible != b.visible || a.allocated != b.allocated || a.ikEnabled != b.ikEnabled ||
        a.rigidStates.size() != b.rigidStates.size())
        return false;
    for (std::size_t i = 0; i < a.rigidStates.size(); ++i)
        if (a.rigidStates[i].modelIndex != b.rigidStates[i].modelIndex ||
            a.rigidStates[i].boneIndex != b.rigidStates[i].boneIndex)
            return false;
    return true;
}
template <class T, class Equal>
bool SameTrack(const PmmKeyTrack<T>& a, const PmmKeyTrack<T>& b, Equal equal) {
    if (a.dense.size() != b.dense.size() || a.sparse.size() != b.sparse.size())
        return false;
    for (std::size_t i = 0; i < a.dense.size(); ++i)
        if (!equal(a.dense[i], b.dense[i]))
            return false;
    for (std::size_t i = 0; i < a.sparse.size(); ++i)
        if (a.sparse[i].index != b.sparse[i].index || !equal(a.sparse[i].value, b.sparse[i].value))
            return false;
    return true;
}
template <class T, class Patch>
bool PatchTrack(Editor& editor, const PmmKeyTrack<T>& before, const PmmKeyTrack<T>& after,
                Patch patch) {
    if (before.dense.size() != after.dense.size() || before.sparse.size() != after.sparse.size() ||
        before.denseRecordOffsets.size() != before.dense.size())
        return false;
    for (std::size_t i = 0; i < before.dense.size(); ++i)
        patch(editor, before.denseRecordOffsets[i], before.dense[i], after.dense[i]);
    for (std::size_t i = 0; i < before.sparse.size(); ++i) {
        if (before.sparse[i].index != after.sparse[i].index)
            return false;
        patch(editor, before.sparse[i].recordOffset, before.sparse[i].value, after.sparse[i].value);
    }
    return editor.valid;
}
void PatchCameraKey(Editor& editor, std::size_t offset, PmmVersion version,
                    const PmmCameraKey& before, const PmmCameraKey& after) {
    editor.changed(offset + 12, before.distance, after.distance);
    editor.changed(offset + 16, before.eye, after.eye);
    editor.changed(offset + 28, before.target, after.target);
    std::size_t interpolation = offset + 40;
    if (version == PmmVersion::V2) {
        editor.changed(offset + 40, before.parentModel, after.parentModel);
        editor.changed(offset + 44, before.parentBone, after.parentBone);
        interpolation += 8;
    }
    for (std::size_t column = 0; column < 6; ++column)
        for (std::size_t row = 0; row < 4; ++row)
            editor.changed(interpolation + column * 4 + row, before.interpolation[row][column],
                           after.interpolation[row][column]);
    editor.changed(interpolation + 24, before.perspective, after.perspective);
    editor.changed(interpolation + 25, before.fov, after.fov);
    editor.changed(interpolation + 29, before.selected, after.selected);
}
void PatchBoneKey(Editor& editor, std::size_t offset, PmmVersion version, const PmmBoneKey& before,
                  const PmmBoneKey& after) {
    for (int lane = 0; lane < 4; ++lane)
        for (int group = 0; group < 4; ++group)
            editor.changed(offset + 12 + lane * 4 + group, before.interpolation[lane + group * 4],
                           after.interpolation[lane + group * 4]);
    editor.changed(offset + 28, before.position, after.position);
    editor.changed(offset + 40, before.rotation, after.rotation);
    editor.changed(offset + 56, before.allocated, after.allocated);
    if (version == PmmVersion::V2)
        editor.changed(offset + 57, before.physicsDisabled, after.physicsDisabled);
    else if (before.physicsDisabled != after.physicsDisabled)
        editor.valid = false;
}
void PatchMorphKey(Editor& editor, std::size_t offset, const PmmMorphKey& before,
                   const PmmMorphKey& after) {
    editor.changed(offset + 12, before.value, after.value);
    editor.changed(offset + 16, before.allocated, after.allocated);
}
void PatchLightKey(Editor& editor, std::size_t offset, const PmmLightKey& before,
                   const PmmLightKey& after) {
    editor.changed(offset + 12, before.color, after.color);
    editor.changed(offset + 24, before.direction, after.direction);
    editor.changed(offset + 36, before.selected, after.selected);
}
void PatchSelfShadowKey(Editor& editor, std::size_t offset, const PmmSelfShadowKey& before,
                        const PmmSelfShadowKey& after) {
    editor.changed(offset + 12, before.mode, after.mode);
    editor.changed(offset + 13, before.distance, after.distance);
    editor.changed(offset + 17, before.selected, after.selected);
}
void PatchGravityKey(Editor& editor, std::size_t offset, const PmmGravityKey& before,
                     const PmmGravityKey& after) {
    editor.changed(offset + 12, before.noiseEnabled, after.noiseEnabled);
    editor.changed(offset + 13, before.noise, after.noise);
    editor.changed(offset + 17, before.acceleration, after.acceleration);
    editor.changed(offset + 21, before.direction, after.direction);
    editor.changed(offset + 33, before.selected, after.selected);
}
bool PackedAccessoryFlag(std::uint8_t visible, float opacity, std::uint8_t& packed,
                         bool exact = true) {
    if (visible > 1 || !std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f)
        return false;
    const int percent = static_cast<int>(std::lround(opacity * 100.0f));
    if (exact && std::abs(opacity - static_cast<float>(percent) / 100.0f) > 1e-5f)
        return false;
    packed = static_cast<std::uint8_t>(((100 - percent) << 1) | visible);
    return true;
}
void PatchAccessoryKey(Editor& editor, std::size_t offset, const PmmAccessoryKey& before,
                       const PmmAccessoryKey& after) {
    std::uint8_t oldFlag = 0, newFlag = 0;
    if (!PackedAccessoryFlag(before.visible, before.opacity, oldFlag) ||
        !PackedAccessoryFlag(after.visible, after.opacity, newFlag) || after.shadowEnabled > 1 ||
        after.selected > 1) {
        editor.valid = false;
        return;
    }
    editor.changed(offset + 12, oldFlag, newFlag);
    editor.changed(offset + 13, before.parentModel, after.parentModel);
    editor.changed(offset + 17, before.parentBone, after.parentBone);
    editor.changed(offset + 21, before.position, after.position);
    editor.changed(offset + 33, before.rotation, after.rotation);
    editor.changed(offset + 45, before.scale, after.scale);
    editor.changed(offset + 49, before.shadowEnabled, after.shadowEnabled);
    editor.changed(offset + 50, before.selected, after.selected);
}
void PatchAccessoryState(Editor& editor, std::size_t offset, PmmVersion version,
                         const PmmAccessoryState& before, const PmmAccessoryState& after) {
    std::uint8_t oldFlag = 0, newFlag = 0;
    if (!PackedAccessoryFlag(before.visible, before.opacity, oldFlag, false) ||
        !PackedAccessoryFlag(after.visible, after.opacity, newFlag, false) ||
        after.shadowEnabled > 1 || after.additiveBlend > 1) {
        editor.valid = false;
        return;
    }
    editor.changed(offset, oldFlag, newFlag);
    editor.changed(offset + 1, before.parentModel, after.parentModel);
    editor.changed(offset + 5, before.parentBone, after.parentBone);
    editor.changed(offset + 9, before.rotation, after.rotation);
    editor.changed(offset + 21, before.scale, after.scale);
    editor.changed(offset + 25, before.position, after.position);
    editor.changed(offset + 37, before.shadowEnabled, after.shadowEnabled);
    if (version == PmmVersion::V2)
        editor.changed(offset + 38, before.additiveBlend, after.additiveBlend);
}
void PatchDisplayKey(Editor& editor, std::size_t offset, const PmmDisplayKey& before,
                     const PmmDisplayKey& after) {
    if (before.ikEnabled.size() != after.ikEnabled.size() ||
        before.rigidStates.size() != after.rigidStates.size()) {
        editor.valid = false;
        return;
    }
    editor.changed(offset + 12, before.visible, after.visible);
    for (std::size_t i = 0; i < before.ikEnabled.size(); ++i)
        editor.changed(offset + 13 + i, before.ikEnabled[i], after.ikEnabled[i]);
    const auto rigid = offset + 13 + before.ikEnabled.size();
    for (std::size_t i = 0; i < before.rigidStates.size(); ++i) {
        editor.changed(rigid + 8 * i, before.rigidStates[i].modelIndex,
                       after.rigidStates[i].modelIndex);
        editor.changed(rigid + 8 * i + 4, before.rigidStates[i].boneIndex,
                       after.rigidStates[i].boneIndex);
    }
    editor.changed(rigid + 8 * before.rigidStates.size(), before.allocated, after.allocated);
}
bool ValidFlags(const PmmSceneSettings& settings, const PmmTimelineSettings& timeline) {
    for (auto flag : settings.options)
        if (flag > 1)
            return false;
    return timeline.playbackLoop <= 1 && timeline.returnToStart <= 1 &&
           timeline.startAtCurrent <= 1;
}
bool SameEdits(const PmmSceneDocument& expected, const PmmSceneDocument& actual) {
    const auto& a = expected.envelope.settings;
    const auto& b = actual.envelope.settings;
    if (a.renderWidth != b.renderWidth || a.renderHeight != b.renderHeight ||
        a.editPanelWidth != b.editPanelWidth || !Same(a.cameraFov, b.cameraFov) ||
        a.options != b.options ||
        !Same(expected.timeline.currentFrame, actual.timeline.currentFrame) ||
        !Same(expected.timeline.startFrame, actual.timeline.startFrame) ||
        !Same(expected.timeline.lastRegisteredFrame, actual.timeline.lastRegisteredFrame) ||
        !Same(expected.timeline.editMode, actual.timeline.editMode) ||
        !Same(expected.timeline.cameraReferenceMode, actual.timeline.cameraReferenceMode) ||
        !Same(expected.timeline.playbackLoop, actual.timeline.playbackLoop) ||
        !Same(expected.timeline.returnToStart, actual.timeline.returnToStart) ||
        !Same(expected.timeline.startAtCurrent, actual.timeline.startAtCurrent) ||
        !Same(expected.timeline.playStartFrame, actual.timeline.playStartFrame) ||
        !Same(expected.timeline.playStopFrame, actual.timeline.playStopFrame) ||
        !Same(expected.lightState, actual.lightState) ||
        expected.selfShadowMode != actual.selfShadowMode ||
        !Same(expected.physicsInterval, actual.physicsInterval) ||
        expected.physics.playbackMode != actual.physics.playbackMode ||
        expected.physics.gravityNoiseEnabled != actual.physics.gravityNoiseEnabled ||
        expected.physics.gravityNoise != actual.physics.gravityNoise ||
        !Same(expected.physics.gravityMagnitude, actual.physics.gravityMagnitude) ||
        !Same(expected.physics.gravityDirection, actual.physics.gravityDirection) ||
        expected.models.size() != actual.models.size() ||
        expected.accessories.size() != actual.accessories.size() ||
        !SameTrack(expected.cameraKeys, actual.cameraKeys, SameCameraKey) ||
        !SameTrack(expected.lightKeys, actual.lightKeys, SameLightKey) ||
        !SameTrack(expected.selfShadowKeys, actual.selfShadowKeys, SameSelfShadowKey) ||
        !SameTrack(expected.gravityKeys, actual.gravityKeys, SameGravityKey))
        return false;
    for (std::size_t i = 0; i < expected.models.size(); ++i) {
        const auto& left = expected.models[i];
        const auto& right = actual.models[i];
        if (left.slot != right.slot || left.bonePose.size() != right.bonePose.size() ||
            left.morphPose.size() != right.morphPose.size() || left.ikPose != right.ikPose ||
            !SameTrack(left.boneKeys, right.boneKeys, SameBoneKey) ||
            !SameTrack(left.morphKeys, right.morphKeys, SameMorphKey) ||
            !SameTrack(left.displayKeys, right.displayKeys, SameDisplayKey))
            return false;
        for (std::size_t j = 0; j < left.bonePose.size(); ++j)
            if (!Same(left.bonePose[j].components, right.bonePose[j].components) ||
                left.bonePose[j].flags != right.bonePose[j].flags)
                return false;
        for (std::size_t j = 0; j < left.morphPose.size(); ++j)
            if (!Same(left.morphPose[j], right.morphPose[j]))
                return false;
    }
    for (std::size_t i = 0; i < expected.accessories.size(); ++i) {
        const auto& left = expected.accessories[i];
        const auto& right = actual.accessories[i];
        if (left.slot != right.slot ||
            !SameTrack(left.keyRecords, right.keyRecords, SameAccessoryKey) ||
            !SameAccessoryState(left.state, right.state))
            return false;
    }
    return true;
}
}

bool WritePmmSceneDocument(WritePmmBytes write, void* context, const PmmSceneDocument& scene,
                           ResolvePmmV1ModelLayout resolveV1, void* resolverContext) {
    if (!write || (scene.envelope.header.version == PmmVersion::V1 && !resolveV1) ||
        !ValidFlags(scene.envelope.settings, scene.timeline))
        return false;
    Input originalInput{scene.envelope};
    PmmDocument originalEnvelope;
    if (!ReadPmmDocument(Read, &originalInput, originalEnvelope))
        return false;
    PmmSceneDocument original;
    if (!ParsePmmSceneDocument(originalEnvelope, resolveV1, resolverContext, original) ||
        scene.models.size() != original.models.size())
        return false;
    PmmDocument patched = scene.envelope;
    Editor editor{patched};
    const auto& oldSettings = original.envelope.settings;
    const auto& settings = scene.envelope.settings;
    editor.changed(30, oldSettings.renderWidth, settings.renderWidth);
    editor.changed(34, oldSettings.renderHeight, settings.renderHeight);
    editor.changed(38, oldSettings.editPanelWidth, settings.editPanelWidth);
    editor.changed(42, oldSettings.cameraFov, settings.cameraFov);
    const int optionCount = patched.header.version == PmmVersion::V1 ? 6 : 7;
    for (int i = 0; i < optionCount; ++i)
        editor.changed(46 + i, oldSettings.options[i], settings.options[i]);

    const auto& oldTimeline = original.timeline;
    const auto& timeline = scene.timeline;
    const auto t = original.timelineOffset;
    editor.changed(t + 0, oldTimeline.currentFrame, timeline.currentFrame);
    editor.changed(t + 4, oldTimeline.startFrame, timeline.startFrame);
    editor.changed(t + 8, oldTimeline.lastRegisteredFrame, timeline.lastRegisteredFrame);
    editor.changed(t + 12, oldTimeline.editMode, timeline.editMode);
    editor.changed(t + 16, oldTimeline.cameraReferenceMode, timeline.cameraReferenceMode);
    editor.changed(t + 17, oldTimeline.playbackLoop, timeline.playbackLoop);
    editor.changed(t + 18, oldTimeline.returnToStart, timeline.returnToStart);
    editor.changed(t + 19, oldTimeline.startAtCurrent, timeline.startAtCurrent);
    editor.changed(t + 20, oldTimeline.playStartFrame, timeline.playStartFrame);
    editor.changed(t + 24, oldTimeline.playStopFrame, timeline.playStopFrame);
    for (int i = 0; i < 6; ++i)
        editor.changed(original.lightStateOffset + 4 * i, original.lightState[i],
                       scene.lightState[i]);

    if (!PatchTrack(editor, original.cameraKeys, scene.cameraKeys,
                    [version = patched.header.version](Editor& target, std::size_t offset,
                                                       const PmmCameraKey& before,
                                                       const PmmCameraKey& after) {
                        PatchCameraKey(target, offset, version, before, after);
                    }))
        return false;
    if (patched.header.version == PmmVersion::V2 &&
        !PatchTrack(editor, original.lightKeys, scene.lightKeys, PatchLightKey))
        return false;
    if (original.physicsOffset != 0) {
        editor.changed(original.physicsOffset + (patched.header.version == PmmVersion::V2 ? 9 : 0),
                       original.physics.playbackMode, scene.physics.playbackMode);
        const std::size_t base =
            original.physicsOffset + (patched.header.version == PmmVersion::V2 ? 10 : 1);
        editor.changed(base, original.physics.gravityMagnitude, scene.physics.gravityMagnitude);
        editor.changed(base + 4, original.physics.gravityNoise, scene.physics.gravityNoise);
        editor.changed(base + 8, original.physics.gravityDirection, scene.physics.gravityDirection);
        editor.changed(base + 20, original.physics.gravityNoiseEnabled,
                       scene.physics.gravityNoiseEnabled);
    } else if (original.physics.playbackMode != scene.physics.playbackMode ||
               original.physics.gravityMagnitude != scene.physics.gravityMagnitude ||
               original.physics.gravityNoise != scene.physics.gravityNoise ||
               original.physics.gravityDirection != scene.physics.gravityDirection ||
               original.physics.gravityNoiseEnabled != scene.physics.gravityNoiseEnabled)
        return false;
    if (!PatchTrack(editor, original.gravityKeys, scene.gravityKeys, PatchGravityKey))
        return false;
    if (original.selfShadowStateOffset != 0) {
        editor.changed(original.selfShadowStateOffset, original.selfShadowMode,
                       scene.selfShadowMode);
        editor.changed(original.selfShadowStateOffset + 1, original.physicsInterval,
                       scene.physicsInterval);
    } else if (original.selfShadowMode != scene.selfShadowMode ||
               original.physicsInterval != scene.physicsInterval)
        return false;
    if (!PatchTrack(editor, original.selfShadowKeys, scene.selfShadowKeys, PatchSelfShadowKey))
        return false;

    const auto version = patched.header.version;
    for (std::size_t i = 0; i < scene.models.size(); ++i) {
        const auto& oldModel = original.models[i];
        const auto& model = scene.models[i];
        if (model.slot != oldModel.slot || model.bonePose.size() != oldModel.bonePose.size() ||
            model.morphPose.size() != oldModel.morphPose.size() ||
            model.ikPose.size() != oldModel.ikPose.size())
            return false;
        if (!PatchTrack(editor, oldModel.boneKeys, model.boneKeys,
                        [version](Editor& target, std::size_t offset, const PmmBoneKey& before,
                                  const PmmBoneKey& after) {
                            PatchBoneKey(target, offset, version, before, after);
                        }) ||
            !PatchTrack(editor, oldModel.morphKeys, model.morphKeys, PatchMorphKey) ||
            !PatchTrack(editor, oldModel.displayKeys, model.displayKeys, PatchDisplayKey))
            return false;
        const std::size_t componentCount = version == PmmVersion::V1 ? 8 : 7;
        const std::size_t flagCount = version == PmmVersion::V1 ? 2 : 3;
        const std::size_t stride = 4 * componentCount + flagCount;
        for (std::size_t j = 0; j < model.bonePose.size(); ++j) {
            const auto offset = oldModel.bonePoseOffset + stride * j;
            for (std::size_t k = 0; k < componentCount; ++k)
                editor.changed(offset + 4 * k, oldModel.bonePose[j].components[k],
                               model.bonePose[j].components[k]);
            for (std::size_t k = 0; k < flagCount; ++k)
                editor.changed(offset + 4 * componentCount + k, oldModel.bonePose[j].flags[k],
                               model.bonePose[j].flags[k]);
        }
        for (std::size_t j = 0; j < model.morphPose.size(); ++j)
            editor.changed(oldModel.morphPoseOffset + 4 * j, oldModel.morphPose[j],
                           model.morphPose[j]);
        for (std::size_t j = 0; j < model.ikPose.size(); ++j)
            editor.changed(oldModel.ikPoseOffset + j, oldModel.ikPose[j], model.ikPose[j]);
    }
    if (scene.accessories.size() != original.accessories.size())
        return false;
    for (std::size_t i = 0; i < scene.accessories.size(); ++i) {
        const auto& before = original.accessories[i];
        const auto& after = scene.accessories[i];
        if (before.slot != after.slot ||
            !PatchTrack(editor, before.keyRecords, after.keyRecords, PatchAccessoryKey))
            return false;
        PatchAccessoryState(editor, before.stateOffset, version, before.state, after.state);
    }
    if (!editor.valid)
        return false;
    Input input{patched};
    PmmDocument reread;
    PmmSceneDocument checked;
    if (!ReadPmmDocument(Read, &input, reread) ||
        !ParsePmmSceneDocument(reread, resolveV1, resolverContext, checked) ||
        !SameEdits(scene, checked))
        return false;
    return WritePmmDocument(write, context, reread);
}
}
