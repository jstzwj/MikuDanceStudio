#pragma once
#include <cstdint>

namespace mikudancestudio::cpu_bone {
enum class BoneType : unsigned char {
    RotateMove = 0,  // standard joint bone, rotate + translate;
                     // the default after memset(0)
    Move = 1,        // translate-only control bone (translation; files: center,
                     // sleeve); PMX loader maps flag 0x04 translatable -> here
    Ik = 2,          // IK chain driver bone (files: all IK bones); PMX IK
                     // bones map here; gets an IK-off selector slot with Move
    Unused3 = 3,     // never emitted by the shipped files; MMD compares no
                     // explicit == 3 anywhere (only the <= 6 ranges cover it)
    UnderIk = 4,     // under IK influence: IK chain link driven by the IK bone whose
                     // index sits in tailIdx (files: leg/knee/hair tgt=IK);
                     // PMX loader marks every IK link here; resets the scale
                     // quat each pass (bone_transform B/E)
    RotateGrant = 5, // rotation grant: quat = source(tailIdx).quat * own quat
                     // (files: left eye/right eye tgt=both eyes)
    Effector = 6,    // connection-target display: IK chain end-effector / inert chain tip
                     // (files: toe, scarf 5); PMX ikTarget bones map here
    InertTip = 7,    // display-only tail marker (all "tip" bones); never
                     // keyframe-registered; PMX loader maps !(flags &
                     // kBoneFlagVisible) here
    FixedAxis = 8,   // axis-limited twist bone: rotation constrained to the
                     // bone axis (key/VPD registration applies the twist-axis
                     // correction); PMX flag 0x0400 fixed axis maps here
    CoRotate = 9,    // target-bone rotation grant (co-rotate): rotation is the tail
                     // bone's quat re-scaled about its own axis by
                     // tailIdx/100 (bone_transform 0x493F1F / 0x4968F3)
};

// Flag word (BoneRecord::flags).  PMD loads set only Visible on every bone;
// PMX loads keep the file's bits verbatim (bit order = PMX 2.0 spec).
constexpr std::uint16_t kBoneFlagTailIsBone =
    0x0001; // tail is a bone index (else vec3 offset, +464)
constexpr std::uint16_t kBoneFlagMovable =
    0x0004; // translation editable; PMX loader -> BoneType::Move
constexpr std::uint16_t kBoneFlagVisible =
    0x0008;                                   // drawn/pickable; PMD loader sets it on every bone
constexpr std::uint16_t kBoneFlagIk = 0x0020; // bone carries an IK chain record
constexpr std::uint16_t kBoneFlagRotInherit =
    0x0100; // rotation inherited from tailIdx bone (rate: inheritRatio)
constexpr std::uint16_t kBoneFlagTransInherit = 0x0200; // translation inherited from tailIdx bone
constexpr std::uint16_t kBoneFlagInheritMask = 0x0300;  // either inherit bit (physics sweep gate)
constexpr std::uint16_t kBoneFlagFixedAxis = 0x0400; // fixed axis -> BoneType::FixedAxis / axis[3]
constexpr std::uint16_t kBoneFlagLocalAxes = 0x0800; // local coordinate axes (localAxes[6])
constexpr std::uint16_t kBoneFlagAfterPhysics =
    0x1000; // transform after physics (BoneFrameTransform a2 router)
constexpr std::uint16_t kBoneFlagExternalParent = 0x2000; // external parent transform (extParent)

}
