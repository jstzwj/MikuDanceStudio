// Runtime bone key shared by the model timeline and undo snapshots.
#pragma once

#include <cstddef>
#include <cstdint>

namespace mikudancestudio::mdl {

struct BoneKey {
    std::uint32_t frame;
    std::uint32_t previous;
    std::uint32_t next;
    std::uint8_t interpolation[16];
    float position[3];
    float rotation[4];
    std::uint8_t allocated;
    std::uint8_t physicsDisabled;
    std::uint8_t reserved[2];
};
static_assert(sizeof(BoneKey) == 60, "bone key ABI");
static_assert(offsetof(BoneKey, frame) == 0, "bone key frame ABI");
static_assert(offsetof(BoneKey, previous) == 4, "bone key previous ABI");
static_assert(offsetof(BoneKey, next) == 8, "bone key next ABI");
static_assert(offsetof(BoneKey, interpolation) == 12, "bone key interpolation ABI");
static_assert(offsetof(BoneKey, position) == 28, "bone key position ABI");
static_assert(offsetof(BoneKey, rotation) == 40, "bone key rotation ABI");
static_assert(offsetof(BoneKey, allocated) == 56, "bone key allocated ABI");
static_assert(offsetof(BoneKey, physicsDisabled) == 57, "bone key physics ABI");

} // namespace mikudancestudio::mdl
