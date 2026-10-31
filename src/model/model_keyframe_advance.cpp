// ===========================================================================
// VA 0x004A31D0 - per-model keyframe application  (5754 bytes, 187 blocks)
// + helpers VA 0x004A05A0 (VMD interpolation easing) and 0x00499B50
// (bone physics-mode notify).
// ===========================================================================
// __thiscall on the model block, called once per model per playback tick by
// PlaybackPoseAdvance (0x4175A0, timeline_advance.cpp):
//
//   AdvanceModelKeyframes(model, cursor, physicsMode)
//     cursor       float seconds (app 0x9E64C physics cursor)
//     physicsMode  int app 0xA0CC4 (>=2 enables the IK-off bookkeeping and
//                  the rigid-body notifications)
//
// Three sections (original addresses):
//   1  0x4A3236  IK master tracks.  28-byte records at model+9960
//      {+0 frame(u32), +4 prevIdx, +8 nextIdx, +12 display byte ->
//       model+11661, +16 ptr per-IK display-flag bytes -> 24B structs at
//       model+9920 (+18 each), +24 ptr per-IK 8-byte {frame, selIdx} pairs},
//      cursor model+52, gate model+56, count model+11656 (IK chains).
//      Advance while frame < cursor; parking/refresh come in three variants:
//        A  terminal (0x4A3291)  pairs from the cursor record, backward walk
//           for sel+4, sel+8 = 0;
//        B  exact hit (0x4A3477) same, plus a forward walk for sel+8
//           (condition against the cursor record's pair - tautology);
//        C  between keys (0x4A36DF) pairs/flags from the PREVIOUS record,
//           forward condition against the cursor record's pair.
//      The 20-byte selector records at model+314596 (count model+314600,
//      referenced by &bone->slotIndex) hold the IK-off window {+4 start frame,
//      +8 end frame, +12/+16 the {frame, idx} pair} consumed by section 3.
//   2  0x4A391B  morph tracks.  20-byte key records at model+9956
//      {+0 frame, +4 prevIdx, +8 nextIdx, +12 float value}, per-morph cursors
//      int[] at model+44, active bytes at model+48, count model+11648;
//      current value stored to 136-byte structs at model+9924 (+48).
//      Linear interpolation only (VMD morphs have no curve).
//   3  0x4A3AA5  bone tracks.  60-byte key records at model+9952
//      {+0 frame, +4 prevIdx, +8 nextIdx, +12..27 bezier control bytes
//       x1[4] y1[4] x2[4] y2[4] (one byte per channel 0..3, channel-stride
//       1 inside each group), +28..38 position xyz, +40..55 quaternion
//       xyzw, +57 IK-off mode byte}, per-bone cursors int[] at model+36,
//      active bytes at model+40, count model+11652.  Bone structs (604
//       bytes, model+9916): +320 position, +332 quaternion, +392 position
//       backup, +404 quaternion backup, +420 armed start frame, +424/+436
//       working position/quaternion copies, +484 type byte, +492 notify
//       gate, +493 current mode, +600 selector index (-1 = none).
//
//      Quaternion blend is a hand-rolled slerp (0x4A43EF / x64
//      0x7FF7CB4F1D91): dot clamp to +-0.999999f (0x3F7FFFEF, x86
//      flt_531134 = x64 flt_7FF7CB552C24 - NOT 0x3F7FFFFF as first
//      transcribed), and when the angle exceeds the original's pi/2
//      constant (x86 double 0x3FF921FB00000000 / x64 float 0x3FC90FD8 -
//      pi/2 truncated to a 24-bit mantissa, NOT the exact double) while
//      dot < 0 the complement angle acos(-dot) with SUBTRACTED weights is
//      used; otherwise normal added weights.  x86 runs sin/acos in double
//      on float-rounded intermediates; x64 is single precision end to end
//      (acosf/sinf, weights = sin * (1/s) in this advance copy - the
//      0x4B4260 seek copy divides sin/s instead).
//
//      DECOMPILER TRAP (same class as the 0x41789E one, opposite direction):
//      Hex-Rays shows the slerp weights using the RAW fraction; live disasm
//      0x4A44F8 `fsub var_3C` / 0x4A4521 `fmul var_3C` proves the EASED
//      rotation fraction (sub_4A05A0 channel 3, stored float var_3C at
//      0x4A43A5) feeds both sin((1-t)*theta) and sin(t*theta).  The position
//      easing (0x4A4718/0x4A4778/0x4A47EE) passes the RAW fraction in and
//      uses the callee's return value directly (never stored, so no extra
//      rounding by the caller).
//
//      Mid-interpolation bookkeeping (physicsMode >= 2, &bone->hasRigidBody): when the
//      current key's +57 byte is 1 and the previous key's is 0, the blend
//      runs from the ARMED working copies (+424/+436) with prevFrame taken
//      from &bone->rigidIdx instead of the previous record.  The arm is refreshed
//      (0x4A3FF1, unsigned: start < prevFrame || start >= curFrame) from
//      the backups (+392/+404); the exact-hit arm (0x4A3E54) additionally
//      copies the backups over the current pose (+320/+332).
//
//      The selector window (&bone->slotIndex -> 20B record) overrides blending
//      inside [selStart, selEnd): comparisons unsigned (0x4A41E9 sbb/ neg
//      idiom); inside the window the bone snaps to prev (selStart > cursor)
//      or cur (selStart <= cursor / selEnd <= cursor) verbatim.
//
// 0x004A05A0  VMD easing, __thiscall(model, channel, keyIdx, t).  Control
//      bytes at record+channel+{12,16,20,24} (x1,y1,x2,y2, signed), scaled
//      by the double at 0x531108 (0.02362200058996677 - float 0.023622
//      promoted, nominally 3/127).  X(u) = 3(1-u)^2 u x1 + 3(1-u) u^2 x2 +
//      u^3 is inverted by 12 halving bisection steps from u = 0.5 (step
//      halves before each update; exact double-equality early exit; u kept
//      as float); returns Y(u) rounded to float.  Linear early-out
//      x1 == y1 && x2 == y2 returns t unchanged.  Same shape as the camera
//      easing 0x410140 (timeline_advance.cpp) but with the 3x folded into
//      the scale constant and channel-interleaved control bytes.
//
// 0x00499B50  bone physics-mode notify, __thiscall(model, boneIdx, mode):
//      walks the 172-byte rigid-body records at model+12744 (count
//      model+12752); record +28 = bone index, +82 = skip gate, +104 =
//      object pointer.  mode != 0 makes the body kinematic and record+80 =
//      0; mode == 0 restores record+80 to (record+81 ? 2 : 1).  The original
//      writes the old Bullet collision-flag member directly; the port uses
//      Bullet's ABI-stable public collision-flag API for that same operation.
//      Also called from 0x4A2CD0/0x4B2210/0x4B38A0/0x4B4260 (future phases).
//
// Reference: IDA live disassembly of MikuMikuDance.exe v932 (sole source of
// truth; ../translated/ reference files deviate).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "btBulletDynamicsCommon.h"

#include "mikudancestudio/model.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include "native_keyframe_kernel.hpp"

namespace mikudancestudio {
// ---- VA 0x004A05A0 / x64 sub_7FF7CB4EDB00 ----------------------------------
float BoneEase(const mdl::ModelRecord* model, int channel, int keyIdx, float t) {
    return NativeKeyframeKernel::BoneEase(model, channel, keyIdx, t);
}

// ---- VA 0x00499B50 --------------------------------------------------------
void NotifyBonePhysicsMode(mdl::ModelRecord* model, int boneIdx, unsigned char mode) {
    NativeKeyframeKernel::NotifyBonePhysicsMode(model, boneIdx, mode);
}

// ---------------------------------------------------------------------------
// VA 0x004B2210 - broadcast the physics view mode to every bone-flagged
// rigid notify.  __thiscall(model, int mode) where mode is app+0xA0CC4
// (view menu 0x109/0x10D/0x10E/0x110).
//   mode == 0 : physics off  -> NotifyBonePhysicsMode(model, bone, 1)  (kinematic)
//   mode == 1 : physics on   -> NotifyBonePhysicsMode(model, bone, 0)  (dynamic)
//   mode >= 2 : per-bone flag at &bone->physicsDisabled selects 1/0
// x64 RVA 0xC2EE0: only bones with hasRigidBody participate. The menu
// and PMM-load callers pass the current view mode; this changes collision
// flags/modes without pushing a motion-state transform (ModelKinematicSync).
// ---------------------------------------------------------------------------
void ApplyModelBonePhysicsMode(mdl::ModelRecord* model, int mode) {
    NativeKeyframeKernel::ApplyModelBonePhysicsMode(model, mode);
}

// ---- VA 0x004A2CD0 --------------------------------------------------------
// Initializes every per-model animation cursor at playback start.  Unlike
// SeekModelFrame this preserves the incremental-track state consumed by 0x4A31D0.
void InitModelTrackCursors(mdl::ModelRecord* model, float cursor, int physicsMode) {
    NativeKeyframeKernel::InitModelTrackCursors(model, cursor, physicsMode);
}

// ---- VA 0x004A31D0 --------------------------------------------------------
void AdvanceModelKeyframes(mdl::ModelRecord* model, float cursor, int physicsMode) {
    NativeKeyframeKernel::AdvanceModelKeyframes(model, cursor, physicsMode);
}

} // namespace mikudancestudio
