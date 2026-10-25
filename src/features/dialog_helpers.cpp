// ===========================================================================
// dialog_helpers.cpp - remaining dialog/feature gap functions (round N)
// ===========================================================================
// Ported here (Hex-Rays pseudocode is ground truth; x87 operand order and
// store rounding are reproduced with explicit double intermediates wherever
// the original keeps values on the FPU stack):
//   0x004999E0  sign-corrected quaternion nlerp + normalize
//   0x0048F7C0  16-dword matrix copy into object+16 (thiscall)
//   0x004B2210  bone physics-mode broadcast over the rigid notify
//   0x004A8BC0  rolling vertex-position history push + average
//   0x004B75C0  24-channel model history push (dispatch of 0x4A8BC0)
//   0x004B5760  per-frame standard-pose setup + physics trace recorder
//
// Already covered elsewhere (verified this round - NOT re-ported):
//   0x00490230  PMD model writer - src/io/pmd_save.cpp
//   0x004B0C50  UpdateModelVertexBuffers - src/model/model_skinning.cpp:318,
//               declared include/mikudancestudio/ported_funcs.hpp:133
//   0x004912F0  absorbed into src/render/model_renderers.cpp:
//               ConfigureMaterialStages (:430, texture cascade 0x491470..
//               0x491B38/0x491E41..0x492163), DrawModelMaterials (:817,
//               blend prefix + cull/draw branches 0x4921D1..0x4924CE),
//               ResetSphereStageAfterDraw (:596, 0x4925CE..0x4925E5),
//               BuildToonTransform (:382, D3DX toon matrix block)
//   0x00492640  DrawModelEdgeGeometry - src/render/model_renderers.cpp:965
//               (edge-subset DrawIndexedPrimitive loop, EgColor effect pass)
//   0x00492860  shadow material pass absorbed into DrawModelMaterials
//               (shadowOnly path, src/render/model_renderers.cpp:844-848) and
//               reached from the RenderShadowMap port (:1392, 0x426CD0);
//               the skip test includes the original's PMX shadow-cast-bit
//               term (~(flags>>2) & pmx, 0x492914..0x492927) in
//               model_renderers.cpp DrawModelMaterials.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <io.h>
#include <new>
#include <share.h>
#include <sys/stat.h>

#include "mikudancestudio/charset_conv.hpp"
#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"

namespace mikudancestudio {

// VA 0x004A6520 - InitStandardSkeletonQuats: standard-pose
// quaternion derivation from the leg/arm bone
// chain (fills model+64..332).  Defined in src/features/audio_pose_helpers.cpp
// (same namespace/build); declared here for the 0x4B5760 call below.
// was: int 0x4A6520(unsigned char* model, unsigned char a2); with a
// "body not yet ported" note - stale, and the int return disagreed with the
// definition (ODR).  The original's eax is dead at its sole caller
// (0x4B5850; the next instruction is fld, no eax read), hence void.
void InitStandardSkeletonQuats(mdl::ModelRecord* model, unsigned char flag);

namespace {

template <typename T> inline T& At(unsigned char* p, std::size_t offset) {
    return *reinterpret_cast<T*>(p + offset);
}

template <typename T> inline const T& At(const unsigned char* p, std::size_t offset) {
    return *reinterpret_cast<const T*>(p + offset);
}

} // namespace

// ---------------------------------------------------------------------------
// VA 0x004999E0 - sign-corrected quaternion lerp + normalize ("nlerp").
// __stdcall(float out[4], float q0x, float q0y, float q0z, float q0w,
//           float q1x, float q1y, float q1z, float q1w, float t) -> out.
// Callers: the 0x4A9400-family OpenMP skinning workers (ported in
// src/model/model_skinning.cpp behind the 0x4B0C50 dispatcher).  All arithmetic runs on the x87 stack: products and
// differences are computed in double and truncated only at each float store,
// which the explicit double temporaries below reproduce.
// ---------------------------------------------------------------------------
float* QuaternionNlerp(float* out, float q0x, float q0y, float q0z, float q0w, float q1x, float q1y,
                       float q1z, float q1w, float t) {
    const double dot = (double)q1x * (double)q0x + (double)q1w * (double)q0w +
                       (double)q1y * (double)q0y + (double)q1z * (double)q0z; // 0x499A23
    const float dotF = (float)dot;
    float tt;           // st(6) mirror
    if (dotF >= 0.0f) { // 0x499A36
        tt = t;
        out[3] = (float)((double)q0w + ((double)q1w - (double)q0w) * (double)t);
        out[0] = (float)((double)q0x + ((double)q1x - (double)q0x) * (double)t);
        out[1] = (float)((double)q0y + ((double)q1y - (double)q0y) * (double)t);
        out[2] = (float)((double)q0z + (double)t * ((double)q1z - (double)q0z));
    } else {
        const float nt = -t; // 0x499A3E
        tt = nt;
        out[3] = (float)((double)q0w + ((double)q1w + (double)q0w) * (double)nt);
        out[0] = (float)((double)q0x + ((double)q1x + (double)q0x) * (double)nt);
        out[1] = (float)((double)q0y + ((double)q1y + (double)q0y) * (double)nt);
        out[2] = (float)((double)q0z + (double)nt * ((double)q1z + (double)q0z));
    }
    const float norm =
        (float)((double)out[1] * (double)out[1] + (double)out[0] * (double)out[0] +
                (double)out[2] * (double)out[2] + (double)out[3] * (double)out[3]); // 0x499ADE
    const float len = (float)std::sqrt((double)norm);                               // 0x499AEB
    if (len <= 0.0f) {                                                              // 0x499B06
        out[3] = 1.0f;
        out[2] = 0.0f;
        out[1] = 0.0f;
        out[0] = 0.0f;
        return out;
    }
    const float inv = (float)(1.0 / (double)len); // 0x499B0E
    out[0] = out[0] * inv;
    out[1] = out[1] * inv;
    out[2] = out[2] * inv;
    out[3] = inv * out[3]; // operand order
    return out;
}

// ---------------------------------------------------------------------------
// VA 0x0048F7C0 - copy a 4x4 matrix into this+16 (16 dwords, plain copy).
// __thiscall(receiver, const float matrix[16]) -> eax = last element.
// Callers (unported): 0x4DD7C0 (3 sites), 0x4FC760.  The receiver object is
// the one carrying its transform matrix at offset +16.
// ---------------------------------------------------------------------------
void CopyMatrixToObject(unsigned char* self, const void* matrix16) {
    std::memcpy(self + 16, matrix16, 16 * 4);
}

// ---------------------------------------------------------------------------
// VA 0x004A8BC0 - push a tracked position into its rolling history and
// re-average.  __stdcall(float current[3], float history[], int count).
//   1. shift the `count` entries (3 floats each) one slot toward index 0
//      (rep movsd in the original; memmove here - same byte result)
//   2. store `current` into the last slot
//   3. average every entry whose Y component != -999.0f and write the
//      average back into `current` (accumulate in float, one rounding per
//      add exactly like the fstp-per-iteration original; the 1/count and
//      product run through double).
// Sole caller: 0x4B75C0.
// x86 0x4B75C0 / x64 0x7FF7CB4F9CC0: each joint has 30 vec3 samples.
// The original visits these independent channels in a different order. They
// do not overlap; each stores the raw input before averaging valid Y samples.
// The window is FPS/10, clamped to 30; zero FPS leaves histories unchanged.
void ModelVertexHistoryPush(mdl::ModelRecord* model, int samples) {
    if (model == nullptr || samples <= 0)
        return;
    const int n = samples > static_cast<int>(mdl::kSkeletonHistoryLength)
                      ? static_cast<int>(mdl::kSkeletonHistoryLength)
                      : samples;
    auto& state = *model;
    for (std::size_t joint = 0; joint < mdl::kTrackedJointCount; ++joint)
        mdl::PushSkeletonJointHistory(state.currentJoints.positions[joint],
                                      state.skeletonHistory.joints[joint], n);
}

// ---------------------------------------------------------------------------
// VA 0x004B5760 - per-frame standard-pose setup + physics trace recorder.
// __thiscall(model, bool recordEnable(a2), char mirrorLeftRight(a3),
//             char skeletonFlag(a4)) -> char
// (returns 1 when recording was previously active and is now stopped).
// Callers: 0x46FBB9 in the FrameDriver - args are (app+0xA0D68 == 4),
// app+0xA03DC, app+0xA03DD.  Runs after InitStandardSkeletonQuats (standard-pose quaternions
// into model+64..332) and mirrors, per named standard bone, a quaternion
// (bone->trans..336) and position (bone->rotQuat..344) with left/right
// mirroring on the mirrorLeftRight flag; the center is additionally height-scaled.
// While recording, each bone writes its channel into the current PoseTraceSample;
// the sample count advances once after all bones have been processed.
// Bone-name constants are raw Shift-JIS from the original .rdata, including NUL.
// ---------------------------------------------------------------------------
namespace {

const unsigned char kCenter[9] =                            // 0x531184
    {0x83, 0x5A, 0x83, 0x93, 0x83, 0x5E, 0x81, 0x5B, 0x00}; // センター
const unsigned char kUpperBody[7] =                         // 0x53117C
    {0x8F, 0xE3, 0x94, 0xBC, 0x90, 0x67, 0x00};             // upper body
const unsigned char kNeck[3] =                              // 0x531178
    {0x8E, 0xF1, 0x00};                                     // neck
const unsigned char kArmL[5] =                              // 0x52B81C
    {0x8D, 0xB6, 0x98, 0x72, 0x00};                         // left arm
const unsigned char kElbowL[7] =                            // 0x52B814
    {0x8D, 0xB6, 0x82, 0xD0, 0x82, 0xB6, 0x00};             // left elbow
const unsigned char kArmR[5] =                              // 0x52B804
    {0x89, 0x45, 0x98, 0x72, 0x00};                         // right arm
const unsigned char kElbowR[7] =                            // 0x52B7FC
    {0x89, 0x45, 0x82, 0xD0, 0x82, 0xB6, 0x00};             // right elbow
const unsigned char kLowerBody[7] =                         // 0x531170
    {0x89, 0xBA, 0x94, 0xBC, 0x90, 0x67, 0x00};             // lower body
const unsigned char kLegL[5] =                              // 0x531168
    {0x8D, 0xB6, 0x91, 0xAB, 0x00};                         // left leg
const unsigned char kLegR[5] =                              // 0x531160
    {0x89, 0x45, 0x91, 0xAB, 0x00};                         // right leg
const unsigned char kKneeL[7] =                             // 0x530F60
    {0x8D, 0xB6, 0x82, 0xD0, 0x82, 0xB4, 0x00};             // left knee
const unsigned char kKneeR[7] =                             // 0x530F58
    {0x89, 0x45, 0x82, 0xD0, 0x82, 0xB4, 0x00};             // right knee
const unsigned char kLegIkL[9] =                            // 0x531154
    {0x8D, 0xB6, 0x91, 0xAB, 0x82, 0x68, 0x82, 0x6A, 0x00}; // left leg IK
const unsigned char kLegIkR[9] =                            // 0x531148
    {0x89, 0x45, 0x91, 0xAB, 0x82, 0x68, 0x82, 0x6A, 0x00}; // right leg IK
const unsigned char kWristL[7] =                            // 0x52B80C
    {0x8D, 0xB6, 0x8E, 0xE8, 0x8E, 0xF1, 0x00};             // left wrist
const unsigned char kWristR[7] =                            // 0x52B7F4
    {0x89, 0x45, 0x8E, 0xE8, 0x8E, 0xF1, 0x00};             // right wrist
const unsigned char kShoulderL[5] =                         // 0x531140
    {0x8D, 0xB6, 0x8C, 0xA8, 0x00};                         // left shoulder
const unsigned char kShoulderR[5] =                         // 0x531138
    {0x89, 0x45, 0x8C, 0xA8, 0x00};                         // right shoulder
const unsigned char kAnkleL[7] =                            // 0x5311D8
    {0x8D, 0xB6, 0x91, 0xAB, 0x8E, 0xF1, 0x00};             // left ankle

// Apply a model-space quaternion and store its named trace channel in WXYZ order.
void WritePoseGroup(mdl::ModelRecord* model, mdl::BoneRecord* bone,
                    mdl::PoseTraceQuaternion mdl::PoseTraceSample::* channel, float x, float y,
                    float z, float w) {
    bone->rotQuat[0] = x;
    bone->rotQuat[1] = y;
    bone->rotQuat[2] = z;
    bone->rotQuat[3] = w;
    if (model->poseTraceRecording != 0)
        (model->poseTraceBuffer[model->poseTraceSampleCount].*channel).Store(bone->rotQuat);
}

} // namespace

char ModelStandardPoseSetup(mdl::ModelRecord* model, bool recordEnable,
                            unsigned char mirrorLeftRight, unsigned char skeletonFlag) {
    char wasRecording = 0;
    if (model->poseTraceRecording == 0 && recordEnable) { // 0x4B577A
        model->poseTraceRecording = 1;
        if (model->poseTraceBuffer != nullptr) {
            ::operator delete(model->poseTraceBuffer); // 0x4B5795
            model->poseTraceBuffer = nullptr;
        }
        model->poseTraceBuffer = static_cast<mdl::PoseTraceSample*>(
            ::operator new(mdl::kPoseTraceCapacity * sizeof(mdl::PoseTraceSample))); // 0x4B57B0
        model->poseTraceSampleCount = 0;
    }
    if (!recordEnable) { // 0x4B57BE
        if (model->poseTraceRecording != 0)
            wasRecording = 1;
        model->poseTraceRecording = 0;
    }

    // IK scan (0x4B57DA..0x4B5847): pick up the leg-IK "add" flags that the
    // knee branches below special-case.  recordEnable (a2) is overwritten
    // like the original.
    bool leftIkAdd = recordEnable;
    bool rightIkAdd = false;
    if (model->ikChainCount > 0) {
        for (std::uint16_t i = 0; i < (std::uint16_t)model->ikChainCount; ++i) {
            const mikudancestudio::mdl::IkChain& ik = model->ikChains[i];
            const mikudancestudio::mdl::BoneRecord* bone = &model->boneTable[ik.boneIndex];
            if (std::memcmp(bone, kLegIkL, 9) == 0)
                leftIkAdd = ik.enabled != 0; // 0x4B5814
            if (std::memcmp(bone, kLegIkR, 9) == 0)
                rightIkAdd = ik.enabled != 0; // 0x4B582E
        }
    }

    InitStandardSkeletonQuats(model, skeletonFlag); // 0x4B5850

    // Height-scale normalization (0x4B5854..0x4B5994): when the scale slot
    // still holds the 90.0 sentinel and a leg height exists, derive the
    // scale from the left-leg bind pose and the offsets from the left ankle.
    if ((mdl::SkeletonScalar)model->skeletonCalibration.heightScale == 90.0 &&
        (mdl::SkeletonScalar)model->skeletonCalibration.legLength != -1.0) {
        const int boneCount = model->boneCount;
        mikudancestudio::mdl::BoneRecord* bones = model->boneTable;
        const int leg = mdl::FindSkeletonBone(bones, boneCount, kLegL, sizeof kLegL);
        if (leg >= 0) {
            const float legY = bones[leg].position[1];
            if ((mdl::SkeletonScalar)legY != -1.0) {
                const float scale = model->skeletonCalibration.legLength / legY; // 0x4B5928
                const mdl::SkeletonScalar scaleD = scale;                        // held st(6)
                model->skeletonCalibration.heightScale = scale;
                const float offset =
                    -model->currentJoints[mdl::TrackedJoint::RightAnkle][1] / scale; // 0x4B5954
                model->skeletonCalibration.heightOffset = offset;
                const int ankle = mdl::FindSkeletonBone(bones, boneCount, kAnkleL, sizeof kAnkleL);
                if (ankle >= 0) {
                    const float ankleY = bones[ankle].position[1];
                    if ((mdl::SkeletonScalar)ankleY == -1.0) {
                        model->skeletonCalibration.legIkXOffset =
                            (float)(mdl::SkeletonScalar{80} / scaleD);
                    } else {
                        model->skeletonCalibration.heightOffset =
                            (float)((mdl::SkeletonScalar)ankleY / scaleD +
                                    (mdl::SkeletonScalar)offset);
                        model->skeletonCalibration.legIkXOffset =
                            (float)(mdl::SkeletonScalar{80} / scaleD); // 0x4B59B6
                    }
                } else {
                    model->skeletonCalibration.legIkXOffset =
                        (float)(mdl::SkeletonScalar{80} / scaleD); // 0x4B5994
                }
            }
        }
    }

    // Mirror/emit the standard bones (0x4B5994..0x4B77C7).  v45..47 are the
    // センター position snapshot reused by the leg-IK "keep pose" fallback;
    // the original leaves them as stale stack bytes when the model has no
    // センター - the port zero-inits them (deviation noted).
    float centerPos[3] = {0.0f, 0.0f, 0.0f};
    const int boneCount = model->boneCount;
    mikudancestudio::mdl::BoneRecord* bones = model->boneTable;
    for (int i = 0; i < boneCount; ++i) {
        mikudancestudio::mdl::BoneRecord* bone = &bones[i];
        if (std::memcmp(bone, kCenter, 9) == 0) {
            if ((mdl::SkeletonScalar)model->currentJoints[mdl::TrackedJoint::Center][1] != -999.0) {
                const float inv = mdl::SkeletonScalar{1} /
                                  (mdl::SkeletonScalar)model->skeletonCalibration.heightScale;
                bone->trans[0] = (float)((mdl::SkeletonScalar)
                                             model->currentJoints[mdl::TrackedJoint::Center][0] *
                                         inv);
                bone->trans[1] = (float)((mdl::SkeletonScalar)
                                             model->currentJoints[mdl::TrackedJoint::Center][1] *
                                         inv);
                bone->trans[2] =
                    (float)(inv * (mdl::SkeletonScalar)
                                      model->currentJoints[mdl::TrackedJoint::Center][2]);
                if (mirrorLeftRight == 0)
                    bone->trans[0] = -bone->trans[0];
            }
            bone->rotQuat[3] = 1.0f;
            bone->rotQuat[0] = 0.0f;
            bone->rotQuat[1] = 0.0f;
            bone->rotQuat[2] = 0.0f;
            if (model->poseTraceRecording != 0) {
                auto& sample = model->poseTraceBuffer[model->poseTraceSampleCount];
                std::memcpy(sample.center, bone->trans, sizeof bone->trans);
            }
            centerPos[0] = bone->trans[0];
            centerPos[1] = bone->trans[1];
            centerPos[2] = bone->trans[2];
        } else if (std::memcmp(bone, kUpperBody, 7) == 0) {
            WritePoseGroup(model, bone, &mdl::PoseTraceSample::upperBody,
                           model->standardPose.upperBody[0],
                           mirrorLeftRight ? model->standardPose.upperBody[1]
                                           : -model->standardPose.upperBody[1],
                           mirrorLeftRight ? model->standardPose.upperBody[2]
                                           : -model->standardPose.upperBody[2],
                           model->standardPose.upperBody[3]);
        } else if (std::memcmp(bone, kNeck, 3) == 0) {
            WritePoseGroup(
                model, bone, &mdl::PoseTraceSample::neck, model->standardPose.neck[0],
                mirrorLeftRight ? model->standardPose.neck[1] : -model->standardPose.neck[1],
                mirrorLeftRight ? model->standardPose.neck[2] : -model->standardPose.neck[2],
                model->standardPose.neck[3]);
        } else if (std::memcmp(bone, kArmL, 5) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::leftArm,
                               model->standardPose.leftArm[0], model->standardPose.leftArm[1],
                               model->standardPose.leftArm[2], model->standardPose.leftArm[3]);
            else
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::leftArm,
                               model->standardPose.rightArm[0], -model->standardPose.rightArm[1],
                               -model->standardPose.rightArm[2], model->standardPose.rightArm[3]);
        } else if (std::memcmp(bone, kElbowL, 7) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::leftElbow,
                               model->standardPose.leftElbow[0], model->standardPose.leftElbow[1],
                               model->standardPose.leftElbow[2], model->standardPose.leftElbow[3]);
            else
                WritePoseGroup(
                    model, bone, &mdl::PoseTraceSample::leftElbow,
                    model->standardPose.rightElbow[0], -model->standardPose.rightElbow[1],
                    -model->standardPose.rightElbow[2], model->standardPose.rightElbow[3]);
        } else if (std::memcmp(bone, kArmR, 5) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::rightArm,
                               model->standardPose.rightArm[0], model->standardPose.rightArm[1],
                               model->standardPose.rightArm[2], model->standardPose.rightArm[3]);
            else
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::rightArm,
                               model->standardPose.leftArm[0], -model->standardPose.leftArm[1],
                               -model->standardPose.leftArm[2], model->standardPose.leftArm[3]);
        } else if (std::memcmp(bone, kElbowR, 7) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::rightElbow,
                               model->standardPose.rightElbow[0], model->standardPose.rightElbow[1],
                               model->standardPose.rightElbow[2],
                               model->standardPose.rightElbow[3]);
            else
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::rightElbow,
                               model->standardPose.leftElbow[0], -model->standardPose.leftElbow[1],
                               -model->standardPose.leftElbow[2], model->standardPose.leftElbow[3]);
        } else if (std::memcmp(bone, kLowerBody, 7) == 0) {
            WritePoseGroup(model, bone, &mdl::PoseTraceSample::lowerBody,
                           model->standardPose.lowerBody[0],
                           mirrorLeftRight ? model->standardPose.lowerBody[1]
                                           : -model->standardPose.lowerBody[1],
                           mirrorLeftRight ? model->standardPose.lowerBody[2]
                                           : -model->standardPose.lowerBody[2],
                           model->standardPose.lowerBody[3]);
        } else if (std::memcmp(bone, kLegL, 5) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::leftLeg,
                               model->standardPose.leftLeg[0], model->standardPose.leftLeg[1],
                               model->standardPose.leftLeg[2], model->standardPose.leftLeg[3]);
            else
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::leftLeg,
                               model->standardPose.rightLeg[0], -model->standardPose.rightLeg[1],
                               -model->standardPose.rightLeg[2], model->standardPose.rightLeg[3]);
        } else if (std::memcmp(bone, kLegR, 5) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::rightLeg,
                               model->standardPose.rightLeg[0], model->standardPose.rightLeg[1],
                               model->standardPose.rightLeg[2], model->standardPose.rightLeg[3]);
            else
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::rightLeg,
                               model->standardPose.leftLeg[0], -model->standardPose.leftLeg[1],
                               -model->standardPose.leftLeg[2], model->standardPose.leftLeg[3]);
        } else if (std::memcmp(bone, kKneeL, 7) == 0) { // 0x530F60
            if (leftIkAdd)
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::leftKnee, 0.0f, 0.0f, 0.0f,
                               1.0f);
            else if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::leftKnee,
                               model->standardPose.leftKnee[0], model->standardPose.leftKnee[1],
                               model->standardPose.leftKnee[2], model->standardPose.leftKnee[3]);
            else
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::leftKnee,
                               model->standardPose.rightKnee[0], -model->standardPose.rightKnee[1],
                               -model->standardPose.rightKnee[2], model->standardPose.rightKnee[3]);
        } else if (std::memcmp(bone, kKneeR, 7) == 0) { // 0x530F58
            if (rightIkAdd)
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::rightKnee, 0.0f, 0.0f, 0.0f,
                               1.0f);
            else if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::rightKnee,
                               model->standardPose.rightKnee[0], model->standardPose.rightKnee[1],
                               model->standardPose.rightKnee[2], model->standardPose.rightKnee[3]);
            else
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::rightKnee,
                               model->standardPose.leftKnee[0], -model->standardPose.leftKnee[1],
                               -model->standardPose.leftKnee[2], model->standardPose.leftKnee[3]);
        } else if (std::memcmp(bone, kLegIkL, 9) == 0) {
            if (mirrorLeftRight != 0) {
                if ((mdl::SkeletonScalar)model->currentJoints[mdl::TrackedJoint::LeftAnkle][1] !=
                    -999.0) {
                    const float inv = mdl::SkeletonScalar{1} /
                                      (mdl::SkeletonScalar)model->skeletonCalibration.heightScale;
                    bone->trans[0] =
                        (float)(inv * (mdl::SkeletonScalar)
                                          model->currentJoints[mdl::TrackedJoint::LeftAnkle][0]);
                    bone->trans[1] =
                        (float)((mdl::SkeletonScalar)
                                    model->currentJoints[mdl::TrackedJoint::LeftAnkle][1] *
                                inv);
                    bone->trans[2] =
                        (float)(inv * (mdl::SkeletonScalar)
                                          model->currentJoints[mdl::TrackedJoint::LeftAnkle][2]);
                    bone->trans[1] = model->skeletonCalibration.heightOffset + bone->trans[1];
                    bone->trans[0] = bone->trans[0] - model->skeletonCalibration.legIkXOffset;
                    bone->rotQuat[3] = model->standardPose.leftFoot[3];
                    bone->rotQuat[0] = model->standardPose.leftFoot[0];
                    bone->rotQuat[1] = model->standardPose.leftFoot[1];
                    bone->rotQuat[2] = model->standardPose.leftFoot[2];
                } else if (skeletonFlag != 0) {
                    bone->trans[0] = centerPos[0];
                    bone->trans[1] = centerPos[1];
                    bone->trans[2] = centerPos[2];
                }
            } else if ((mdl::SkeletonScalar)
                           model->currentJoints[mdl::TrackedJoint::RightAnkle][1] != -999.0) {
                const float inv = mdl::SkeletonScalar{1} /
                                  (mdl::SkeletonScalar)model->skeletonCalibration.heightScale;
                bone->trans[0] =
                    (float)(inv * (mdl::SkeletonScalar)
                                      model->currentJoints[mdl::TrackedJoint::RightAnkle][0]);
                bone->trans[1] =
                    (float)((mdl::SkeletonScalar)
                                model->currentJoints[mdl::TrackedJoint::RightAnkle][1] *
                            inv);
                bone->trans[2] =
                    (float)(inv * (mdl::SkeletonScalar)
                                      model->currentJoints[mdl::TrackedJoint::RightAnkle][2]);
                bone->trans[1] = model->skeletonCalibration.heightOffset + bone->trans[1];
                bone->trans[0] = model->skeletonCalibration.legIkXOffset + bone->trans[0];
                bone->trans[0] = -bone->trans[0];
                bone->rotQuat[3] = model->standardPose.rightFoot[3];
                bone->rotQuat[0] = model->standardPose.rightFoot[0];
                bone->rotQuat[1] = -model->standardPose.rightFoot[1];
                bone->rotQuat[2] = -model->standardPose.rightFoot[2];
            } else if (skeletonFlag != 0) {
                bone->trans[0] = centerPos[0];
                bone->trans[1] = centerPos[1];
                bone->trans[2] = centerPos[2];
            }
            if (model->poseTraceRecording != 0) {
                auto& sample = model->poseTraceBuffer[model->poseTraceSampleCount];
                std::memcpy(sample.leftLegIk.position, bone->trans, sizeof bone->trans);
                sample.leftLegIk.rotation.Store(bone->rotQuat);
            }
        } else if (std::memcmp(bone, kLegIkR, 9) == 0) {
            if (mirrorLeftRight != 0) {
                if ((mdl::SkeletonScalar)model->currentJoints[mdl::TrackedJoint::RightAnkle][1] !=
                    -999.0) {
                    const float inv = mdl::SkeletonScalar{1} /
                                      (mdl::SkeletonScalar)model->skeletonCalibration.heightScale;
                    bone->trans[0] =
                        (float)(inv * (mdl::SkeletonScalar)
                                          model->currentJoints[mdl::TrackedJoint::RightAnkle][0]);
                    bone->trans[1] =
                        (float)((mdl::SkeletonScalar)
                                    model->currentJoints[mdl::TrackedJoint::RightAnkle][1] *
                                inv);
                    bone->trans[2] =
                        (float)(inv * (mdl::SkeletonScalar)
                                          model->currentJoints[mdl::TrackedJoint::RightAnkle][2]);
                    bone->trans[1] = model->skeletonCalibration.heightOffset + bone->trans[1];
                    bone->trans[0] = model->skeletonCalibration.legIkXOffset + bone->trans[0];
                    bone->rotQuat[3] = model->standardPose.rightFoot[3];
                    bone->rotQuat[0] = model->standardPose.rightFoot[0];
                    bone->rotQuat[1] = model->standardPose.rightFoot[1];
                    bone->rotQuat[2] = model->standardPose.rightFoot[2];
                } else if (skeletonFlag != 0) {
                    bone->trans[0] = centerPos[0];
                    bone->trans[1] = centerPos[1];
                    bone->trans[2] = centerPos[2];
                }
            } else if ((mdl::SkeletonScalar)model->currentJoints[mdl::TrackedJoint::LeftAnkle][1] !=
                       -999.0) {
                const float inv = mdl::SkeletonScalar{1} /
                                  (mdl::SkeletonScalar)model->skeletonCalibration.heightScale;
                bone->trans[0] =
                    (float)(inv * (mdl::SkeletonScalar)
                                      model->currentJoints[mdl::TrackedJoint::LeftAnkle][0]);
                bone->trans[1] = (float)((mdl::SkeletonScalar)
                                             model->currentJoints[mdl::TrackedJoint::LeftAnkle][1] *
                                         inv);
                bone->trans[2] =
                    (float)(inv * (mdl::SkeletonScalar)
                                      model->currentJoints[mdl::TrackedJoint::LeftAnkle][2]);
                bone->trans[1] = model->skeletonCalibration.heightOffset + bone->trans[1];
                bone->trans[0] = bone->trans[0] - model->skeletonCalibration.legIkXOffset;
                bone->trans[0] = -bone->trans[0];
                bone->rotQuat[3] = model->standardPose.leftFoot[3];
                bone->rotQuat[0] = model->standardPose.leftFoot[0];
                bone->rotQuat[1] = -model->standardPose.leftFoot[1];
                bone->rotQuat[2] = -model->standardPose.leftFoot[2];
            } else if (skeletonFlag != 0) {
                bone->trans[0] = centerPos[0];
                bone->trans[1] = centerPos[1];
                bone->trans[2] = centerPos[2];
            }
            if (model->poseTraceRecording != 0) {
                auto& sample = model->poseTraceBuffer[model->poseTraceSampleCount];
                std::memcpy(sample.rightLegIk.position, bone->trans, sizeof bone->trans);
                sample.rightLegIk.rotation.Store(bone->rotQuat);
            }
        } else if (std::memcmp(bone, kWristL, 7) == 0) { // 0x52B80C
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::leftWrist,
                               model->standardPose.leftWrist[0], model->standardPose.leftWrist[1],
                               model->standardPose.leftWrist[2], model->standardPose.leftWrist[3]);
            else
                WritePoseGroup(
                    model, bone, &mdl::PoseTraceSample::leftWrist,
                    model->standardPose.rightWrist[0], -model->standardPose.rightWrist[1],
                    -model->standardPose.rightWrist[2], model->standardPose.rightWrist[3]);
        } else if (std::memcmp(bone, kWristR, 7) == 0) { // 0x52B7F4
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::rightWrist,
                               model->standardPose.rightWrist[0], model->standardPose.rightWrist[1],
                               model->standardPose.rightWrist[2],
                               model->standardPose.rightWrist[3]);
            else
                WritePoseGroup(model, bone, &mdl::PoseTraceSample::rightWrist,
                               model->standardPose.leftWrist[0], -model->standardPose.leftWrist[1],
                               -model->standardPose.leftWrist[2], model->standardPose.leftWrist[3]);
        } else if (std::memcmp(bone, kShoulderL, 5) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(
                    model, bone, &mdl::PoseTraceSample::leftShoulder,
                    model->standardPose.leftShoulder[0], model->standardPose.leftShoulder[1],
                    model->standardPose.leftShoulder[2], model->standardPose.leftShoulder[3]);
            else
                WritePoseGroup(
                    model, bone, &mdl::PoseTraceSample::leftShoulder,
                    model->standardPose.rightShoulder[0], -model->standardPose.rightShoulder[1],
                    -model->standardPose.rightShoulder[2], model->standardPose.rightShoulder[3]);
        } else if (std::memcmp(bone, kShoulderR, 5) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(
                    model, bone, &mdl::PoseTraceSample::rightShoulder,
                    model->standardPose.rightShoulder[0], model->standardPose.rightShoulder[1],
                    model->standardPose.rightShoulder[2], model->standardPose.rightShoulder[3]);
            else
                WritePoseGroup(
                    model, bone, &mdl::PoseTraceSample::rightShoulder,
                    model->standardPose.leftShoulder[0], -model->standardPose.leftShoulder[1],
                    -model->standardPose.leftShoulder[2], model->standardPose.leftShoulder[3]);
        }
    }
    if (model->poseTraceRecording != 0)
        ++model->poseTraceSampleCount; // 0x4B77C7
    return wasRecording;
}

} // namespace mikudancestudio
