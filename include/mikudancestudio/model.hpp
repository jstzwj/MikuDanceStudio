// Model-facing API and compatibility boundary.
//
// The reverse-engineered executable stores one model in a flat allocation.
// The actual field map lives in model_layout.hpp; that runtime record is
// the only place where ABI offsets and unknown padding belong.  Code below
// exposes typed views for the portions whose ownership and element type are
// established.  New call sites should use those views instead of inventing
// another numeric offset.
#pragma once

#include <cstddef>
#include <cstdint>

#include "mikudancestudio/bone_layout.hpp"
#include "mikudancestudio/model_layout.hpp"
#include "mikudancestudio/subrecord_layout.hpp"

namespace mikudancestudio::mdl {

// The Win32 record is 0x4CCF4 bytes.  The x64 twin has wider pointer-backed
// tables and architecture-specific bookkeeping, so in-memory allocation must
// follow the compiled ABI rather than the Win32 byte count.
constexpr std::size_t kSize = sizeof(ModelRecord);
// The x64 E build doubled the bone-key pool: the model allocates
// 0x2255100 bytes (600000 x 0x3C) at model+0x2790, sweeps end at byte
// 0x2255100 / record 0x927C0, and the overflow box prints 600000
// (x64 0x7FF7CB4D213D alloc, 0x7FF7CB47B16D box).  The x86 original
// keeps 300000 (0x493E0).
#if defined(_M_X64)
constexpr std::size_t kBoneKeyCapacity = 600000;
#else
constexpr std::size_t kBoneKeyCapacity = 300000;
#endif
constexpr std::size_t kMorphKeyCapacity = 20000;
constexpr std::size_t kDisplayKeyCapacity = 1000;

// Select physics-enabled keys of bones that have rigid bodies.
void SelectPhysicsOnBoneKeys(ModelRecord& model);

// Five-int entries used by the post-load bone evaluation order.  This table
// contains no pointers, so its 20-byte stride is identical on x86 and x64.
struct BoneReference {
    std::int32_t modelIndex;
    std::int32_t boneIndex;
};

struct IkChain {
    std::int32_t boneIndex;
    std::int32_t targetBone;
    std::uint8_t linkCount;
    std::uint8_t reserved0[3];
    std::uint16_t* links;
    std::uint16_t iterations;
    std::uint8_t enabled;
    std::uint8_t reserved1;
    float maxAngle;
};

struct FrameGroup {
    char name[20];
    char nameEn[20];
    // Both facial and bone display rows use this common pair.  The target
    // is a morph index for facial rows and a bone index for bone rows.
    std::uint16_t groupIndex;
    std::uint16_t targetIndex;
    std::uint8_t selected;
    std::uint8_t reserved;
};
static_assert(sizeof(FrameGroup) == 46, "frame group ABI");
static_assert(offsetof(FrameGroup, name) == 0, "frame group name ABI");
static_assert(offsetof(FrameGroup, nameEn) == 20,
              "frame group English name ABI");
static_assert(offsetof(FrameGroup, groupIndex) == 40,
              "frame group index ABI");
static_assert(offsetof(FrameGroup, targetIndex) == 42,
              "frame group target ABI");
static_assert(offsetof(FrameGroup, selected) == 44,
              "frame group selection ABI");

// One entry in the display-group name table.  The final byte is tested by
// several UI paths, but its zero/non-zero polarity has not yet been unified.
struct DisplayGroup {
    char name[50];
    char nameEn[50];
    std::uint8_t flags;
};
static_assert(sizeof(DisplayGroup) == 101, "display group ABI");
static_assert(offsetof(DisplayGroup, name) == 0,
              "display group name ABI");
static_assert(offsetof(DisplayGroup, nameEn) == 50,
              "display group English name ABI");
static_assert(offsetof(DisplayGroup, flags) == 100,
              "display group flags ABI");

// PMX display-frame entries normalize the variable-width file index to a
// signed 32-bit value.  They are temporary loader records only.
struct PmxDisplayFrameEntry {
    std::uint8_t type;  // 0 bone, 1 morph
    unsigned char padding[3];
    std::int32_t index;
};
static_assert(sizeof(PmxDisplayFrameEntry) == 8,
              "PMX display-frame entry ABI");

#if MIKUDANCESTUDIO_X64
static_assert(sizeof(IkChain) == 32, "IK chain x64 ABI");
static_assert(offsetof(IkChain, links) == 16, "IK chain links x64 ABI");
static_assert(offsetof(IkChain, iterations) == 24,
              "IK chain iterations x64 ABI");
static_assert(offsetof(IkChain, enabled) == 26, "IK chain enabled x64 ABI");
static_assert(offsetof(IkChain, maxAngle) == 28,
              "IK chain angle x64 ABI");
#else
static_assert(sizeof(IkChain) == 24, "IK chain x86 ABI");
static_assert(offsetof(IkChain, links) == 12, "IK chain links x86 ABI");
static_assert(offsetof(IkChain, iterations) == 16,
              "IK chain iterations x86 ABI");
static_assert(offsetof(IkChain, enabled) == 18, "IK chain enabled x86 ABI");
static_assert(offsetof(IkChain, maxAngle) == 20,
              "IK chain angle x86 ABI");
#endif

static_assert(sizeof(BoneReference) == 8, "bone reference ABI");

// Timeline records are ordinary source-level records in the original, not
// arbitrary byte buffers.  Bone and morph records contain no pointers and
// therefore keep their x86 size on x64.  DisplayKey contains two owned
// buffers, so it naturally grows from 28 to 40 bytes on x64.
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
static_assert(offsetof(BoneKey, interpolation) == 12,
              "bone key interpolation ABI");
static_assert(offsetof(BoneKey, position) == 28, "bone key position ABI");
static_assert(offsetof(BoneKey, rotation) == 40, "bone key rotation ABI");
static_assert(offsetof(BoneKey, allocated) == 56, "bone key allocated ABI");
static_assert(offsetof(BoneKey, physicsDisabled) == 57,
              "bone key physics ABI");

struct MorphKey {
    std::uint32_t frame;
    std::uint32_t previous;
    std::uint32_t next;
    float value;
    std::uint8_t allocated;
    std::uint8_t reserved[3];
};
static_assert(sizeof(MorphKey) == 20, "morph key ABI");
static_assert(offsetof(MorphKey, frame) == 0, "morph key frame ABI");
static_assert(offsetof(MorphKey, previous) == 4, "morph key previous ABI");
static_assert(offsetof(MorphKey, next) == 8, "morph key next ABI");
static_assert(offsetof(MorphKey, value) == 12, "morph key value ABI");
static_assert(offsetof(MorphKey, allocated) == 16, "morph key allocated ABI");

struct DisplayKey {
    std::uint32_t frame;
    std::uint32_t previous;
    std::uint32_t next;
    std::uint8_t visible;
    std::uint8_t reserved0[3];
    std::uint8_t* ikStates;
    std::uint8_t allocated;
    std::uint8_t reserved1[3];
    BoneReference* selectorStates;
};
#if MIKUDANCESTUDIO_X64
static_assert(sizeof(DisplayKey) == 40, "display key x64 ABI");
static_assert(offsetof(DisplayKey, ikStates) == 16,
              "display key IK state x64 ABI");
static_assert(offsetof(DisplayKey, allocated) == 24,
              "display key allocated x64 ABI");
static_assert(offsetof(DisplayKey, selectorStates) == 32,
              "display key selector state x64 ABI");
#else
static_assert(sizeof(DisplayKey) == 28, "display key x86 ABI");
static_assert(offsetof(DisplayKey, ikStates) == 16,
              "display key IK state x86 ABI");
static_assert(offsetof(DisplayKey, allocated) == 20,
              "display key allocated x86 ABI");
static_assert(offsetof(DisplayKey, selectorStates) == 24,
              "display key selector state x86 ABI");
#endif
static_assert(offsetof(DisplayKey, frame) == 0, "display key frame ABI");
static_assert(offsetof(DisplayKey, previous) == 4,
              "display key previous ABI");
static_assert(offsetof(DisplayKey, next) == 8, "display key next ABI");
static_assert(offsetof(DisplayKey, visible) == 12,
              "display key visible ABI");

inline BoneReference*& SelectorStates(DisplayKey& key) {
    return key.selectorStates;
}

inline std::uint8_t*& IkStates(DisplayKey& key) {
    return key.ikStates;
}

inline const std::uint8_t* IkStates(const DisplayKey& key) {
    return static_cast<const std::uint8_t*>(key.ikStates);
}

inline const BoneReference* SelectorStates(const DisplayKey& key) {
    return static_cast<const BoneReference*>(key.selectorStates);
}

constexpr std::size_t kPoseTraceFlag = offsetof(ModelRecord, poseTraceRecording);
constexpr std::size_t kPoseTraceBuffer = offsetof(ModelRecord, poseTraceBuffer);
constexpr std::size_t kPmdModelNameBytes = 20;

enum class PmxTextBufferSlot : std::size_t {
    japaneseName,
    englishName,
    japaneseComment,
    englishComment,
};

template <typename T>
inline T& At(unsigned char* m, std::size_t off) {
    return *reinterpret_cast<T*>(m + off);
}

template <typename T>
inline const T& At(const unsigned char* m, std::size_t off) {
    return *reinterpret_cast<const T*>(m + off);
}

inline std::uint8_t& PoseTraceFlag(unsigned char* m) {
    return reinterpret_cast<ModelRecord*>(m)->poseTraceRecording;
}

inline void*& PoseTraceBuffer(unsigned char* m) {
    return reinterpret_cast<ModelRecord*>(m)->poseTraceBuffer;
}

// Typed view of the model object: routes table pointers through
// ModelRecord so the field offset is correct on both architectures
// The typed record owns architecture-correct field placement.
inline ModelRecord* Mdl(unsigned char* m) {
    return reinterpret_cast<ModelRecord*>(m);
}

inline const ModelRecord* Mdl(const unsigned char* m) {
    return reinterpret_cast<const ModelRecord*>(m);
}

inline IkChain*& IkChains(unsigned char* m) {
    return Mdl(m)->ikChains;
}

inline PmdVertex*& PmdVertices(unsigned char* m) {
    return Mdl(m)->rawVertices;
}

inline std::uint32_t& BaseVertexMorphCount(unsigned char* m) {
    return Mdl(m)->morph0Count;
}

inline std::int32_t& BoneMorphOffsetCount(unsigned char* m) {
    return Mdl(m)->boneMorphCount;
}

inline PmdVertexMorphEntry*& BaseVertexMorphTable(unsigned char* m) {
    return Mdl(m)->morph0Table;
}

inline BoneMorphOffsetRecord*& BoneMorphOffsets(unsigned char* m) {
    return Mdl(m)->boneMorphTable;
}

template <typename T>
inline T*& ResourceAs(void*& storage) {
    return reinterpret_cast<T*&>(storage);
}

inline wchar_t*& PmxTextBuffer(unsigned char* m, PmxTextBufferSlot slot) {
    return Mdl(m)->pmxTextBuffers[static_cast<std::size_t>(slot)];
}

inline ModelMaterialRecord*& Materials(unsigned char* m) {
    return Mdl(m)->materials;
}

inline const ModelMaterialRecord& Material(const unsigned char* bytes) {
    return *reinterpret_cast<const ModelMaterialRecord*>(bytes);
}

inline FrameGroup*& RigidGroups(unsigned char* m) {
    return ResourceAs<FrameGroup>(Mdl(m)->rbGroups);
}

inline FrameGroup*& DisplayFrames(unsigned char* m) {
    return Mdl(m)->displayFrames;
}

inline DisplayGroup*& DisplayGroups(unsigned char* m) {
    return ResourceAs<DisplayGroup>(Mdl(m)->groupNames);
}

inline char (*PmdToonFileNames(unsigned char* m))[100] {
    return Mdl(m)->pmdToonFileNames;
}

inline std::uint16_t*& Indices(unsigned char* m) {
    return ResourceAs<std::uint16_t>(Mdl(m)->indices);
}

// PMX retains normalized 32-bit indices even when its GPU index buffer uses
// 16-bit elements. PMD owns the same storage through Indices().
inline std::int32_t*& PmxIndices(unsigned char* m) {
    return ResourceAs<std::int32_t>(Mdl(m)->indices);
}

inline PmxUvMorphCounts& UvMorphCounts(unsigned char* m) {
    return Mdl(m)->uvMorphCounts;
}

inline PmxUvMorphTables& UvMorphTables(unsigned char* m) {
    return Mdl(m)->uvMorphTables;
}

inline PmxUvMorphEntry*& PmxUvMorphBaseTable(unsigned char* m) {
    return UvMorphTables(m).byFamily[0];
}

inline PmxUvMorphEntry*& AdditionalUvMorphTable(unsigned char* m,
                                                 std::size_t family) {
    return UvMorphTables(m).byFamily[family + 1];
}

inline PmxMaterialMorphPools& MaterialMorphPools(unsigned char* m) {
    return Mdl(m)->materialMorphPools;
}

inline MaterialMorphPool*& MaterialMorphBase(unsigned char* m) {
    return MaterialMorphPools(m).base;
}

inline MaterialMorphPool*& MaterialMorphAdd(unsigned char* m) {
    return MaterialMorphPools(m).additive;
}

inline MaterialMorphPool*& MaterialMorphMul(unsigned char* m) {
    return MaterialMorphPools(m).multiplicative;
}

// Typed views of the per-model record tables.  Strides are the natural
// sizeof() of each record - 604/624 bones, 136/192 morphs,
// 172/192 rigids, 140/152 joints (x86/x64).
inline BoneRecord*& Bones(unsigned char* m) {
    return Mdl(m)->boneTable;
}

inline unsigned char* BoneBytes(BoneRecord* bones, std::size_t index) {
    return reinterpret_cast<unsigned char*>(&bones[index]);
}

inline MorphRecord*& Morphs(unsigned char* m) {
    return Mdl(m)->morphs;
}

inline BoneKey*& BoneKeys(unsigned char* m) {
    return Mdl(m)->boneKeys;
}

inline MorphKey*& MorphKeys(unsigned char* m) {
    return Mdl(m)->morphKeys;
}

inline std::uint32_t*& BoneKeyIndices(unsigned char* m) {
    return Mdl(m)->boneKeyIndices;
}

inline std::uint32_t*& MorphKeyIndices(unsigned char* m) {
    return Mdl(m)->morphKeyIndices;
}

inline unsigned char* MorphBytes(MorphRecord* morphs, std::size_t index) {
    return reinterpret_cast<unsigned char*>(&morphs[index]);
}

inline RigidRecord*& Rigids(unsigned char* m) {
    return Mdl(m)->rigidTable;
}

inline JointRecord*& Joints(unsigned char* m) {
    return Mdl(m)->jointTable;
}

inline unsigned char* RigidBytes(RigidRecord* records, std::size_t index) {
    return reinterpret_cast<unsigned char*>(&records[index]);
}

inline unsigned char* JointBytes(JointRecord* records, std::size_t index) {
    return reinterpret_cast<unsigned char*>(&records[index]);
}

inline DisplayKey*& DisplayKeys(unsigned char* m) {
    return Mdl(m)->displayKeys;
}

inline BoneOrderEntry*& BoneOrder(unsigned char* m) {
    return Mdl(m)->boneOrderTable;
}

inline std::uint32_t& BoneOrderCount(unsigned char* m) {
    return Mdl(m)->boneOrderCount;
}

}  // namespace mikudancestudio::mdl
