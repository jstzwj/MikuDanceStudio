// Source-level views of the four application-wide timeline key tables.
//
// These records are stored by value and contain no native pointers, so their
// ABI is identical in the 32-bit original and the 64-bit build.  Fields whose
// meaning is not yet established remain reserved; keeping them here prevents
// UI code from duplicating record strides and byte offsets.
#pragma once

#include <cstddef>
#include <cstdint>

namespace mikudancestudio::mdl {

// The original allocates every global and accessory timeline table for this
// fixed frame domain.  PMM stores sparse records indexed directly by frame.
inline constexpr std::size_t kTimelineKeyCapacity = 10000;

struct CameraKey {
    std::uint32_t frame;
    std::uint32_t previous;
    std::uint32_t next;
    float distance;
    float eye[3];
    float target[3];
    std::uint8_t interpolation[4][6];
    std::uint8_t perspective;
    std::uint8_t reserved1[3];
    std::int32_t fov;
    std::uint8_t selected;
    std::uint8_t reserved2[3];
    std::int32_t parentModel;
    std::int32_t parentBone;
};

struct LightKey {
    std::uint32_t frame;
    std::uint32_t previous;
    std::uint32_t next;
    float direction[3];
    float color[3];
    std::uint8_t selected;
    std::uint8_t reserved1[3];
};

struct SelfShadowKey {
    std::uint32_t frame;
    std::uint32_t previous;
    std::uint32_t next;
    std::uint8_t mode;
    std::uint8_t reserved0[3];
    float distance;
    std::uint8_t selected;
    std::uint8_t reserved1[3];
};

struct GravityKey {
    std::uint32_t frame;
    std::uint32_t previous;
    std::uint32_t next;
    float acceleration;
    float direction[3];
    std::int32_t noise;
    std::uint8_t noiseEnabled;
    std::uint8_t selected;
    std::uint8_t reserved1[2];
};

static_assert(sizeof(CameraKey) == 84, "camera key ABI");
static_assert(offsetof(CameraKey, frame) == 0, "camera frame ABI");
static_assert(offsetof(CameraKey, previous) == 4, "camera previous ABI");
static_assert(offsetof(CameraKey, next) == 8, "camera next ABI");
static_assert(offsetof(CameraKey, interpolation) == 40, "camera interpolation ABI");
static_assert(offsetof(CameraKey, perspective) == 64, "camera perspective ABI");
static_assert(offsetof(CameraKey, fov) == 68, "camera fov ABI");
static_assert(offsetof(CameraKey, selected) == 72, "camera selected ABI");
static_assert(offsetof(CameraKey, parentModel) == 76, "camera parent model ABI");
static_assert(offsetof(CameraKey, parentBone) == 80, "camera parent bone ABI");
static_assert(sizeof(LightKey) == 40, "light key ABI");
static_assert(offsetof(LightKey, frame) == 0, "light frame ABI");
static_assert(offsetof(LightKey, previous) == 4, "light previous ABI");
static_assert(offsetof(LightKey, next) == 8, "light next ABI");
static_assert(offsetof(LightKey, selected) == 36, "light selected ABI");
static_assert(offsetof(LightKey, direction) == 12, "light direction ABI");
static_assert(offsetof(LightKey, color) == 24, "light color ABI");
static_assert(sizeof(SelfShadowKey) == 24, "self-shadow key ABI");
static_assert(offsetof(SelfShadowKey, frame) == 0, "self-shadow frame ABI");
static_assert(offsetof(SelfShadowKey, previous) == 4, "self-shadow previous ABI");
static_assert(offsetof(SelfShadowKey, next) == 8, "self-shadow next ABI");
static_assert(offsetof(SelfShadowKey, selected) == 20, "self-shadow selected ABI");
static_assert(offsetof(SelfShadowKey, mode) == 12, "self-shadow mode ABI");
static_assert(offsetof(SelfShadowKey, distance) == 16, "self-shadow distance ABI");
static_assert(sizeof(GravityKey) == 36, "gravity key ABI");
static_assert(offsetof(GravityKey, frame) == 0, "gravity frame ABI");
static_assert(offsetof(GravityKey, previous) == 4, "gravity previous ABI");
static_assert(offsetof(GravityKey, next) == 8, "gravity next ABI");
static_assert(offsetof(GravityKey, selected) == 33, "gravity selected ABI");
static_assert(offsetof(GravityKey, acceleration) == 12, "gravity acceleration ABI");
static_assert(offsetof(GravityKey, direction) == 16, "gravity direction ABI");
static_assert(offsetof(GravityKey, noise) == 28, "gravity noise ABI");
static_assert(offsetof(GravityKey, noiseEnabled) == 32, "gravity noise enabled ABI");

} // namespace mikudancestudio::mdl
