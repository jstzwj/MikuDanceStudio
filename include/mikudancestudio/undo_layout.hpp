// Per-model undo records shared by bone editing and physics-pose capture.
#pragma once

#include <cstddef>
#include <cstdint>

namespace mikudancestudio::mdl {

inline constexpr int kUndoHistoryDepth = 30;

struct BonePoseSnapshot {
    std::int32_t boneIndex;
    float position[3];
    float rotation[4];
    std::uint8_t physicsDisabled;
    std::uint8_t reserved[3];
};
static_assert(sizeof(BonePoseSnapshot) == 36, "bone pose snapshot ABI");

// Values stored in the original undo/redo rings. ChainedBoneKeys joins a
// preceding key edit to the following entry (range-scale is one example).
enum class UndoOperation : std::int32_t {
    Empty = 0,
    BonePose = 1,
    BoneKeys = 2,
    BonePoseWithFrame = 3,
    ChainedBoneKeys = 4,
};

struct UndoRecord {
    UndoOperation operation;
    // Pose count for BonePose/BonePoseWithFrame; auxiliary key-record count
    // for BoneKeys/ChainedBoneKeys (their pose snapshot covers all bones).
    std::int32_t recordCount;
    std::int32_t reserved0;
    std::uint32_t frame;
    // Owned raw storage: allocate with ::operator new, release with
    // ::operator delete throughout edit/capture/disposal paths.
    BonePoseSnapshot* bonePose;
    void* auxiliaryPose;
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
static_assert(offsetof(UndoRecord, auxiliaryPose) == 24, "undo auxiliary pose x64");
#else
static_assert(sizeof(UndoRecord) == 28, "undo record x86 ABI");
static_assert(sizeof(UndoRing) == 840, "undo ring x86 ABI");
static_assert(offsetof(UndoRecord, bonePose) == 16, "undo bone pose x86");
static_assert(offsetof(UndoRecord, auxiliaryPose) == 20, "undo auxiliary pose x86");
#endif

} // namespace mikudancestudio::mdl
