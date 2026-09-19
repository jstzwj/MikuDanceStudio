// ===========================================================================
// MikuDanceStudio - the per-bone runtime record (maintained by hand)
// ===========================================================================
// No checked-in generator reconstructs this record. Change named members
// alongside binary evidence and the architecture-specific assertions below.
// x86 layout pinned byte-exact (604, the port's hardcoded stride);
// x64 layout is the compiler's natural regrowth (624 = 0x270, the
// stride mined from the x64 original), anchored at 11 twin-verified
// offsets.  Placeholder names (f<off>/pad*) are promoted to real
// names as semantics are recovered - never guessed.
// ===========================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "mikudancestudio/raw_pad.hpp"

namespace mikudancestudio::mdl {

using mikudancestudio::RawPad;

// Loader-local PMX IK-link data. This table is converted into the compact
// IkChain link-index list after all PMX bones are read.
struct PmxIkLinkRecord {
    std::int32_t boneIndex;
    std::uint8_t hasLimits;
    unsigned char padding[3];
    float minimum[3];
    float maximum[3];
};
static_assert(sizeof(PmxIkLinkRecord) == 32, "PMX IK link ABI");

// ---------------------------------------------------------------------------
// Bone type byte (BoneRecord::type) and flag word (BoneRecord::flags).
//
// Semantics recovered from three agreeing sources (2026-09):
//   1. this port's VA-anchored behaviour notes (bone_transform.cpp A-E,
//      bone_sort.cpp, key_registrars.cpp, the loader sweeps);
//   2. IDA on the x86 original: the PMX flag->type conversion sites
//      0x4BA033 (flag 0x04 -> 1), 0x4BA127 (0x400 -> 8), 0x4BA5DF
//      (!(0x08) -> 7), 0x4BA740 (IK -> 2), 0x4BA75A (ikTarget -> 6),
//      0x4BA8E0 (IK link -> 4) and the selector-slot test 0x490070
//      (type == 1 || type == 2);
//   3. the PMD files shipped inside the original (Miku_Hatsune.pmd,
//      KAITO.pmd, Dammy_Bone.pmd): every ＩＫ bone is type 2, the translate
//      controls (センター/袖) are type 1, IK links are 4 with tailIdx holding
//      the driving IK bone, eyes are 5, IK chain ends are 6 and every 「先」
//      tip marker is 7 - no file contains a type 3 at all.
//
// NOTE: public PMD spec tables that read "1=rotate, 2=translate, 3=IK" match
// neither the shipped files nor MMD's own code; naming follows MMD's actual
// behaviour (see the audit trail above).
// ---------------------------------------------------------------------------
enum class BoneType : unsigned char {
    RotateMove = 0,   // standard joint bone, rotate + translate (回転移動);
                      // the default after memset(0)
    Move = 1,         // translate-only control bone (移動; files: センター,
                      // 袖); PMX loader maps flag 0x04 translatable -> here
    Ik = 2,           // IK chain driver bone (files: 全ＩＫ bones); PMX IK
                      // bones map here; gets an IK-off selector slot with Move
    Unused3 = 3,      // never emitted by the shipped files; MMD compares no
                      // explicit == 3 anywhere (only the <= 6 ranges cover it)
    UnderIk = 4,      // IK影響下: IK chain link driven by the IK bone whose
                      // index sits in tailIdx (files: 足/ひざ/髪 tgt=ＩＫ);
                      // PMX loader marks every IK link here; resets the scale
                      // quat each pass (bone_transform B/E)
    RotateGrant = 5,  // 回転付与: quat = source(tailIdx).quat * own quat
                      // (files: 左目/右目 tgt=両目)
    Effector = 6,     // 接続先表示用: IK chain end-effector / inert chain tip
                      // (files: つま先, ﾏﾌﾗｰ５); PMX ikTarget bones map here
    InertTip = 7,     // display-only tail marker (all 「先」bones); never
                      // keyframe-registered; PMX loader maps !(flags &
                      // kBoneFlagVisible) here
    FixedAxis = 8,    // axis-limited twist bone: rotation constrained to the
                      // bone axis (key/VPD registration applies the twist-axis
                      // correction); PMX flag 0x0400 fixed axis maps here
    CoRotate = 9,     // 対象ボーン回転付与 (co-rotate): rotation is the tail
                      // bone's quat re-scaled about its own axis by
                      // tailIdx/100 (bone_transform 0x493F1F / 0x4968F3)
};

// Flag word (BoneRecord::flags).  PMD loads set only Visible on every bone;
// PMX loads keep the file's bits verbatim (bit order = PMX 2.0 spec).
constexpr std::uint16_t kBoneFlagTailIsBone = 0x0001;     // tail is a bone index (else vec3 offset, +464)
constexpr std::uint16_t kBoneFlagMovable = 0x0004;        // translation editable; PMX loader -> BoneType::Move
constexpr std::uint16_t kBoneFlagVisible = 0x0008;        // drawn/pickable; PMD loader sets it on every bone
constexpr std::uint16_t kBoneFlagIk = 0x0020;             // bone carries an IK chain record
constexpr std::uint16_t kBoneFlagRotInherit = 0x0100;     // rotation inherited from tailIdx bone (rate: inheritRatio)
constexpr std::uint16_t kBoneFlagTransInherit = 0x0200;   // translation inherited from tailIdx bone
constexpr std::uint16_t kBoneFlagInheritMask = 0x0300;    // either inherit bit (physics sweep gate)
constexpr std::uint16_t kBoneFlagFixedAxis = 0x0400;      // fixed axis -> BoneType::FixedAxis / axis[3]
constexpr std::uint16_t kBoneFlagLocalAxes = 0x0800;      // local coordinate axes (localAxes[6])
constexpr std::uint16_t kBoneFlagAfterPhysics = 0x1000;   // transform after physics (BoneFrameTransform a2 router)
constexpr std::uint16_t kBoneFlagExternalParent = 0x2000; // external parent transform (extParent)

struct BoneRecord {
    char name[20];  // 0  (SJIS)
    char nameEn[20];  // 20  (x64 anchor 20 verified (sub_14008CE20))
    wchar_t* jpText;  // 40  (read-text buffer (pmx ReadTextBuf JP); freed after conversion)
    wchar_t* enText;  // 44  (read-text buffer (EN))
    std::int32_t parent;  // 48  (parent bone index)
    float matInit[16];  // 52  (source matrix (debug_geometry walks elements))
    float matLocal[16];  // 116  (skinning/local matrix (bone_transform, vpd 0x74 region))
    float matWorld[16];  // 180  (world matrix; loaders write identity (f%5==0 -> 1.0))
    float matExtra[16];  // 244  (extension matrix (memcpy 0x40 src; dialog_select_ops reads [0]))
    float position[3];  // 308  (model-space position)
    float trans[3];  // 320  (vpd translation (0x140))
    float rotQuat[4];  // 332  (vpd quat (0x14C); w@344=1.0f init)
    float rotQuat2[4];  // 348  (w@360=1.0f init)
    float physicsOffset[3];  // 364  physics-swept translation (was f364);
                             // feeds matLocal and the 0x100/0x200 inheritance
    float physicsQuat[4];  // 376  physics-swept rotation quaternion (was
                           // f376); CoRotate rescales it about the tail axis
    float ikBackup[7];  // 392  (keyframe backup block 392..419 (kfa mirrors; [3..6]=quat))
    std::int32_t rigidIdx;  // 420  (-296 sentinel = unlinked (x64 anchor 428))
    float ikWorkingPos[3];  // 424  (kfa working-copy position 424..435)
    float ikWorkingQuat[4];  // 436  (kfa working-copy quaternion 436..447)
    std::int32_t selState;  // 452  (frame_modes / sprite_overlay)
    std::int32_t selState2;  // 456
    std::int32_t tailBone;  // 460  (tail-is-bone index)
    float tailOffset[3];  // 464  (tail-is-offset vector)
    std::int32_t tailScreenX;  // 476  projected screen X of the tail point
                               // (offset-tail physics bones; was f476)
    std::int32_t tailScreenY;  // 480  ... and Y (was f480)
    BoneType type;  // 484  (bone type byte (x64 anchor 492: <7 or ==8 name filter))
    RawPad<3> gap0;  // 485..488 (unrecovered)
    std::int32_t tailIdx;  // 488  (inheritance source index)
    unsigned char hasRigidBody;  // 492  bone drives / is driven by a rigid
                                 // body (physics participation gate; was f492)
    unsigned char physicsDisabled;  // 493  per-bone physics off: keyframes
                                    // own the transform despite the rigid
                                    // body (PMM per-bone byte; was f493)
    RawPad<2> gap1;  // 494..496 (unrecovered)
    std::int32_t layer;  // 496  (transform layer)
    std::uint16_t flags;  // 500  (PMD/PMX bone flag bits (x64 anchor 508))
    RawPad<2> gap2;  // 502..504 (unrecovered)
    float inheritRatio;  // 504  (0x100/0x200 inheritance rate)
    float axis[3];  // 508  (fixed axis (0x400), normalized)
    float localAxes[6];  // 520  (local axes (0x800))
    std::int32_t extParent;  // 544  (external parent (0x2000))
    std::int32_t ikTarget;  // 548  (IK target bone)
    std::int32_t ikLoop;  // 552  (IK loop count)
    float ikAngle;  // 556  (IK angle limit)
    std::int32_t ikLinkCount;  // 560
#if defined(_M_X64)
    RawPad<4> gap3;  // x64 572..576 (align slide)
#endif
    PmxIkLinkRecord* ikLinks;  // 564  (x64 anchor 576)
    unsigned char twistEnable;  // 568  (pmx2 twist-limit solver gate (link[568], x64 584))
    RawPad<3> gap4;  // 569..572 (unrecovered)
    float ikLimitMin[3];  // 572  (ClampEuler lower bounds (x64 588))
    float ikLimitMax[3];  // 584  (ClampEuler upper bounds)
    unsigned char hasFlag;  // 596  (x64 anchor 612)
    RawPad<3> gap5;  // 597..600 (unrecovered)
    std::int32_t slotIndex;  // 600  (-1 init; x64 anchor 616 (sub_1400A9AC0 [r14+rax-8]))
#if defined(_M_X64)
    RawPad<4> gapTail;  // 620..624
#else
#endif
};

#if !defined(_M_X64)
static_assert(offsetof(BoneRecord, name) == 0,
              "name x86");
static_assert(offsetof(BoneRecord, nameEn) == 20,
              "nameEn x86");
static_assert(offsetof(BoneRecord, jpText) == 40,
              "jpText x86");
static_assert(offsetof(BoneRecord, enText) == 44,
              "enText x86");
static_assert(offsetof(BoneRecord, parent) == 48,
              "parent x86");
static_assert(offsetof(BoneRecord, matInit) == 52,
              "matInit x86");
static_assert(offsetof(BoneRecord, matLocal) == 116,
              "matLocal x86");
static_assert(offsetof(BoneRecord, matWorld) == 180,
              "matWorld x86");
static_assert(offsetof(BoneRecord, matExtra) == 244,
              "matExtra x86");
static_assert(offsetof(BoneRecord, position) == 308,
              "position x86");
static_assert(offsetof(BoneRecord, trans) == 320,
              "trans x86");
static_assert(offsetof(BoneRecord, rotQuat) == 332,
              "rotQuat x86");
static_assert(offsetof(BoneRecord, rotQuat2) == 348,
              "rotQuat2 x86");
static_assert(offsetof(BoneRecord, physicsOffset) == 364,
              "physicsOffset x86");
static_assert(offsetof(BoneRecord, physicsQuat) == 376,
              "physicsQuat x86");
static_assert(offsetof(BoneRecord, ikBackup) == 392,
              "ikBackup x86");
static_assert(offsetof(BoneRecord, rigidIdx) == 420,
              "rigidIdx x86");
static_assert(offsetof(BoneRecord, ikWorkingPos) == 424,
              "ikWorkingPos x86");
static_assert(offsetof(BoneRecord, ikWorkingQuat) == 436,
              "ikWorkingQuat x86");
static_assert(offsetof(BoneRecord, selState) == 452,
              "selState x86");
static_assert(offsetof(BoneRecord, selState2) == 456,
              "selState2 x86");
static_assert(offsetof(BoneRecord, tailBone) == 460,
              "tailBone x86");
static_assert(offsetof(BoneRecord, tailOffset) == 464,
              "tailOffset x86");
static_assert(offsetof(BoneRecord, tailScreenX) == 476,
              "tailScreenX x86");
static_assert(offsetof(BoneRecord, tailScreenY) == 480,
              "tailScreenY x86");
static_assert(offsetof(BoneRecord, type) == 484,
              "type x86");
static_assert(offsetof(BoneRecord, tailIdx) == 488,
              "tailIdx x86");
static_assert(offsetof(BoneRecord, hasRigidBody) == 492,
              "hasRigidBody x86");
static_assert(offsetof(BoneRecord, physicsDisabled) == 493,
              "physicsDisabled x86");
static_assert(offsetof(BoneRecord, layer) == 496,
              "layer x86");
static_assert(offsetof(BoneRecord, flags) == 500,
              "flags x86");
static_assert(offsetof(BoneRecord, inheritRatio) == 504,
              "inheritRatio x86");
static_assert(offsetof(BoneRecord, axis) == 508,
              "axis x86");
static_assert(offsetof(BoneRecord, localAxes) == 520,
              "localAxes x86");
static_assert(offsetof(BoneRecord, extParent) == 544,
              "extParent x86");
static_assert(offsetof(BoneRecord, ikTarget) == 548,
              "ikTarget x86");
static_assert(offsetof(BoneRecord, ikLoop) == 552,
              "ikLoop x86");
static_assert(offsetof(BoneRecord, ikAngle) == 556,
              "ikAngle x86");
static_assert(offsetof(BoneRecord, ikLinkCount) == 560,
              "ikLinkCount x86");
static_assert(offsetof(BoneRecord, ikLinks) == 564,
              "ikLinks x86");
static_assert(offsetof(BoneRecord, twistEnable) == 568,
              "twistEnable x86");
static_assert(offsetof(BoneRecord, ikLimitMin) == 572,
              "ikLimitMin x86");
static_assert(offsetof(BoneRecord, ikLimitMax) == 584,
              "ikLimitMax x86");
static_assert(offsetof(BoneRecord, hasFlag) == 596,
              "hasFlag x86");
static_assert(offsetof(BoneRecord, slotIndex) == 600,
              "slotIndex x86");
static_assert(sizeof(BoneRecord) == 604,
              "bone record x86 size");
#else
static_assert(offsetof(BoneRecord, nameEn) == 20,
              "nameEn x64");
static_assert(offsetof(BoneRecord, jpText) == 40,
              "jpText x64");
static_assert(offsetof(BoneRecord, matInit) == 60,
              "matInit x64 (PMX skinning worker)");
static_assert(offsetof(BoneRecord, matWorld) == 188,
              "matWorld x64");
static_assert(offsetof(BoneRecord, position) == 316,
              "position x64");
static_assert(offsetof(BoneRecord, trans) == 328,
              "trans x64");
static_assert(offsetof(BoneRecord, rotQuat) == 340,
              "rotQuat x64");
static_assert(offsetof(BoneRecord, rigidIdx) == 428,
              "rigidIdx x64");
static_assert(offsetof(BoneRecord, type) == 492,
              "type x64");
static_assert(offsetof(BoneRecord, flags) == 508,
              "flags x64");
static_assert(offsetof(BoneRecord, ikLinks) == 576,
              "ikLinks x64");
static_assert(offsetof(BoneRecord, hasFlag) == 612,
              "hasFlag x64");
static_assert(offsetof(BoneRecord, slotIndex) == 616,
              "slotIndex x64");
static_assert(sizeof(BoneRecord) == 624,
              "bone record x64 size");
#endif

}  // namespace mikudancestudio::mdl
