// ===========================================================================
// Shared machinery for the per-model keyframe consumers
//   VA 0x004A31D0 (model_keyframe_advance.cpp - incremental advance) and
//   VA 0x004B4260 (model_frame_seek.cpp - direct frame seek).
// ===========================================================================
// Record layouts and the selector-refresh semantics are documented in the
// model_keyframe_advance.cpp header; both functions agree on them.
// =========================================================================//
#ifndef MIKUDANCESTUDIO_SRC_MODEL_KEYFRAME_COMMON_HPP
#define MIKUDANCESTUDIO_SRC_MODEL_KEYFRAME_COMMON_HPP

#include <cstdint>
#include <cstring>

#include "mikudancestudio/model.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"

namespace mikudancestudio {
namespace kfa {

// memcpy-based record accessors - no alignment or type-punning assumptions.
inline std::uint32_t Rd32(const unsigned char* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
inline std::int32_t RdI32(const unsigned char* p) {
    std::int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
inline float RdF32(const unsigned char* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}
inline void Wr32(unsigned char* p, std::uint32_t v) {
    std::memcpy(p, &v, 4);
}
inline void WrF32(unsigned char* p, float v) {
    std::memcpy(p, &v, 4);
}
// Per-IK display flags copy (0x4A329F / 0x4A3487 / 0x4A3709 /
// 0x4B42D9 / 0x4B46E5 / 0x4B43D2): one byte per IK chain.
inline void CopyIkDisplayFlags(mdl::ModelRecord* m, const mdl::DisplayKey& rec) {
    const auto* flags = mdl::IkStates(rec);
    mdl::IkChain* const chains = m->ikChains;
    const std::uint32_t count = m->ikChainCount;
    for (std::uint32_t k = 0; k < count; ++k)
        chains[k].enabled = flags[k];
}

inline bool ReadLiveDisplayKeyLink(void* context, std::uint32_t index,
                                   format::PmmDisplayKeyLink& output) {
    if (!context || index >= mdl::kDisplayKeyCapacity)
        return false;
    const auto& key = static_cast<const mdl::DisplayKey*>(context)[index];
    output = {key.frame, key.previous, key.next, key.visible};
    return true;
}

inline bool ReadLiveSelectorPair(void* context, std::uint32_t keyIndex, std::uint32_t selectorIndex,
                                 format::PmmRigidState& output) {
    if (!context || keyIndex >= mdl::kDisplayKeyCapacity)
        return false;
    const auto* keys = static_cast<const mdl::DisplayKey*>(context);
    if (!mdl::SelectorStates(keys[keyIndex]))
        return false;
    const auto& pair = mdl::SelectorStates(keys[keyIndex])[selectorIndex];
    output = {pair.modelIndex, pair.boneIndex};
    return true;
}

inline void ApplyDisplaySelection(mdl::ModelRecord* m, mdl::DisplayKey* keys,
                                  const format::PmmDisplaySelection& selection) {
    using Kind = format::PmmDisplaySelectionKind;
    if (selection.kind == Kind::None)
        return;
    m->loadComplete = selection.visible;
    CopyIkDisplayFlags(m, keys[selection.heldIndex]);
    mdl::BoneOrderEntry* const selectors = (m)->boneOrderTable;
    const auto count = m->boneOrderCount;
    for (std::uint32_t i = 0; i < count; ++i) {
        format::PmmEvaluatedSelector window;
        if (!format::EvaluatePmmSelectorWindow(ReadLiveDisplayKeyLink, ReadLiveSelectorPair, keys,
                                               selection, i, window))
            continue;
        auto& selector = selectors[i];
        selector.linkedModel = window.linkedModel;
        selector.linkedBone = window.linkedBone;
        selector.windowStart = window.windowStart;
        selector.windowEnd = window.windowEnd;
    }
}

// quaternion + position copy helpers for the bone snap branches
// (typed on BoneRecord: the offsets regrow per architecture)
inline void CopyBoneKeyVerbatim(mikudancestudio::mdl::BoneRecord* bone, const mdl::BoneKey& rec) {
    for (int c = 0; c < 4; ++c)
        bone->rotQuat[c] = rec.rotation[c];
    for (int c = 0; c < 3; ++c)
        bone->trans[c] = rec.position[c];
}
inline void WriteBonePrevVerbatim(mikudancestudio::mdl::BoneRecord* bone, const float pq[4],
                                  const float ppos[3]) {
    for (int c = 0; c < 4; ++c)
        bone->rotQuat[c] = pq[c];
    for (int c = 0; c < 3; ++c)
        bone->trans[c] = ppos[c];
}

inline format::PmmEvaluatedSelector PortableSelector(const mdl::BoneOrderEntry& window) {
    return {window.linkedModel, window.linkedBone, window.windowStart, window.windowEnd};
}
inline format::PmmBoneKey PortableBoneKey(const mdl::BoneKey& key) {
    format::PmmBoneKey result{};
    result.frame = key.frame;
    std::memcpy(result.position.data(), key.position, sizeof key.position);
    std::memcpy(result.rotation.data(), key.rotation, sizeof key.rotation);
    return result;
}
inline bool ApplyLiveBoneSelectorPose(mdl::BoneRecord* bone, format::PmmBoneSelectorResult decision,
                                      const mdl::BoneKey& previous, const mdl::BoneKey& next,
                                      bool playback, const float previousPosition[3] = nullptr,
                                      const float previousRotation[4] = nullptr) {
    if (decision == format::PmmBoneSelectorResult::Unchanged)
        return false;
    auto left = PortableBoneKey(previous);
    const auto right = PortableBoneKey(next);
    if (previousPosition)
        std::memcpy(left.position.data(), previousPosition, sizeof(float) * 3);
    if (previousRotation)
        std::memcpy(left.rotation.data(), previousRotation, sizeof(float) * 4);
    format::PmmEvaluatedBone pose;
    if (!format::ApplyPmmBoneSelectorPose(decision, left, right, playback, pose))
        return false;
    std::memcpy(bone->trans, pose.translation.data(), sizeof(float) * 3);
    std::memcpy(bone->rotQuat, pose.rotation.data(), sizeof(float) * 4);
    return true;
}

// Working-copy mirror for the IK-off arming: quat backup ikBackup[3..6] ->
// working ikWorkingQuat, position backup ikBackup[0..2] -> working
// ikWorkingPos (the mid-interpolation re-arm of both functions).
inline void MirrorBackupsToWorking(mikudancestudio::mdl::BoneRecord* bone) {
    for (int c = 0; c < 4; ++c)
        bone->ikWorkingQuat[c] = bone->ikBackup[3 + c];
    for (int c = 0; c < 3; ++c)
        bone->ikWorkingPos[c] = bone->ikBackup[c];
}
// The exact-hit arm additionally refreshes the CURRENT pose from the
// backups (0x4A3E54..0x4A3F11 / 0x4B4BD2..0x4B4C82).
inline void MirrorBackupsToCurrent(mikudancestudio::mdl::BoneRecord* bone) {
    for (int c = 0; c < 4; ++c)
        bone->rotQuat[c] = bone->ikBackup[3 + c];
    for (int c = 0; c < 3; ++c)
        bone->trans[c] = bone->ikBackup[c];
}

} // namespace kfa
} // namespace mikudancestudio

#endif // MIKUDANCESTUDIO_SRC_MODEL_KEYFRAME_COMMON_HPP
