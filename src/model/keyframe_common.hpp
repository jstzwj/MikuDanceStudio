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
inline void Wr32(unsigned char* p, std::uint32_t v) { std::memcpy(p, &v, 4); }
inline void WrF32(unsigned char* p, float v) { std::memcpy(p, &v, 4); }
// Per-IK display flags copy (0x4A329F / 0x4A3487 / 0x4A3709 /
// 0x4B42D9 / 0x4B46E5 / 0x4B43D2): one byte per IK chain.
inline void CopyIkDisplayFlags(unsigned char* m, const mdl::DisplayKey& rec) {
    const auto* flags = mdl::IkStates(rec);
    mdl::IkChain* const chains = mdl::IkChains(m);
    const std::uint32_t count = mdl::Mdl(m)->ikChainCount;
    for (std::uint32_t k = 0; k < count; ++k)
        chains[k].enabled = flags[k];
}

// Selector refresh, common tail of the IK master-track updates
// (0x4A32DE / 0x4A34C9 / 0x4A373E and 0x4B430F / 0x4B471F / 0x4B43EF).
// sel+12/+16 always take the pair of pairIdx's record.  sel+4: backward
// walk from backStart over the 28-byte records while the pair is unchanged;
// 0 if it reaches the frame-0 sentinel, else the frame of the record after
// the walked predecessor.  sel+8: the "with forward" variants walk forward
// from fwdStart while nextIdx != 0 and the pair of the record stepped to
// still matches; 0 on nextIdx == 0, else the frame of the first differing
// record.  The forward walk is entered when sel+12/+16 match fwdPairIdx's
// pair (tautology in the exact-hit variant - same record - a real
// prev-vs-cursor test in the between-keys variant); otherwise sel+8 takes
// fwdStart's frame.  The terminal variant passes withForward=false.
inline void RefreshIkSelectors(unsigned char* m, mdl::DisplayKey* keys,
                               int pairIdx, int backStart, bool withForward,
                               int fwdStart, int fwdPairIdx) {
    mdl::BoneOrderEntry* const selectors = mdl::BoneOrder(m);
    const int cnt = static_cast<int>(mdl::Mdl(m)->boneOrderCount);
    for (int i = 0; i < cnt; ++i) {
        mdl::BoneOrderEntry& selector = selectors[i];
        const mdl::BoneReference& pair = mdl::SelectorStates(keys[pairIdx])[i];
        selector.linkedModel = pair.modelIndex;
        selector.linkedBone = pair.boneIndex;
        int walk = backStart;
        for (;;) {
            const mdl::DisplayKey& wrec = keys[walk];
            if (wrec.frame == 0) {  // frame-0 sentinel record
                selector.windowStart = 0;
                break;
            }
            const int prev = static_cast<int>(wrec.previous);
            const mdl::BoneReference& previousPair =
                mdl::SelectorStates(keys[prev])[i];
            if (selector.linkedBone == previousPair.boneIndex &&
                selector.linkedModel == previousPair.modelIndex) {
                walk = prev;
                continue;
            }
            // frame of the record following the walked predecessor
            selector.windowStart = keys[keys[prev].next].frame;
            break;
        }
        if (!withForward) {
            selector.windowEnd = 0;
            continue;
        }
        const mdl::BoneReference& forwardPair =
            mdl::SelectorStates(keys[fwdPairIdx])[i];
        if (selector.linkedBone == forwardPair.boneIndex &&
            selector.linkedModel == forwardPair.modelIndex) {
            int f = fwdStart;
            bool toEnd = false;
            for (;;) {
                const int next = static_cast<int>(keys[f].next);
                if (next == 0) {
                    toEnd = true;
                    break;
                }
                f = next;
                const mdl::BoneReference& nextPair =
                    mdl::SelectorStates(keys[f])[i];
                if (!(selector.linkedBone == nextPair.boneIndex &&
                      selector.linkedModel == nextPair.modelIndex))
                    break;
            }
            selector.windowEnd = toEnd ? 0u : keys[f].frame;
        } else {
            selector.windowEnd = keys[fwdStart].frame;
        }
    }
}

// quaternion + position copy helpers for the bone snap branches
// (typed on BoneRecord: the offsets regrow per architecture)
inline void CopyBoneKeyVerbatim(mikudancestudio::mdl::BoneRecord* bone,
                                const mdl::BoneKey& rec) {
    for (int c = 0; c < 4; ++c)
        bone->rotQuat[c] = rec.rotation[c];
    for (int c = 0; c < 3; ++c)
        bone->trans[c] = rec.position[c];
}
inline void WriteBonePrevVerbatim(mikudancestudio::mdl::BoneRecord* bone,
                                  const float pq[4], const float ppos[3]) {
    for (int c = 0; c < 4; ++c) bone->rotQuat[c] = pq[c];
    for (int c = 0; c < 3; ++c) bone->trans[c] = ppos[c];
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

}  // namespace kfa
}  // namespace mikudancestudio

#endif  // MIKUDANCESTUDIO_SRC_MODEL_KEYFRAME_COMMON_HPP
