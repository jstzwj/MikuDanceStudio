#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mikudancestudio::mdl {

// One external-parent selector, shared by playback, PMM and the edit dialog.
// Slot zero represents the model root. The dialog edits a separate copy of
// these records, including the active frame interval.
struct BoneOrderEntry {
    std::int32_t boneIndex;
    std::uint32_t windowStart;
    std::uint32_t windowEnd;
    std::int32_t linkedModel;  // -1 none, -2 ground, otherwise model slot
    std::int32_t linkedBone;
};
static_assert(std::is_trivially_copyable_v<BoneOrderEntry>);
static_assert(sizeof(BoneOrderEntry) == 20);
static_assert(offsetof(BoneOrderEntry, windowStart) == 4);
static_assert(offsetof(BoneOrderEntry, windowEnd) == 8);
static_assert(offsetof(BoneOrderEntry, linkedModel) == 12);
static_assert(offsetof(BoneOrderEntry, linkedBone) == 16);

inline BoneOrderEntry* CopyBoneBindings(const BoneOrderEntry* source,
                                       std::size_t count) {
    auto* copy = new BoneOrderEntry[count];
    if (count != 0) std::copy_n(source, count, copy);
    return copy;
}

inline void DetachBoneBindings(BoneOrderEntry* entries, std::size_t count,
                              std::int32_t removedModel) {
    for (std::size_t i = 0; i < count; ++i) {
        auto& entry = entries[i];
        if (entry.linkedModel == removedModel) {
            entry.linkedModel = -1;
            entry.linkedBone = 0;
        }
    }
}

}  // namespace mikudancestudio::mdl
