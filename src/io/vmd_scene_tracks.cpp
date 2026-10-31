#include "mikudancestudio/vmd_scene_tracks.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
namespace mikudancestudio::format {
namespace {
template <class T> bool Finite(const T& values) {
    for (float value : values)
        if (!std::isfinite(value))
            return false;
    return true;
}
template <class Key, class Input>
bool Merge(PmmKeyTrack<Key> existing, const std::vector<Input>& input, PmmKeyTrack<Key>& output) {
    if (existing.dense.size() != 1)
        return false;
    std::map<std::uint32_t, Key> keys;
    keys.emplace(existing.dense[0].frame, existing.dense[0]);
    for (const auto& sparse : existing.sparse)
        keys[sparse.value.frame] = sparse.value;
    for (const auto& source : input) {
        Key key;
        if (!DecodeVmdSceneKey(source, key))
            return false;
        keys[key.frame] = key;
    }
    if (keys.empty() || keys.begin()->first != 0 || keys.size() > 1000)
        return false;
    PmmKeyTrack<Key> result;
    std::size_t index = 0;
    for (auto& entry : keys) {
        auto key = entry.second;
        key.previous = index ? std::uint32_t(index - 1) : 0;
        key.next = index + 1 < keys.size() ? std::uint32_t(index + 1) : 0;
        if (!index)
            result.dense.push_back(key);
        else
            result.sparse.push_back({int(index), 0, key});
        ++index;
    }
    output = std::move(result);
    return true;
}
}
bool DecodeVmdSceneKey(const VmdCameraKey& source, PmmCameraKey& output) {
    if (source.frame > unsigned(std::numeric_limits<int>::max()) ||
        source.fov > unsigned(std::numeric_limits<int>::max()) || !std::isfinite(source.distance) ||
        !Finite(source.eye) || !Finite(source.target))
        return false;
    PmmCameraKey key;
    key.frame = source.frame;
    key.distance = source.distance;
    key.parentModel = -1;
    std::copy_n(source.eye, 3, key.eye.begin());
    std::copy_n(source.target, 3, key.target.begin());
    for (int channel = 0; channel < 6; ++channel)
        for (int component = 0; component < 4; ++component) {
            const auto value = source.interpolation[channel][component];
            if (value > 127)
                return false;
            const int target = component == 1 ? 2 : component == 2 ? 1 : component;
            key.interpolation[target][channel] = value;
        }
    key.fov = int(source.fov);
    key.perspective = source.perspective ? 1 : 0;
    output = key;
    return true;
}
bool DecodeVmdSceneKey(const VmdLightKey& source, PmmLightKey& output) {
    if (source.frame > unsigned(std::numeric_limits<int>::max()) || !Finite(source.color) ||
        !Finite(source.direction))
        return false;
    PmmLightKey key;
    key.frame = source.frame;
    std::copy_n(source.color, 3, key.color.begin());
    std::copy_n(source.direction, 3, key.direction.begin());
    output = key;
    return true;
}
bool DecodeVmdSceneKey(const VmdShadowKey& source, PmmSelfShadowKey& output) {
    if (source.frame > unsigned(std::numeric_limits<int>::max()) || source.mode > 2 ||
        !std::isfinite(source.distance))
        return false;
    PmmSelfShadowKey key;
    key.frame = source.frame;
    key.mode = source.mode;
    key.distance = source.distance;
    output = key;
    return true;
}
bool ApplyVmdSceneTracks(SceneSession& target, const VmdDocument& motion, std::string& reason) {
    if (!target.Loaded() || !motion.hasCameraSection) {
        reason = "scene-global-motion-unavailable";
        return false;
    }
    if (!motion.bones.empty() || !motion.morphs.empty() || !motion.ik.empty()) {
        reason = "model-tracks-require-model-binding";
        return false;
    }
    auto candidate = target;
    PmmKeyTrack<PmmCameraKey> camera;
    PmmKeyTrack<PmmLightKey> light;
    PmmKeyTrack<PmmSelfShadowKey> shadow;
    if ((!motion.cameras.empty() &&
         (!Merge(candidate.Document().cameraKeys, motion.cameras, camera) ||
          !candidate.ReplaceRuntimeCameraTrack(std::move(camera)))) ||
        (!motion.lights.empty() && (!Merge(candidate.Document().lightKeys, motion.lights, light) ||
                                    !candidate.ReplaceRuntimeLightTrack(std::move(light)))) ||
        (!motion.shadows.empty() &&
         (!Merge(candidate.Document().selfShadowKeys, motion.shadows, shadow) ||
          !candidate.ReplaceRuntimeSelfShadowTrack(std::move(shadow))))) {
        reason = "invalid-scene-global-motion";
        return false;
    }
    if (!candidate.Seek(target.Pose().frame)) {
        reason = "scene-global-motion-seek-failed";
        return false;
    }
    target = std::move(candidate);
    reason.clear();
    return true;
}
}
