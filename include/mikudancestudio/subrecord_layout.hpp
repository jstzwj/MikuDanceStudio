// ===========================================================================
// MikuMikuDance - per-model sub-record types (morph / rigid / joint)
// ===========================================================================
// Layout doctrine: source-level structs whose x86 layout is byte-pinned to
// the x86 original (loader call sites) and whose x64 layout is the
// compiler's natural regrowth - verified against the x64 original binary
// (MikuMikuDance.exe x64, PMX loader sub_1400A9AC0 / PMD loader
// sub_1400B2670, mined 2026-08-31):
//
//   record   x86   x64   evidence
//   morph     136   192   stride 3*64 in UI loops (sub_14003F550),
//                          morph+0x60 byte == panel/type field
//   rigid     172   192   alloc `mov eax, 0C0h; mul rdx` @0x1400B0913,
//                          table @model+0x3568, count @model+0x3578
//   joint     140   152   alloc `imul r8, 98h` @0x1400B1335, rigidA/B
//                          stored @+0x28/+0x2C, table @model+0x3570
//
// Raw PMD vertex records contain no pointers, so their stride is unchanged
// on both architectures. PMX and PMD share the 2292-byte material work
// record; its recovered fields are defined below.
// =========================================================================//
#pragma once

#include <cstddef>
#include <cstdint>
#include "model_geometry_vertices.hpp"

#ifndef MIKUDANCESTUDIO_X64
#if defined(_M_X64) || defined(__x86_64__)
#define MIKUDANCESTUDIO_X64 1
#else
#define MIKUDANCESTUDIO_X64 0
#endif
#endif

class btRigidBody;

namespace mikudancestudio::mdl {

// PMD vertex and vertex-morph records contain no pointers. Their ABI is
// therefore unchanged between the original x86 program and its x64 twin.
struct PmdVertex {
    float position[3];
    float normal[3];
    float uv[2];
    std::int16_t bone[2];
    std::int8_t weightPercent;
    std::uint8_t edgeDisabled;
};
static_assert(sizeof(PmdVertex) == 40, "PMD vertex ABI");

struct PmdVertexMorphEntry {
    std::uint32_t vertexIndex;
    float offset[3];
};
static_assert(sizeof(PmdVertexMorphEntry) == 16, "PMD vertex morph ABI");

// PMX normalises variable-width file indices to int32_t in its in-memory
// morph tables.  These records are allocation units used by the loader, not
// direct casts of the variable-width on-disk entries.
struct PmxGroupMorphEntry {
    std::int32_t morphIndex;
    float weight;
};
static_assert(sizeof(PmxGroupMorphEntry) == 8, "PMX group morph ABI");

struct PmxBoneMorphEntry {
    std::int32_t boneIndex;
    float translation[3];
    float rotation[4];
};
static_assert(sizeof(PmxBoneMorphEntry) == 32, "PMX bone morph ABI");

// Per-model accumulation record used while applying PMX bone morphs.
struct BoneMorphOffsetRecord {
    std::int32_t boneIndex;
    float translation[3];
    float rotation[4];
};
static_assert(sizeof(BoneMorphOffsetRecord) == 32, "bone morph offset ABI");

struct PmxUvMorphEntry {
    std::int32_t vertexIndex;
    float offset[4];
};
static_assert(sizeof(PmxUvMorphEntry) == 20, "PMX UV morph ABI");

// The original expands PMX's contiguous 28 material-morph floats into this
// renderer layout, leaving two words zero at the original's internal gaps.
struct MaterialMorphChannels {
    float diffuse[4];
    float specular[3];
    float reserved0;
    float specularPower;
    float ambient[3];
    float reserved1;
    float edgeColor[4];
    float edgeSize;
    float textureTint[4];
    float sphereTint[4];
    float toonTint[4];
};
static_assert(sizeof(MaterialMorphChannels) == 120, "material morph channel ABI");
static_assert(offsetof(MaterialMorphChannels, diffuse) == 0, "material morph diffuse ABI");
static_assert(offsetof(MaterialMorphChannels, specular) == 16, "material morph specular ABI");
static_assert(offsetof(MaterialMorphChannels, reserved0) == 28,
              "material morph first internal gap ABI");
static_assert(offsetof(MaterialMorphChannels, specularPower) == 32,
              "material morph specular power ABI");
static_assert(offsetof(MaterialMorphChannels, ambient) == 36, "material morph ambient ABI");
static_assert(offsetof(MaterialMorphChannels, reserved1) == 48,
              "material morph second internal gap ABI");
static_assert(offsetof(MaterialMorphChannels, edgeColor) == 52, "material morph edge color ABI");
static_assert(offsetof(MaterialMorphChannels, edgeSize) == 68, "material morph edge size ABI");
static_assert(offsetof(MaterialMorphChannels, textureTint) == 72,
              "material morph texture tint ABI");
static_assert(offsetof(MaterialMorphChannels, sphereTint) == 88, "material morph sphere tint ABI");
static_assert(offsetof(MaterialMorphChannels, toonTint) == 104, "material morph toon tint ABI");

struct PmxMaterialMorphEntry {
    std::int32_t materialIndex;
    std::uint8_t operation;
    unsigned char operationPadding[3];
    MaterialMorphChannels channels;
};
static_assert(sizeof(PmxMaterialMorphEntry) == 128, "PMX material morph ABI");
static_assert(offsetof(PmxMaterialMorphEntry, channels) == 8, "PmxMaterialMorphEntry.channels ABI");

// Load-time base data and the additive/multiplicative material-morph pools
// use the same channel layout but do not carry a material index or operation.
struct MaterialMorphPool {
    unsigned char prefix[8];
    MaterialMorphChannels channels;
};
static_assert(sizeof(MaterialMorphPool) == 128, "material morph pool ABI");
static_assert(offsetof(MaterialMorphPool, channels) == 8, "material morph pool channel base ABI");

enum class PmxWeightType : std::uint8_t {
    bdef1 = 0,
    bdef2 = 1,
    bdef4 = 2,
    sdef = 3,
    qdef = 4,
};

// PMX uses a loader-local working vertex rather than retaining its variable
// length on-disk form. Additional UVs are stored component first:
// additionalUvByComponent[c][uv] is written as dst[8 + 4 * uv + c].
// PMX stores SDEF as C, R0, R1.  The loader keeps C unchanged and replaces
// R0/R1 with offsets from the weighted SDEF pivot, exactly as consumed by the
// x64 vertex worker.  This makes the persistent representation explicit
// without exposing its byte layout to skinning code.
struct PmxSdefData {
    float center[3];
    float r0Offset[3];
    float r1Offset[3];
};
static_assert(sizeof(PmxSdefData) == 36, "PMX SDEF working data ABI");

struct PmxVertex {
    float position[3];
    float normal[3];
    float uv[2];
    float additionalUvByComponent[4][4];
    PmxWeightType weightType;
    unsigned char weightTypePadding[3];
    std::int32_t bone[4];
    float weight[4];
    PmxSdefData sdef;
    float edgeScale;
    // Set after PMX vertex input is complete. The x64 common skinning tail
    // tests this marker to choose the PMX material-driven outline expansion.
    std::uint8_t hasPmxEdgeData;
    unsigned char workPadding[3];
    // These supply Z/W in a UV-morph base record. Their producer is pending
    // recovery, so the name deliberately makes no stronger claim.
    float uvMorphBaseZW[2];
    std::int32_t materialIndex;
};
static_assert(sizeof(PmxVertex) == 188, "PMX working vertex ABI");
static_assert(offsetof(PmxVertex, weightType) == 96, "PMX weight type");
static_assert(offsetof(PmxVertex, bone) == 100, "PMX bones");
static_assert(offsetof(PmxVertex, weight) == 116, "PMX weights");
static_assert(offsetof(PmxVertex, sdef) == 132, "PMX SDEF working data");
static_assert(offsetof(PmxVertex, sdef.center) == 132, "PMX SDEF center");
static_assert(offsetof(PmxVertex, sdef.r0Offset) == 144, "PMX SDEF R0 offset");
static_assert(offsetof(PmxVertex, sdef.r1Offset) == 156, "PMX SDEF R1 offset");
static_assert(offsetof(PmxVertex, edgeScale) == 168, "PMX edge scale");
static_assert(offsetof(PmxVertex, hasPmxEdgeData) == 172, "PMX edge data marker");
static_assert(offsetof(PmxVertex, uvMorphBaseZW) == 176, "PMX UV base ZW");
static_assert(offsetof(PmxVertex, materialIndex) == 184, "PMX material");

// GPU-facing vertex records. The main record has zero to four extra UV
// vectors appended after base; the x64 worker selection fixes its stride.
using SkinnedVertexBase = render::ModelVertex;
static_assert(sizeof(SkinnedVertexBase) == 32, "base GPU vertex ABI");

// The five PMX workers select one of these concrete records by additional UV
// count. Source storage is component-first for loader convenience, whereas
// the GPU record keeps each additional UV as its contiguous float4.
template <std::size_t AdditionalUvCount>
using SkinnedVertex = render::ExtendedModelVertex<AdditionalUvCount>;
static_assert(sizeof(SkinnedVertex<1>) == 48, "one additional UV GPU ABI");
static_assert(sizeof(SkinnedVertex<2>) == 64, "two additional UV GPU ABI");
static_assert(sizeof(SkinnedVertex<3>) == 80, "three additional UV GPU ABI");
static_assert(sizeof(SkinnedVertex<4>) == 96, "four additional UV GPU ABI");

using EdgeVertex = render::ModelEdgeVertex;
static_assert(sizeof(EdgeVertex) == 16, "edge GPU vertex ABI");

// Renderer material working record. It is deliberately not a PMX on-disk
// material: the loader mirrors and rearranges file data and caches texture
// paths here. The padding preserves the original 2292-byte allocation ABI.
struct ModelMaterialRecord {
    float diffuse[4];
    float diffuseMirror[4];
    float specular[3];
    unsigned char gap0[4];
    float ambient[3];
    unsigned char gap1[4];
    float specularPower;
    wchar_t texturePath[280];
    wchar_t spherePath[280];
    std::int32_t faceVertexCount;
    std::uint8_t toonReference;
    std::uint8_t doubleSided;
    unsigned char gap2[2];
    float edgeColor[4];
    float edgeSize;
    wchar_t toonPath[512];
    std::uint8_t flags;
    std::uint8_t sphereMode;
    unsigned char tail[50];
};
static_assert(sizeof(ModelMaterialRecord) == 2292, "material record ABI");
static_assert(offsetof(ModelMaterialRecord, texturePath) == 68,
              "ModelMaterialRecord.texturePath ABI");
static_assert(offsetof(ModelMaterialRecord, spherePath) == 628,
              "ModelMaterialRecord.spherePath ABI");
static_assert(offsetof(ModelMaterialRecord, faceVertexCount) == 1188,
              "ModelMaterialRecord.faceVertexCount ABI");
static_assert(offsetof(ModelMaterialRecord, edgeColor) == 1196,
              "ModelMaterialRecord.edgeColor ABI");
static_assert(offsetof(ModelMaterialRecord, toonPath) == 1216, "ModelMaterialRecord.toonPath ABI");
static_assert(offsetof(ModelMaterialRecord, flags) == 2240, "ModelMaterialRecord.flags ABI");

// ---- morph records --------------------------------------------------------
enum class MorphPanel : std::uint8_t {
    system = 0,
    eyebrow = 1,
    eye = 2,
    mouth = 3,
    other = 4,
};

struct MorphRecord {
    char name[20];                          // 0    SJIS
    char nameEn[20];                        // 20
    wchar_t* jpText;                        // 40   read-text buffer (PMX)
    wchar_t* enText;                        // 44
    float value;                            // 48   current morph value (keyframes)
    std::int32_t offsetCount;               // 52   generic count / cleared per type
    std::int32_t uvCounts[5];               // 56   uv1..uv5 entry counts
    std::int32_t boneCount;                 // 76
    std::int32_t groupCount;                // 80
    std::int32_t materialCount;             // 84
    MorphPanel panel;                       // 88
    std::uint8_t type;                      // 89   0 group 1 vertex 2 bone 3-7 uv 8 mat
    PmdVertexMorphEntry* vertexEntries;     // 92   16-byte entries
    PmxBoneMorphEntry* boneEntries;         // 96   32-byte entries
    PmxGroupMorphEntry* groupEntries;       // 100  8-byte entries
    PmxUvMorphEntry* uvEntries[5];          // 104  20-byte entries per family
    PmxMaterialMorphEntry* materialEntries; // 124  128-byte entries
    unsigned char* impulseEntries;          // 128  188-byte entries (type 9)
    unsigned char* impulse2Entries;         // 132  (type 10, unused by v9.32)
};

// ---- rigid body records ---------------------------------------------------
struct RigidRecord {
    char name[20];          // 0    SJIS
    wchar_t* jpText;        // 20   read-text buffer (PMX)
    wchar_t* enText;        // 24
    std::int32_t boneIndex; // 28
    std::uint8_t group;     // 32
    std::uint8_t padGroup;
    std::uint16_t noCollapse; // 34
    std::uint8_t shape;       // 36   0 sphere 1 box 2 capsule
    unsigned char padShape[3];
    float size[3];              // 40
    float position[3];          // 52
    float rotation[3];          // 64
    float mass;                 // 76
    std::uint8_t mode;          // 80   0 static 1 dynamic 2 dynamic-bone
    std::uint8_t kinematicFlag; // 81   set when mode == 2
    std::uint8_t staticFlag;    // 82   set when mode == 0
    unsigned char padMode;
    std::intptr_t bodyId; // 84   Bullet debug ID; editor scratch copies reuse this as a combo index
    float linearDamping;  // 88
    float angularDamping; // 92
    float restitution;    // 96
    float friction;       // 100
    btRigidBody* body;    // 104  owned Bullet body
    float invTransform[16]; // 108  0x40 inverse bone-relative matrix
};

// ---- joint records --------------------------------------------------------
struct JointRecord {
    char name[20];       // 0    SJIS
    wchar_t* jpText;     // 20   read-text buffer (PMX)
    wchar_t* enText;     // 24
    std::int32_t rigidA; // 28
    std::int32_t rigidB; // 32
    float position[3];   // 36
    float rotation[3];   // 48
    // The original stores the four limit vectors in a shuffled order
    // (see the PMX loader reads: 72,76,80 then 60,64,68 then 96,100,104
    // then 84,88,92 on x86).
    float limits[12];        // 60
    float springs[6];        // 108
    std::int32_t constraint; // 132  CreatePhysJoint UID (int, NOT a
                             //      pointer - x64 stores edx @+0x90)
    float radiusBound;       // 136  distA + distB + limit radius
};

} // namespace mikudancestudio::mdl

// ---- x86 pins (byte-exact against the ported loader call sites) -----------
#if !MIKUDANCESTUDIO_X64
static_assert(sizeof(mikudancestudio::mdl::MorphRecord) == 136, "morph x86 size");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, jpText) == 40,
              "MorphRecord.jpText x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, value) == 48,
              "MorphRecord.value x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, offsetCount) == 52,
              "MorphRecord.offsetCount x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, uvCounts) == 56,
              "MorphRecord.uvCounts x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, boneCount) == 76,
              "MorphRecord.boneCount x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, groupCount) == 80,
              "MorphRecord.groupCount x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, materialCount) == 84,
              "MorphRecord.materialCount x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, panel) == 88,
              "MorphRecord.panel x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, type) == 89, "MorphRecord.type x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, vertexEntries) == 92,
              "MorphRecord.vertexEntries x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, boneEntries) == 96,
              "MorphRecord.boneEntries x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, groupEntries) == 100,
              "MorphRecord.groupEntries x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, uvEntries) == 104,
              "MorphRecord.uvEntries x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, materialEntries) == 124,
              "MorphRecord.materialEntries x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, impulseEntries) == 128,
              "MorphRecord.impulseEntries x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, impulse2Entries) == 132,
              "MorphRecord.impulse2Entries x86 ABI");

static_assert(sizeof(mikudancestudio::mdl::RigidRecord) == 172, "rigid x86 size");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, jpText) == 20,
              "RigidRecord.jpText x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, enText) == 24,
              "RigidRecord.enText x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, boneIndex) == 28,
              "RigidRecord.boneIndex x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, group) == 32,
              "RigidRecord.group x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, noCollapse) == 34,
              "RigidRecord.noCollapse x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, shape) == 36,
              "RigidRecord.shape x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, size) == 40, "RigidRecord.size x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, position) == 52,
              "RigidRecord.position x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, rotation) == 64,
              "RigidRecord.rotation x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, mass) == 76, "RigidRecord.mass x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, mode) == 80, "RigidRecord.mode x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, bodyId) == 84,
              "RigidRecord.bodyId x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, linearDamping) == 88,
              "RigidRecord.linearDamping x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, body) == 104, "RigidRecord.body x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, invTransform) == 108,
              "RigidRecord.invTransform x86 ABI");

static_assert(sizeof(mikudancestudio::mdl::JointRecord) == 140, "joint x86 size");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, jpText) == 20,
              "JointRecord.jpText x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, enText) == 24,
              "JointRecord.enText x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, rigidA) == 28,
              "JointRecord.rigidA x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, rigidB) == 32,
              "JointRecord.rigidB x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, position) == 36,
              "JointRecord.position x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, rotation) == 48,
              "JointRecord.rotation x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, limits) == 60,
              "JointRecord.limits x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, springs) == 108,
              "JointRecord.springs x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, constraint) == 132,
              "JointRecord.constraint x86 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, radiusBound) == 136,
              "JointRecord.radiusBound x86 ABI");
#else
// ---- x64 anchors (natural regrowth, mined from the original binary) -------
static_assert(sizeof(mikudancestudio::mdl::MorphRecord) == 192, "morph x64 size");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, panel) == 0x60,
              "MorphRecord.panel x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, type) == 0x61,
              "MorphRecord.type x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::MorphRecord, vertexEntries) == 0x68,
              "MorphRecord.vertexEntries x64 ABI");

static_assert(sizeof(mikudancestudio::mdl::RigidRecord) == 192, "rigid x64 size 0xC0");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, jpText) == 24,
              "RigidRecord.jpText x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, enText) == 32,
              "RigidRecord.enText x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, size) == 52, "RigidRecord.size x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, rotation) == 0x4C,
              "RigidRecord.rotation x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, bodyId) == 96,
              "RigidRecord.bodyId x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, body) == 120, "RigidRecord.body x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::RigidRecord, invTransform) == 128,
              "RigidRecord.invTransform x64 ABI");

static_assert(sizeof(mikudancestudio::mdl::JointRecord) == 152, "joint x64 size 0x98");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, jpText) == 24,
              "JointRecord.jpText x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, enText) == 32,
              "JointRecord.enText x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, rigidA) == 0x28,
              "JointRecord.rigidA x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, rigidB) == 0x2C,
              "JointRecord.rigidB x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, limits) == 0x48,
              "JointRecord.limits x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, springs) == 0x78,
              "JointRecord.springs x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, constraint) == 0x90,
              "JointRecord.constraint x64 ABI");
static_assert(offsetof(mikudancestudio::mdl::JointRecord, radiusBound) == 0x94,
              "JointRecord.radiusBound x64 ABI");
#endif
