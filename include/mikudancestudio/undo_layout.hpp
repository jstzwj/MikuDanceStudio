// Per-model undo records shared by bone editing and physics-pose capture.
#pragma once

#include <cstddef>
#include <cstdint>

#include "mikudancestudio/bone_key_layout.hpp"

namespace mikudancestudio::mdl {

inline constexpr int kUndoHistoryDepth = 30;

struct BonePoseSnapshot {
    std::int32_t boneIndex;
    float position[3];
    float rotation[4];
    // Saved bonePhysicsState edit marker, not BoneRecord::physicsDisabled.
    std::uint8_t poseEdited;
    std::uint8_t reserved[3];
};
static_assert(sizeof(BonePoseSnapshot) == 36, "bone pose snapshot ABI");
static_assert(offsetof(BonePoseSnapshot, poseEdited) == 32, "bone pose edit marker ABI");

// Values stored in the original undo/redo rings. ChainedBoneKeys joins a
// preceding key edit to the following entry (range-scale is one example).
enum class UndoOperation : std::int32_t {
    Empty = 0,
    BonePose = 1,
    BoneKeys = 2,
    BonePoseWithFrame = 3,
    ChainedBoneKeys = 4,
};

// One displaced timeline slot; the key index is independent of its bone owner.
struct BoneKeyUndoEntry {
    std::int32_t index;
    BoneKey key;
};
static_assert(sizeof(BoneKeyUndoEntry) == 64, "bone-key undo record ABI");
static_assert(offsetof(BoneKeyUndoEntry, key) == 4, "bone-key undo payload ABI");

inline constexpr std::size_t kUndoKeysPerInsertion = 3;
inline constexpr std::size_t kTraceUndoKeysPerSample = 54;

struct UndoRecord {
    UndoOperation operation;
    // Pose count for BonePose/BonePoseWithFrame; keySnapshots count
    // for BoneKeys/ChainedBoneKeys (their pose snapshot covers all bones).
    std::int32_t recordCount;
    std::int32_t reserved0;
    std::uint32_t frame;
    // Owned arrays: allocate with scalar ::operator new, release with
    // ::operator delete throughout edit/capture/disposal paths.
    BonePoseSnapshot* bonePose;
    BoneKeyUndoEntry* keySnapshots;
    std::int32_t reserved1;
};

struct UndoRing {
    UndoRecord slots[kUndoHistoryDepth];
};

static_assert(sizeof(UndoOperation) == 4, "undo operation ABI");
static_assert(offsetof(UndoRecord, recordCount) == 4, "undo record count ABI");

#if defined(_M_X64) || defined(__x86_64__)
static_assert(sizeof(UndoRecord) == 40, "undo record x64 ABI");
static_assert(sizeof(UndoRing) == 1200, "undo ring x64 ABI");
static_assert(offsetof(UndoRecord, bonePose) == 16, "undo bone pose x64");
static_assert(offsetof(UndoRecord, keySnapshots) == 24, "undo key snapshots x64");
#else
static_assert(sizeof(UndoRecord) == 28, "undo record x86 ABI");
static_assert(sizeof(UndoRing) == 840, "undo ring x86 ABI");
static_assert(offsetof(UndoRecord, bonePose) == 16, "undo bone pose x86");
static_assert(offsetof(UndoRecord, keySnapshots) == 20, "undo key snapshots x86");
#endif

} // namespace mikudancestudio::mdl
