#pragma once

#include <cstddef>
#include <cstdint>

namespace mikudancestudio::mdl {

// Runtime accessory object.  Its first four fields are native pointers;
// consequently the scalar/string tail starts 16 bytes later in the x64
// executable.  The opaque block between the resource pointers and editable
// transform is retained only at this ABI boundary.
struct AccessoryTexturePaths {
    wchar_t primary[512];
    wchar_t sphere[512];
};
static_assert(sizeof(AccessoryTexturePaths) == 2048);

struct AccessoryRecord {
    void* mesh;
    void* materials;
    AccessoryTexturePaths* texturePaths;
    std::uint8_t* textureTypes;
    wchar_t directory[256];
    std::uint8_t visible;
    std::uint8_t reservedVisibility[3];
    float position[3];
    float rotation[3];
    float scale;
    std::int32_t parentModel;
    std::int32_t parentBone;
    char name[100];
    wchar_t sourcePath[256];
    std::uint8_t shadowEnabled;
    std::uint8_t order;
    std::uint8_t additiveBlend;
    std::uint8_t reservedFlags;
    float opacity;
    std::uint32_t materialCount;
    std::int32_t currentMaterial;
    // +0x4AC x86 / +0x4BC x64: timeline row flag - set on the accessory's
    // registration row (combo 0x1D7 selection, x86 0x48E2A4) and read by
    // the pump's Enter register-frame block (x64 0x44FB0A) to decide which
    // accessory tracks get a keyframe.
    std::uint8_t rowSelected;
    std::uint8_t reservedTail[3];
};

#if defined(_M_X64)
static_assert(sizeof(AccessoryRecord) == 0x4C0, "accessory x64 size");
static_assert(offsetof(AccessoryRecord, visible) == 544,
          "AccessoryRecord.visible x64 ABI");
static_assert(offsetof(AccessoryRecord, position) == 548,
          "AccessoryRecord.position x64 ABI");
static_assert(offsetof(AccessoryRecord, parentModel) == 576,
          "AccessoryRecord.parentModel x64 ABI");
static_assert(offsetof(AccessoryRecord, name) == 584,
          "AccessoryRecord.name x64 ABI");
static_assert(offsetof(AccessoryRecord, shadowEnabled) == 1196,
          "AccessoryRecord.shadowEnabled x64 ABI");
static_assert(offsetof(AccessoryRecord, opacity) == 1200,
          "AccessoryRecord.opacity x64 ABI");
static_assert(offsetof(AccessoryRecord, rowSelected) == 1212,
              "rowSelected x64 (+0x4BC)");
#else
static_assert(sizeof(AccessoryRecord) == 0x4B0, "accessory x86 size");
static_assert(offsetof(AccessoryRecord, visible) == 528,
          "AccessoryRecord.visible x86 ABI");
static_assert(offsetof(AccessoryRecord, position) == 532,
          "AccessoryRecord.position x86 ABI");
static_assert(offsetof(AccessoryRecord, parentModel) == 560,
          "AccessoryRecord.parentModel x86 ABI");
static_assert(offsetof(AccessoryRecord, name) == 568,
          "AccessoryRecord.name x86 ABI");
static_assert(offsetof(AccessoryRecord, shadowEnabled) == 1180,
          "AccessoryRecord.shadowEnabled x86 ABI");
static_assert(offsetof(AccessoryRecord, opacity) == 1184,
          "AccessoryRecord.opacity x86 ABI");
static_assert(offsetof(AccessoryRecord, rowSelected) == 1196,
              "rowSelected x86 (+0x4AC)");
#endif

inline AccessoryRecord* Accessory(void* object) {
    return static_cast<AccessoryRecord*>(object);
}

inline const AccessoryRecord* Accessory(const void* object) {
    return static_cast<const AccessoryRecord*>(object);
}

// One entry in an accessory's 10000-key timeline.  Unlike model BoneKey,
// this record stores the accessory's attachment and render state; the two
// formats only happen to share a 60-byte size.
struct AccessoryKey {
    std::uint32_t frame;
    std::uint32_t previous;
    std::uint32_t next;
    std::uint8_t visible;
    std::uint8_t shadowEnabled;
    std::uint8_t reserved[2];
    std::int32_t parentModel;
    std::int32_t parentBone;
    std::uint8_t selected;
    std::uint8_t reservedSelection[3];
    float position[3];
    float rotation[3];
    float scale;
    float opacity;
};
static_assert(sizeof(AccessoryKey) == 60, "accessory key ABI");
static_assert(offsetof(AccessoryKey, frame) == 0, "accessory frame ABI");
static_assert(offsetof(AccessoryKey, previous) == 4,
              "accessory previous ABI");
static_assert(offsetof(AccessoryKey, next) == 8, "accessory next ABI");
static_assert(offsetof(AccessoryKey, visible) == 12,
              "accessory visible ABI");
static_assert(offsetof(AccessoryKey, parentModel) == 16,
              "accessory attachment ABI");
static_assert(offsetof(AccessoryKey, selected) == 24,
              "accessory selection ABI");
static_assert(offsetof(AccessoryKey, position) == 28,
              "accessory position ABI");
static_assert(offsetof(AccessoryKey, rotation) == 40,
              "accessory rotation ABI");
static_assert(offsetof(AccessoryKey, scale) == 52,
              "accessory scale ABI");
static_assert(offsetof(AccessoryKey, opacity) == 56,
              "accessory opacity ABI");

// Clipboard form used by the accessory paste command.  It omits the linked
// list indices and selection flag, and stores a frame offset plus source slot.
struct AccessoryClipboardKey {
    std::uint32_t frameOffset;
    std::uint8_t slot;
    std::uint8_t reserved0[3];
    std::uint8_t visible;
    std::uint8_t shadowEnabled;
    std::uint8_t reserved1[2];
    std::int32_t parentModel;
    std::int32_t parentBone;
    float position[3];
    float rotation[3];
    float scale;
    float opacity;
};
static_assert(sizeof(AccessoryClipboardKey) == 52,
              "accessory clipboard ABI");
static_assert(offsetof(AccessoryClipboardKey, parentModel) == 12,
              "accessory clipboard attachment ABI");
static_assert(offsetof(AccessoryClipboardKey, position) == 20,
              "accessory clipboard position ABI");
static_assert(offsetof(AccessoryClipboardKey, rotation) == 32,
              "accessory clipboard rotation ABI");
static_assert(offsetof(AccessoryClipboardKey, scale) == 44,
              "accessory clipboard scale ABI");
static_assert(offsetof(AccessoryClipboardKey, opacity) == 48,
              "accessory clipboard opacity ABI");

}  // namespace mikudancestudio::mdl
