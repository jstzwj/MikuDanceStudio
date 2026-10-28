#include "mikudancestudio/runtime_cpu_tracks.hpp"
#include "mikudancestudio/model.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <map>

namespace mikudancestudio {
namespace {
template <class Key>
bool ValidTrack(const format::PmmKeyTrack<Key>& track, std::size_t roots, std::size_t capacity) {
    if (track.dense.size() != roots || roots > capacity)
        return false;
    std::map<std::uint32_t, const Key*> keys;
    for (std::uint32_t n = 0; n < roots; ++n)
        keys.emplace(n, &track.dense[n]);
    for (const auto& key : track.sparse)
        if (key.index < 0 || std::size_t(key.index) < roots || std::size_t(key.index) >= capacity ||
            !keys.emplace(std::uint32_t(key.index), &key.value).second)
            return false;
    std::set<std::uint32_t> visited;
    for (std::uint32_t root = 0; root < roots; ++root) {
        auto index = root;
        const Key* previous = nullptr;
        std::uint32_t previousIndex = 0;
        for (;;) {
            const auto found = keys.find(index);
            if (found == keys.end() || !visited.insert(index).second)
                return false;
            const auto& key = *found->second;
            if (previous && (key.previous != previousIndex || key.frame <= previous->frame))
                return false;
            if (!key.next)
                break;
            previous = &key;
            previousIndex = index;
            index = key.next;
        }
    }
    return visited.size() == keys.size();
}
template <class Key, class F> void Each(const format::PmmKeyTrack<Key>& track, F f) {
    for (std::size_t n = 0; n < track.dense.size(); ++n)
        f(n, track.dense[n]);
    for (const auto& key : track.sparse)
        f(std::size_t(key.index), key.value);
}
bool DisplayMaps(const mdl::ModelRecord& live, const format::PmmSceneModel& model,
                 format::PmmVersion version, std::vector<unsigned>& ik,
                 std::vector<unsigned>& selectors) {
    ik.clear();
    selectors.clear();
    if (version == format::PmmVersion::V1) {
        if (model.layout.ikCount != live.ikChainCount || model.rigidCount)
            return false;
        for (unsigned n = 0; n < live.ikChainCount; ++n)
            ik.push_back(n);
        return true;
    }
    if (model.ikSources.size() != model.layout.ikCount ||
        model.rigidSources.size() != model.rigidCount)
        return false;
    for (int bone : model.ikSources) {
        unsigned n = 0;
        for (; n < live.ikChainCount; ++n)
            if (live.ikChains[n].boneIndex == bone)
                break;
        if (n == live.ikChainCount || std::find(ik.begin(), ik.end(), n) != ik.end())
            return false;
        ik.push_back(n);
    }
    for (int bone : model.rigidSources) {
        unsigned n = 0;
        if (bone != -1) {
            n = 1;
            for (; n < live.boneOrderCount; ++n)
                if (live.boneOrderTable[n].boneIndex == bone)
                    break;
        }
        if (n >= live.boneOrderCount ||
            std::find(selectors.begin(), selectors.end(), n) != selectors.end())
            return false;
        selectors.push_back(n);
    }
    return true;
}
bool SameName(const char* name, const std::vector<char>& disk) {
    const auto end = std::find(disk.begin(), disk.end(), '\0');
    const std::size_t size = std::size_t(end - disk.begin());
    return size <= 20 && (!size || std::memcmp(name, disk.data(), size) == 0) &&
           (size == 20 || name[size] == 0);
}
}
bool ValidateCpuPmmTracks(mdl::ModelRecord* raw, const format::PmmSceneModel& model,
                          format::PmmVersion version, std::string& reason) {
    if (!raw) {
        reason = "missing_cpu_model";
        return false;
    }
    const auto& live = *raw;
    if (live.boneCount != model.layout.boneCount || live.morphCount != model.layout.morphCount) {
        reason = "cpu_pmm_layout_requires_remapping";
        return false;
    }
    if (version == format::PmmVersion::V2) {
        if (model.boneNames.size() != live.boneCount ||
            model.morphNames.size() != live.morphCount) {
            reason = "cpu_pmm_missing_names";
            return false;
        }
        for (unsigned n = 0; n < live.boneCount; ++n)
            if (!SameName(live.boneTable[n].name, model.boneNames[n])) {
                reason = "cpu_pmm_bone_name_requires_remapping";
                return false;
            }
        for (unsigned n = 0; n < live.morphCount; ++n)
            if (!SameName(live.morphs[n].name, model.morphNames[n])) {
                reason = "cpu_pmm_morph_name_requires_remapping";
                return false;
            }
    }
    std::vector<unsigned> ik, selectors;
    if (!DisplayMaps(live, model, version, ik, selectors)) {
        reason = "cpu_pmm_display_mapping";
        return false;
    }
    if (!ValidTrack(model.boneKeys, live.boneCount, mdl::kBoneKeyCapacity) ||
        !ValidTrack(model.morphKeys, live.morphCount, mdl::kMorphKeyCapacity) ||
        !ValidTrack(model.displayKeys, 1, mdl::kDisplayKeyCapacity)) {
        reason = "invalid_cpu_pmm_links";
        return false;
    }
    bool valid = true;
    Each(model.boneKeys, [&](std::size_t, const auto& key) {
        for (float v : key.position)
            valid = valid && std::isfinite(v);
        for (float v : key.rotation)
            valid = valid && std::isfinite(v);
    });
    Each(model.morphKeys,
         [&](std::size_t, const auto& key) { valid = valid && std::isfinite(key.value); });
    Each(model.displayKeys, [&](std::size_t, const auto& key) {
        valid = valid && key.ikEnabled.size() == ik.size() &&
                key.rigidStates.size() == selectors.size();
    });
    if (!valid) {
        reason = "cpu_pmm_display_payload";
        return false;
    }
    reason.clear();
    return true;
}
bool PublishCpuPmmTracks(mdl::ModelRecord* raw, const format::PmmSceneModel& model,
                         format::PmmVersion version, std::string& reason) {
    if (!ValidateCpuPmmTracks(raw, model, version, reason))
        return false;
    auto& live = *raw;
    std::vector<unsigned> ik, selectors;
    DisplayMaps(live, model, version, ik, selectors);
    std::memset(live.boneKeys, 0, sizeof(mdl::BoneKey) * mdl::kBoneKeyCapacity);
    std::memset(live.morphKeys, 0, sizeof(mdl::MorphKey) * mdl::kMorphKeyCapacity);
    Each(model.boneKeys, [&](std::size_t index, const auto& source) {
        auto& key = live.boneKeys[index];
        key.frame = source.frame;
        key.previous = source.previous;
        key.next = source.next;
        std::copy(source.interpolation.begin(), source.interpolation.end(), key.interpolation);
        std::copy(source.position.begin(), source.position.end(), key.position);
        std::copy(source.rotation.begin(), source.rotation.end(), key.rotation);
        key.allocated = source.allocated;
        key.physicsDisabled = source.physicsDisabled;
    });
    Each(model.morphKeys, [&](std::size_t index, const auto& source) {
        auto& key = live.morphKeys[index];
        key.frame = source.frame;
        key.previous = source.previous;
        key.next = source.next;
        key.value = source.value;
        key.allocated = source.allocated;
    });
    for (std::size_t n = 0; n < mdl::kDisplayKeyCapacity; ++n) {
        auto& key = live.displayKeys[n];
        key.frame = key.previous = key.next = 0;
        key.visible = 1;
        key.allocated = 0;
        for (unsigned i = 0; i < live.ikChainCount; ++i)
            key.ikStates[i] = 1;
        for (unsigned i = 0; i < live.boneOrderCount; ++i)
            key.selectorStates[i] = {-1, 0};
    }
    Each(model.displayKeys, [&](std::size_t index, const auto& source) {
        auto& key = live.displayKeys[index];
        key.frame = source.frame;
        key.previous = source.previous;
        key.next = source.next;
        key.visible = source.visible;
        key.allocated = source.allocated;
        for (unsigned n = 0; n < ik.size(); ++n)
            key.ikStates[ik[n]] = source.ikEnabled[n];
        for (unsigned n = 0; n < selectors.size(); ++n)
            key.selectorStates[selectors[n]] = {source.rigidStates[n].modelIndex,
                                                source.rigidStates[n].boneIndex};
    });
    live.comboSelIndex = model.selection.comboIndex;
    live.comboSelIndex2 =
        version == format::PmmVersion::V2 ? model.previousDisplayOrder : model.selection.comboIndex;
    live.maxFrame = model.selection.maxFrame;
    live.edgeScale = model.edgeScale;
    live.toonFlag = model.toonFlag;
    reason.clear();
    return true;
}
bool ApplyMappedCpuPmmPose(mdl::ModelRecord* raw, const format::PmmSceneModel& model,
                           const format::PmmEvaluatedModel& pose, format::PmmVersion version) {
    const auto& live = *raw;
    std::vector<unsigned> ik, selectors;
    if (!DisplayMaps(live, model, version, ik, selectors) || pose.ikEnabled.size() != ik.size() ||
        pose.selectors.size() != selectors.size())
        return false;
    auto mapped = pose;
    mapped.ikEnabled.resize(live.ikChainCount);
    for (unsigned n = 0; n < live.ikChainCount; ++n)
        mapped.ikEnabled[n] = live.ikChains[n].enabled;
    for (unsigned n = 0; n < ik.size(); ++n)
        mapped.ikEnabled[ik[n]] = pose.ikEnabled[n];
    mapped.selectors.resize(live.boneOrderCount);
    for (unsigned n = 0; n < live.boneOrderCount; ++n) {
        const auto& from = live.boneOrderTable[n];
        mapped.selectors[n] = {from.linkedModel, from.linkedBone, from.windowStart, from.windowEnd};
    }
    for (unsigned n = 0; n < selectors.size(); ++n)
        mapped.selectors[selectors[n]] = pose.selectors[n];
    ApplyCpuPmmPose(raw, mapped, version == format::PmmVersion::V2);
    return true;
}
void ApplyCpuPmmPose(mdl::ModelRecord* raw, const format::PmmEvaluatedModel& pose,
                     bool applySelectors) {
    auto& live = *raw;
    if (pose.bones.size() != live.boneCount || pose.morphValues.size() != live.morphCount ||
        pose.ikEnabled.size() != live.ikChainCount ||
        (applySelectors && pose.selectors.size() != live.boneOrderCount))
        return;
    live.loadComplete = pose.visible;
    for (std::uint32_t i = 0; i < live.ikChainCount; ++i)
        live.ikChains[i].enabled = pose.ikEnabled[i];
    for (std::uint32_t i = 0; applySelectors && i < live.boneOrderCount; ++i) {
        auto& target = live.boneOrderTable[i];
        const auto& source = pose.selectors[i];
        target.linkedModel = source.linkedModel;
        target.linkedBone = source.linkedBone;
        target.windowStart = source.windowStart;
        target.windowEnd = source.windowEnd;
    }
    for (std::uint32_t i = 0; i < live.morphCount; ++i)
        live.morphs[i].value = pose.morphValues[i];
    for (std::uint32_t i = 0; i < live.boneCount; ++i) {
        auto& target = live.boneTable[i];
        const auto type = static_cast<std::int8_t>(target.type);
        if ((type > static_cast<std::int8_t>(mdl::BoneType::Effector) &&
             target.type != mdl::BoneType::FixedAxis) ||
            target.hasRigidBody || target.slotIndex >= 0)
            continue;
#ifdef MIKUDANCESTUDIO_DIAG
        if (const char* path = std::getenv("MDS_BONE_POSE_COMPARE")) {
            float translationDelta = 0.0f;
            float rotationDelta = 0.0f;
            for (int component = 0; component < 3; ++component)
                translationDelta =
                    std::max(translationDelta, std::fabs(target.trans[component] -
                                                         pose.bones[i].translation[component]));
            for (int component = 0; component < 4; ++component)
                rotationDelta =
                    std::max(rotationDelta, std::fabs(target.rotQuat[component] -
                                                      pose.bones[i].rotation[component]));
            if (translationDelta > 0.001f || rotationDelta > 0.001f) {
                if (auto* output = std::fopen(path, "a")) {
                    std::fprintf(
                        output,
                        "slot=%u bone=%u name=%.20s translation_delta=%g rotation_delta=%g "
                        "live_translation=(%g,%g,%g) session_translation=(%g,%g,%g)\n",
                        pose.slot, i, target.name, translationDelta, rotationDelta, target.trans[0],
                        target.trans[1], target.trans[2], pose.bones[i].translation[0],
                        pose.bones[i].translation[1], pose.bones[i].translation[2]);
                    std::fclose(output);
                }
            }
        }
#endif
        std::copy(pose.bones[i].translation.begin(), pose.bones[i].translation.end(), target.trans);
        std::copy(pose.bones[i].rotation.begin(), pose.bones[i].rotation.end(), target.rotQuat);
        // Native seek/playback changes this flag only for rigid-body bones.
        // The caller's native physics path handles those bones; ordinary
        // bones retain their saved flag even when a key has another value.
    }
}
}
