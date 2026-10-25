#pragma once

#include <cstddef>
#include <cstdint>

namespace mikudancestudio::mdl {

// Current bone poses copied by the bone panel, without timeline key data.
struct BonePoseClipboardRecord {
    char name[20];
    float position[3];
    float rotation[4];
};
static_assert(sizeof(BonePoseClipboardRecord) == 0x30, "bone pose clipboard ABI");

struct ClipboardSelectionCounts {
    std::uint32_t bones;
    std::uint32_t morphs;
    std::uint32_t displays;
    std::uint32_t cameras;
    std::uint32_t lights;
    std::uint32_t shadows;
    std::uint32_t gravity;
    std::uint32_t accessories;
};
static_assert(sizeof(ClipboardSelectionCounts) == 32, "clipboard selection counts ABI");

struct BoneClipboardRecord {
    char name[30];
    std::uint8_t reserved0[2];
    std::uint32_t frame;
    float rotation[4];
    float position[3];
    std::uint8_t physicsDisabled;
    std::uint8_t interpolation[16];
    std::uint8_t reserved1[3];
};
static_assert(sizeof(BoneClipboardRecord) == 84, "bone clipboard ABI");

struct MorphClipboardRecord {
    char name[30];
    std::uint8_t reserved[2];
    std::uint32_t frame;
    float value;
};
static_assert(sizeof(MorphClipboardRecord) == 40, "morph clipboard ABI");

struct IkClipboardState {
    char boneName[20];
    std::uint8_t enabled;
};
static_assert(sizeof(IkClipboardState) == 21, "IK clipboard state ABI");

struct SelectorClipboardState {
    char boneName[20];
    std::int32_t modelIndex;
    std::int32_t boneIndex;
};
static_assert(sizeof(SelectorClipboardState) == 28, "selector clipboard state ABI");

struct DisplayClipboardRecord {
    std::uint32_t frame;
    std::uint8_t visible;
    std::uint8_t reserved[3];
    std::int32_t ikCount;
    IkClipboardState* ikStates;
    std::int32_t selectorCount;
    SelectorClipboardState* selectorStates;
};
#if defined(_M_X64)
static_assert(sizeof(DisplayClipboardRecord) == 40, "display clipboard x64 ABI");
static_assert(offsetof(DisplayClipboardRecord, ikStates) == 16, "display clipboard x64 IK pointer");
static_assert(offsetof(DisplayClipboardRecord, selectorCount) == 24,
              "display clipboard x64 selector count");
static_assert(offsetof(DisplayClipboardRecord, selectorStates) == 32,
              "display clipboard x64 selector pointer");
#else
static_assert(sizeof(DisplayClipboardRecord) == 24, "display clipboard x86 ABI");
static_assert(offsetof(DisplayClipboardRecord, ikStates) == 12, "display clipboard x86 IK pointer");
static_assert(offsetof(DisplayClipboardRecord, selectorCount) == 16,
              "display clipboard x86 selector count");
static_assert(offsetof(DisplayClipboardRecord, selectorStates) == 20,
              "display clipboard x86 selector pointer");
#endif

struct CameraClipboardRecord {
    std::uint32_t frame;
    float eye[3];
    float target[3];
    std::int32_t fov;
    std::uint8_t perspective;
    std::uint8_t interpolation[24];
    std::uint8_t reserved[3];
    float distance;
    std::int32_t parentModel;
    std::int32_t parentBone;
};
static_assert(sizeof(CameraClipboardRecord) == 72, "camera clipboard ABI");

struct LightClipboardRecord {
    std::uint32_t frame;
    float direction[3];
    float color[3];
};
static_assert(sizeof(LightClipboardRecord) == 28, "light clipboard ABI");

struct ShadowClipboardRecord {
    std::uint32_t frame;
    std::uint8_t mode;
    std::uint8_t reserved[3];
    float distance;
};
static_assert(sizeof(ShadowClipboardRecord) == 12, "shadow clipboard ABI");

struct GravityClipboardRecord {
    std::uint32_t frame;
    float acceleration;
    float direction[3];
    std::int32_t noise;
    std::uint8_t noiseEnabled;
    std::uint8_t reserved[3];
};
static_assert(sizeof(GravityClipboardRecord) == 28, "gravity clipboard ABI");

} // namespace mikudancestudio::mdl
