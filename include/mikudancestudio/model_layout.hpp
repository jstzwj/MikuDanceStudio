// ===========================================================================
// MikuDanceStudio - the per-model runtime record
// ===========================================================================
// x86 layout pinned byte-exact (0x4CCF4); x64 architecture-specific
// storage is pinned against independently binary-mined anchors.
// Placeholder names (f<off>/v<off>/pad*) are promoted to real names as
// semantics are recovered - never guessed.
// ===========================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "mikudancestudio/raw_pad.hpp"
#include "mikudancestudio/undo_layout.hpp"
#include "mikudancestudio/skeleton_tracking.hpp"

namespace mikudancestudio { class PhysicsScene; }

namespace mikudancestudio::mdl {

struct BoneRecord;
struct MorphRecord;
struct BoneKey;
struct MorphKey;
struct DisplayKey;
struct FrameGroup;
struct RigidRecord;
struct JointRecord;
struct IkChain;
struct PmdVertex;
struct PmdVertexMorphEntry;
struct PmxVertex;
struct PmxUvMorphEntry;
struct BoneMorphOffsetRecord;
struct MaterialMorphPool;

struct PmxUvMorphCounts { std::int32_t byFamily[5]; };
struct PmxUvMorphTables { PmxUvMorphEntry* byFamily[5]; };
struct PmxMaterialMorphPools {
    MaterialMorphPool* base;
    MaterialMorphPool* additive;
    MaterialMorphPool* multiplicative;
};

using mikudancestudio::RawPad;

struct ModelRecord {
    void* hwnd;  // 0  (dialog owner (0x4BF42B this[0]))
    std::uint32_t vertexCount;  // 4  (this[1])
    void* vertexBuffer;  // 8  (FVF 0x112, 32 B/vtx)
    void* vertexBuffer2;  // 12  (FVF 0x042, 16 B/vtx)
    void* indexBuffer;  // 16
    std::uint32_t indexCount;  // 20  (this[5])
    void* indices;  // 24  (WORD*)
    std::uint32_t materialCount;  // 28  (this[7])
    void* materials;  // 32  (2292-byte material records)
    std::uint32_t* boneKeyCursors;  // 36  (one cursor per bone)
    unsigned char* boneTrackActive;  // 40  (one flag per bone)
    std::uint32_t* morphKeyCursors;  // 44  (one cursor per morph)
    unsigned char* morphTrackActive;  // 48  (one flag per morph)
    std::uint32_t displayKeyCursor;  // 52
    unsigned char displayTrackActive;  // 56
    RawPad<3> gap0;  // 57..60 (unrecovered)
    ::mikudancestudio::PhysicsScene* scenePtr;  // 60  (shared Bullet/D3D physics scene)
    StandardSkeletonPose standardPose;
    SkeletonHistory skeletonHistory;
    std::uint8_t poseTraceRecording;
    void* poseTraceBuffer;
    std::uint8_t pmxTextEncoding;        // 8624  (0 UTF-16; x64 PMX loader)
    std::uint8_t pmxAdditionalUvCount;   // 8625
    std::uint8_t pmxVertexIndexSize;     // 8626
    std::uint8_t pmxTextureIndexSize;    // 8627
    std::uint8_t pmxBoneIndexSize;       // 8628
    std::uint8_t pmxMorphIndexSize;      // 8629
    std::uint8_t pmxMaterialIndexSize;   // 8630
    std::uint8_t pmxRigidIndexSize;      // 8631
    // Unrecovered PMX bookkeeping before the flattened morph counts.
#if defined(_M_X64)
    RawPad<60> pmxReserved;
#else
    RawPad<52> pmxReserved;
#endif
    std::uint32_t morph0Count;  // 8684  (model_skinning / dialog_helpers)
    PmxUvMorphCounts uvMorphCounts;
    std::int32_t boneMorphCount;  // 8708  (pmx_load boneMorphTotal store)
    RawPad<12> gap6;  // 8712..8724 (unrecovered)
    PmdVertexMorphEntry* morph0Table;  // 8724  (16-byte PMD morph entries)
    BoneMorphOffsetRecord* boneMorphTable;
    // The reference destructor releases this slot, but no producer or
    // element semantics have been established. Keep its ownership explicit.
    void* reservedMorphTable;
    PmxUvMorphTables uvMorphTables;
    PmxMaterialMorphPools materialMorphPools;
    // Free keyframe-slot scan cursor.  x64: +0x22B8 = 8888, shared by the
    // bone-key pool (0x7FF7CB4E9958, 0x7FF7CB4EA46F), the morph-key pool
    // (0x7FF7CB48E416) and the display-key pool (0x7FF7CB48E5EA); the cursor
    // only ever advances.  8836 (0x2274) has zero x64 references.
    std::int32_t searchCursor;  // x86 8768; x64 8888 (0x22B8)
    std::int32_t maxBoneLayer;  // x86 8772; x64 8892 (maximum PMX bone transform layer)
    char name[50];  // Runtime capacity; PMD stores only 20 bytes on disk.
    char nameEn[50];
    char comment[256];  // 8876
    char commentEn[256];  // 9132
#if defined(_M_X64)
    // x64 PMX loader sub_1400A9AC0 stores its four text pointers at
    // +0x2528..+0x2540; the four-byte gap aligns the first qword.
    RawPad<4> pmxTextAlignment;
#endif
    wchar_t* pmxTextBuffers[4];  // JP name, EN name, JP comment, EN comment
    wchar_t path[256];  // 9404  (resolved model path 0x24BC)
    BoneRecord* boneTable;  // 9916  (604/624-byte bone records; x64 anchor 10056 verified)
    IkChain* ikChains;  // 9920  (24/32-byte chain records)
    MorphRecord* morphs;  // 9924  (136/192-byte morph records; x64 0x2758)
    PmdVertex* rawVertices;  // 9928  (40-byte PMD vertex records; x64 same)
    PmxVertex* pmxVertices;  // 9932  (188-byte PMX working vertices; x64 0x2768)
    void* groupNames;  // 9936  (101-byte facial group names; x64 0x2770)
    unsigned char groupCount;  // 9940  (pmd_load grpCountPtr)
    RawPad<3> gap11;  // 9941..9944 (unrecovered)
    void* rbGroups;  // 9944  (46-byte rigid group records)
    FrameGroup* displayFrames;  // 9948  (46-byte display frame records; x64 0x2788)
    BoneKey* boneKeys;  // 9952  (60-byte bone key records; x64 0x2790)
    MorphKey* morphKeys;  // 9956  (20-byte morph key records; x64 0x2798)
    DisplayKey* displayKeys;  // 9960  (28/40-byte display key records; x64 0x27A0)
    UndoRing undoRings[2];  // 9964  (two consecutive 30-slot rings; x64 slots grow 28 -> 40 bytes)
    unsigned char comboSelIndex;  // 11644  (0x460430)
    unsigned char comboSelIndex2;  // 11645
    RawPad<2> gap12;  // 11646..11648 (unrecovered)
    std::uint32_t morphCount;  // 11648  (pmd/pmx loaders _read(fh, m+11648, 2/4); x64 twin 0x310C)
    std::uint32_t boneCount;  // 11652  (x64 twin 12560)
    std::uint32_t ikChainCount;  // 11656
    unsigned char displayState;  // 11660  (from app)
    unsigned char loadComplete;  // 11661
    RawPad<2> gap13;  // 11662..11664 (unrecovered)
    std::int32_t selectedBone;  // 11664  (current bone index; -1 means none)
    unsigned char* boneSelection;  // 11668  (one byte per bone)
    unsigned char* bonePhysicsState;  // 11672  (one byte per bone)
    std::int32_t selectedMorphs[4];  // 11676  (X/Y/Z/other morph selectors)
    unsigned char facialFrameCount;  // 11692  (display/facial frame groups)
    RawPad<3> gap14;  // 11693..11696 (unrecovered)
    std::uint32_t rigidBodyCount;  // 11696  (pmd/pmx/physics)
    // Post-load lookup tables, indexed respectively by bone and morph.
    // The x64 PMX loader allocates them at +0x3148 and +0x3150.
    std::uint32_t* boneKeyIndices;  // 11700 (x64 0x3148)
    std::uint32_t* morphKeyIndices;  // 11704 (x64 0x3150)
#if defined(_M_X64)
    // The two pointer fields above grow by eight bytes on x64; preserve the
    // directly recovered offsets of the following timeline fields.
#endif
    // Bone-list line bookkeeping written by PostLanguageSweep (0x42F1E0) and
    // read by the panel paint (0x414610) and the name-column click path
    // (0x446A70): the frame-1 head line, the 200 per-line type bytes (1 =
    // bone row / 2 = face row / rigid type otherwise) and the 200 per-line
    // records (bone index, rigid index or -1-morph; -999 = no record).
    // x86 11708/11712/11912; x64 0x3158/0x315C/0x3224 (E-build click
    // handler reads the type bytes at [model+0x315C] and the records at
    // [model+0x3224], 0x7FF7CB45A2CE / 0x7FF7CB459F00).
    std::int32_t boneListSelLine;         // 11708 x86 / 0x3158 x64
    unsigned char boneListRowType[200];   // 11712 x86 / 0x315C x64
    std::int32_t boneListRowRecord[200];  // 11912 x86 / 0x3224 x64
    std::int32_t boneListRows;  // 12712  (scrollbar 0x47C0A0)
    std::int32_t boneListPos;  // 12716
    std::uint32_t maxFrame;  // 12720  (model timeline upper bound)
    std::uint32_t undoState[2];  // 12724  (bone_edit_undo)
    unsigned char undoDirty;  // 12732  (undo snapshot pending)
    unsigned char redoDirty;  // 12733  (redo snapshot pending)
    unsigned char postLoadFlag2;  // 12734
    RawPad<1> gap16;  // 12735..12736 (unrecovered)
    float edgeScale;  // 12736  (model_skinning/render)
    unsigned char physicsFlags;  // 12740  (pmd/pmx/post_load)
    RawPad<3> gap17;  // 12741..12744 (unrecovered)
    RigidRecord* rigidTable;  // 12744  (172/192-byte records; x64 0x3568)
    JointRecord* jointTable;  // 12748  (140/152-byte records; x64 0x3570)
    std::uint32_t rigidCount;  // 12752  (x64 0x3578)
    std::uint32_t jointCount;  // 12756  (x64 0x357C)
    // Model-file directory and the ten PMD toon file names share the range
    // immediately after the physics tables.  Together they occupy exactly
    // the formerly opaque 1512-byte region on both ABIs.
    wchar_t modelDirectory[256];       // 12760..13272
    char pmdToonFileNames[10][100];    // 13272..14272
    unsigned char toonFlag;  // 14272  (post_load_init)
    RawPad<3> gap19;  // 14273..14276 (unrecovered)
    std::uint32_t toonShared;  // 14276  (model_renderers)
    SkeletonJoints currentJoints;
    RawPad<12> skeletonReserved;
    float lightDir[3];  // 14568
    float legIkXOffset;  // 14580 (height-normalized left/right leg IK X correction)
    std::int32_t matMisc;  // 14584
    // Set once the model owns a display/IK key track.  The original uses
    // this as the cheap gate before walking displayKeys; it is distinct from
    // the per-frame displayTrackActive cursor state near the record head.
    unsigned char displayKeyframesPresent;  // 14588
    // Per-model copy of the app-wide OpenNI runtime version.  Menu command
    // 292 (auto frame record) pushes the app byte (x86 0xA03EA / x64
    // 0xA137E) into every loaded model; the bone/physics probes read it
    // back as a capability gate (>= 14 / >= 15).  x64 twin at model+0x3CA5
    // (0x7FF7CB47033B..342: movzx eax,[rbx+0A137Eh]; mov [rcx+3CA5h],al).
    unsigned char openniVersion;  // 14589
    unsigned char physicsMode;  // 14590  (SetMenuItemInfo gate)
    RawPad<1> gap25;  // 14591..14592 (unrecovered)
    std::uint32_t displayRootBone;  // 14592 (x64 0x3CA8; first PMX display-frame bone)
    // Undo snapshot deduplication.  The x64 E build doubles the array to
    // 600000 bytes at model+0x3CAC (memset 0x927C0, x64 0x7FF7CB4E819F);
    // the four alignment bytes before boneOrderTable (x64 0x96470) come
    // from pointer alignment, so the tail anchors below still hold.
#if defined(_M_X64)
    unsigned char keyVisitMap[600000];  // 15532 (0x3CAC)
#else
    unsigned char keyVisitMap[300000];  // 14596
#endif
    void* boneOrderTable;  // 314596  (20-byte slots; x64 0x96470)
    std::uint32_t boneOrderCount;  // 314600  (x64 0x96478)
    std::int32_t centerBone;  // 314604  (physics_frame 0x4B3460)
    std::int32_t frameRegistrationSelection;  // 314608  (combo 434 selection)
};

#if !defined(_M_X64)
static_assert(offsetof(ModelRecord, hwnd) == 0,
              "hwnd x86");
static_assert(offsetof(ModelRecord, vertexCount) == 4,
              "vertexCount x86");
static_assert(offsetof(ModelRecord, vertexBuffer) == 8,
              "vertexBuffer x86");
static_assert(offsetof(ModelRecord, vertexBuffer2) == 12,
              "vertexBuffer2 x86");
static_assert(offsetof(ModelRecord, indexBuffer) == 16,
              "indexBuffer x86");
static_assert(offsetof(ModelRecord, indexCount) == 20,
              "indexCount x86");
static_assert(offsetof(ModelRecord, indices) == 24,
              "indices x86");
static_assert(offsetof(ModelRecord, materialCount) == 28,
              "materialCount x86");
static_assert(offsetof(ModelRecord, materials) == 32,
              "materials x86");
static_assert(offsetof(ModelRecord, boneKeyCursors) == 36,
              "boneKeyCursors x86");
static_assert(offsetof(ModelRecord, boneTrackActive) == 40,
              "boneTrackActive x86");
static_assert(offsetof(ModelRecord, morphKeyCursors) == 44,
              "morphKeyCursors x86");
static_assert(offsetof(ModelRecord, morphTrackActive) == 48,
              "morphTrackActive x86");
static_assert(offsetof(ModelRecord, displayKeyCursor) == 52,
              "displayKeyCursor x86");
static_assert(offsetof(ModelRecord, displayTrackActive) == 56,
              "displayTrackActive x86");
static_assert(offsetof(ModelRecord, scenePtr) == 60,
              "scenePtr x86");
static_assert(offsetof(ModelRecord, pmxTextEncoding) == 8624,
              "pmxTextEncoding x86");
static_assert(offsetof(ModelRecord, morph0Count) == 8684,
              "morph0Count x86");
static_assert(offsetof(ModelRecord, boneMorphCount) == 8708,
              "boneMorphCount x86");
static_assert(offsetof(ModelRecord, morph0Table) == 8724,
              "morph0Table x86");
static_assert(offsetof(ModelRecord, boneMorphTable) == 8728,
              "boneMorphTable x86");
static_assert(offsetof(ModelRecord, maxBoneLayer) == 8772,
              "maxBoneLayer x86");
static_assert(offsetof(ModelRecord, name) == 8776,
              "name x86");
static_assert(offsetof(ModelRecord, nameEn) == 8826,
              "nameEn x86");
static_assert(offsetof(ModelRecord, searchCursor) == 8768,
              "searchCursor x86");
static_assert(offsetof(ModelRecord, comment) == 8876,
              "comment x86");
static_assert(offsetof(ModelRecord, commentEn) == 9132,
              "commentEn x86");
static_assert(offsetof(ModelRecord, pmxTextBuffers) == 9388,
              "pmxTextBuffers x86");
static_assert(offsetof(ModelRecord, path) == 9404,
              "path x86");
static_assert(offsetof(ModelRecord, boneTable) == 9916,
              "boneTable x86");
static_assert(offsetof(ModelRecord, ikChains) == 9920,
              "ikChains x86");
static_assert(offsetof(ModelRecord, morphs) == 9924,
              "morphs x86");
static_assert(offsetof(ModelRecord, rawVertices) == 9928,
              "rawVertices x86");
static_assert(offsetof(ModelRecord, pmxVertices) == 9932,
              "pmxVertices x86");
static_assert(offsetof(ModelRecord, groupNames) == 9936,
              "groupNames x86");
static_assert(offsetof(ModelRecord, groupCount) == 9940,
              "groupCount x86");
static_assert(offsetof(ModelRecord, rbGroups) == 9944,
              "rbGroups x86");
static_assert(offsetof(ModelRecord, displayFrames) == 9948,
              "displayFrames x86");
static_assert(offsetof(ModelRecord, boneKeys) == 9952,
              "boneKeys x86");
static_assert(offsetof(ModelRecord, morphKeys) == 9956,
              "morphKeys x86");
static_assert(offsetof(ModelRecord, displayKeys) == 9960,
              "displayKeys x86");
static_assert(offsetof(ModelRecord, undoRings) == 9964,
              "undoRings x86");
static_assert(offsetof(ModelRecord, comboSelIndex) == 11644,
              "comboSelIndex x86");
static_assert(offsetof(ModelRecord, comboSelIndex2) == 11645,
              "comboSelIndex2 x86");
static_assert(offsetof(ModelRecord, morphCount) == 11648,
              "morphCount x86");
static_assert(offsetof(ModelRecord, boneCount) == 11652,
              "boneCount x86");
static_assert(offsetof(ModelRecord, ikChainCount) == 11656,
              "ikChainCount x86");
static_assert(offsetof(ModelRecord, displayState) == 11660,
              "displayState x86");
static_assert(offsetof(ModelRecord, loadComplete) == 11661,
              "loadComplete x86");
static_assert(offsetof(ModelRecord, selectedBone) == 11664,
              "selectedBone x86");
static_assert(offsetof(ModelRecord, boneSelection) == 11668,
              "boneSelection x86");
static_assert(offsetof(ModelRecord, bonePhysicsState) == 11672,
              "bonePhysicsState x86");
static_assert(offsetof(ModelRecord, selectedMorphs) == 11676,
              "selectedMorphs x86");
static_assert(offsetof(ModelRecord, facialFrameCount) == 11692,
              "facialFrameCount x86");
static_assert(offsetof(ModelRecord, rigidBodyCount) == 11696,
              "rigidBodyCount x86");
static_assert(offsetof(ModelRecord, boneKeyIndices) == 11700,
              "boneKeyIndices x86");
static_assert(offsetof(ModelRecord, morphKeyIndices) == 11704,
              "morphKeyIndices x86");
static_assert(offsetof(ModelRecord, boneListSelLine) == 11708,
              "boneListSelLine x86");
static_assert(offsetof(ModelRecord, boneListRowType) == 11712,
              "boneListRowType x86");
static_assert(offsetof(ModelRecord, boneListRowRecord) == 11912,
              "boneListRowRecord x86");
static_assert(offsetof(ModelRecord, boneListRows) == 12712,
              "boneListRows x86");
static_assert(offsetof(ModelRecord, boneListPos) == 12716,
              "boneListPos x86");
static_assert(offsetof(ModelRecord, maxFrame) == 12720,
              "maxFrame x86");
static_assert(offsetof(ModelRecord, undoState) == 12724,
              "undoState x86");
static_assert(offsetof(ModelRecord, undoDirty) == 12732,
              "undoDirty x86");
static_assert(offsetof(ModelRecord, redoDirty) == 12733,
              "redoDirty x86");
static_assert(offsetof(ModelRecord, postLoadFlag2) == 12734,
              "postLoadFlag2 x86");
static_assert(offsetof(ModelRecord, edgeScale) == 12736,
              "edgeScale x86");
static_assert(offsetof(ModelRecord, physicsFlags) == 12740,
              "physicsFlags x86");
static_assert(offsetof(ModelRecord, rigidTable) == 12744,
              "rigidTable x86");
static_assert(offsetof(ModelRecord, jointTable) == 12748,
              "jointTable x86");
static_assert(offsetof(ModelRecord, rigidCount) == 12752,
              "rigidCount x86");
static_assert(offsetof(ModelRecord, jointCount) == 12756,
              "jointCount x86");
static_assert(offsetof(ModelRecord, modelDirectory) == 12760,
              "modelDirectory x86");
static_assert(offsetof(ModelRecord, pmdToonFileNames) == 13272,
              "pmdToonFileNames x86");
static_assert(offsetof(ModelRecord, toonFlag) == 14272,
              "toonFlag x86");
static_assert(offsetof(ModelRecord, toonShared) == 14276,
              "toonShared x86");
static_assert(offsetof(ModelRecord, lightDir) == 14568,
              "lightDir x86");
static_assert(offsetof(ModelRecord, legIkXOffset) == 14580,
              "legIkXOffset x86");
static_assert(offsetof(ModelRecord, matMisc) == 14584,
              "matMisc x86");
static_assert(offsetof(ModelRecord, openniVersion) == 14589,
              "openniVersion x86");
static_assert(offsetof(ModelRecord, physicsMode) == 14590,
              "physicsMode x86");
static_assert(offsetof(ModelRecord, displayRootBone) == 14592,
              "displayRootBone x86");
static_assert(offsetof(ModelRecord, keyVisitMap) == 14596,
              "keyVisitMap x86");
static_assert(offsetof(ModelRecord, boneOrderTable) == 314596,
              "boneOrderTable x86");
static_assert(offsetof(ModelRecord, boneOrderCount) == 314600,
              "boneOrderCount x86");
static_assert(offsetof(ModelRecord, centerBone) == 314604,
              "centerBone x86");
static_assert(offsetof(ModelRecord, frameRegistrationSelection) == 314608,
              "frameRegistrationSelection x86");
static_assert(sizeof(ModelRecord) == 0x4CCF4,
              "model record x86 size");
#else
static_assert(offsetof(ModelRecord, name) == 8896,
              "name x64 (select-dialog JP name 0x22C0)");
static_assert(offsetof(ModelRecord, nameEn) == 8946,
              "nameEn x64");
static_assert(offsetof(ModelRecord, comboSelIndex) == 12552,
              "comboSelIndex x64 (0x3108)");
static_assert(offsetof(ModelRecord, comboSelIndex2) == 12553,
              "comboSelIndex2 x64 (0x3109; display-order byte scans in "
              "physics_frame/playback_catchup and the select dialog)");
static_assert(offsetof(ModelRecord, comment) == 8996,
              "comment x64");
static_assert(offsetof(ModelRecord, commentEn) == 9252,
              "commentEn x64");
static_assert(offsetof(ModelRecord, pmxTextBuffers) == 0x2528,
              "pmxTextBuffers x64");
static_assert(offsetof(ModelRecord, path) == 0x2548,
              "path x64");
static_assert(offsetof(ModelRecord, searchCursor) == 8888,
              "searchCursor x64 (0x22B8; bone pool 0x7FF7CB4E9958/"
              "0x7FF7CB4EA46F, morph pool 0x7FF7CB48E416, display pool "
              "0x7FF7CB48E5EA)");
static_assert(offsetof(ModelRecord, materialCount) == 56,
              "materialCount x64");
static_assert(offsetof(ModelRecord, materials) == 64,
              "materials x64");
static_assert(offsetof(ModelRecord, boneTable) == 10056,
              "boneTable x64");
static_assert(offsetof(ModelRecord, morphs) == 10072,
              "morphs x64");
static_assert(offsetof(ModelRecord, rawVertices) == 10080,
              "rawVertices x64");
static_assert(offsetof(ModelRecord, pmxVertices) == 10088,
              "pmxVertices x64");
static_assert(offsetof(ModelRecord, groupNames) == 10096,
              "groupNames x64");
static_assert(offsetof(ModelRecord, displayFrames) == 10120,
              "displayFrames x64");
static_assert(offsetof(ModelRecord, boneKeys) == 10128,
              "boneKeys x64");
static_assert(offsetof(ModelRecord, morphKeys) == 10136,
              "morphKeys x64");
static_assert(offsetof(ModelRecord, displayKeys) == 10144,
              "displayKeys x64");
static_assert(offsetof(ModelRecord, undoRings) == 10152,
              "undoRings x64");
static_assert(offsetof(ModelRecord, morphCount) == 12556,
              "morphCount x64");
static_assert(offsetof(ModelRecord, boneCount) == 12560,
              "boneCount x64");
static_assert(offsetof(ModelRecord, facialFrameCount) == 12608,
              "facialFrameCount x64");
static_assert(offsetof(ModelRecord, displayRootBone) == 0x3CA8,
              "displayRootBone x64");
static_assert(offsetof(ModelRecord, boneKeyIndices) == 0x3148,
              "boneKeyIndices x64");
static_assert(offsetof(ModelRecord, morphKeyIndices) == 0x3150,
              "morphKeyIndices x64");
static_assert(offsetof(ModelRecord, boneListSelLine) == 0x3158,
              "boneListSelLine x64");
static_assert(offsetof(ModelRecord, boneListRowType) == 0x315C,
              "boneListRowType x64");
static_assert(offsetof(ModelRecord, boneListRowRecord) == 0x3224,
              "boneListRowRecord x64");
static_assert(offsetof(ModelRecord, boneListRows) == 0x3544,
              "boneListRows x64");
static_assert(offsetof(ModelRecord, physicsFlags) == 13664,
              "physicsFlags x64 (0x3560; per-model language-flag sweep "
              "store 0x7FF7CB43A4F5)");
static_assert(offsetof(ModelRecord, rigidTable) == 13672,
              "rigidTable x64");
static_assert(offsetof(ModelRecord, jointTable) == 13680,
              "jointTable x64");
static_assert(offsetof(ModelRecord, rigidCount) == 13688,
              "rigidCount x64");
static_assert(offsetof(ModelRecord, jointCount) == 13692,
              "jointCount x64");
static_assert(offsetof(ModelRecord, modelDirectory) == 13696,
              "modelDirectory x64");
static_assert(offsetof(ModelRecord, pmdToonFileNames) == 14208,
              "pmdToonFileNames x64");
static_assert(offsetof(ModelRecord, undoState) == 13648,
              "undoState x64 (0x3550, ring depth 30, 0x7FF7CB4470A0)");
static_assert(offsetof(ModelRecord, undoDirty) == 13656,
              "undoDirty x64 (0x3558, 0x7FF7CB447074)");
static_assert(offsetof(ModelRecord, redoDirty) == 13657,
              "redoDirty x64 (0x3559, 0x7FF7CB44708A)");
static_assert(offsetof(ModelRecord, selectedBone) == 0x311C,
              "selectedBone x64");
static_assert(offsetof(ModelRecord, selectedMorphs) == 0x3130,
              "selectedMorphs x64");
static_assert(offsetof(ModelRecord, keyVisitMap) == 0x3CAC,
              "keyVisitMap x64");
static_assert(offsetof(ModelRecord, boneOrderTable) == 0x96470,
              "boneOrderTable x64");
static_assert(offsetof(ModelRecord, boneOrderCount) == 0x96478,
              "boneOrderCount x64");
#endif


// Recovered tracking and PMX fields must never alias one another.
#if defined(_M_X64)
static_assert(offsetof(ModelRecord, standardPose) == 120);
static_assert(offsetof(ModelRecord, skeletonHistory) == 392);
static_assert(offsetof(ModelRecord, poseTraceRecording) == 8672);
static_assert(offsetof(ModelRecord, poseTraceBuffer) == 8680);
static_assert(offsetof(ModelRecord, pmxTextEncoding) == 8688);
static_assert(offsetof(ModelRecord, morph0Count) == 8756);
static_assert(offsetof(ModelRecord, uvMorphCounts) == 8760);
static_assert(offsetof(ModelRecord, boneMorphCount) == 8780);
static_assert(offsetof(ModelRecord, morph0Table) == 8800);
static_assert(offsetof(ModelRecord, boneMorphTable) == 8808);
static_assert(offsetof(ModelRecord, reservedMorphTable) == 8816);
static_assert(offsetof(ModelRecord, uvMorphTables) == 8824);
static_assert(offsetof(ModelRecord, materialMorphPools) == 8864);
static_assert(offsetof(ModelRecord, currentJoints) == 15216);
#else
static_assert(offsetof(ModelRecord, standardPose) == 64);
static_assert(offsetof(ModelRecord, skeletonHistory) == 336);
static_assert(offsetof(ModelRecord, poseTraceRecording) == 8616);
static_assert(offsetof(ModelRecord, poseTraceBuffer) == 8620);
static_assert(offsetof(ModelRecord, uvMorphCounts) == 8688);
static_assert(offsetof(ModelRecord, reservedMorphTable) == 8732);
static_assert(offsetof(ModelRecord, uvMorphTables) == 8736);
static_assert(offsetof(ModelRecord, materialMorphPools) == 8756);
static_assert(offsetof(ModelRecord, currentJoints) == 14280);
#endif

}  // namespace mikudancestudio::mdl
