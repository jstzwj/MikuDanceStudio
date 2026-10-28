// Wave-context reset and standard-skeleton quaternion calculations.
// Trace registration lives in src/model/pose_trace.cpp.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/wave_audio_context.hpp"
#include "mikudancestudio/panel_controls.hpp"

#include "../model/keyframe_common.hpp"

namespace mikudancestudio {
namespace {

using kfa::Rd32;
using kfa::RdI32;
using kfa::RdF32;
using kfa::Wr32;
using kfa::WrF32;

// Quaternion helpers share the required, statically imported D3DX runtime.
using FnQuatInverseLocal = float*(WINAPI*)(float out[4], const float q[4]);
using FnQuatRotationAxisLocal = float*(WINAPI*)(float out[4], const float axis[3], float angle);

FnQuatInverseLocal LocalQuatInverse() {
    return d3dx::Api::quatInverse;
}
FnQuatRotationAxisLocal LocalQuatRotationAxis() {
    return d3dx::Api::quatRotationAxis;
}

// .rdata float constants (bit-exact; Hex-Rays decimal renderings do not
// round-trip, e.g. flt_52B738 = 0x40490FD8 prints as "3.141592" but the
// float nearest 3.141592 is 0x40490FD7):
constexpr float kPiFloat = 3.141592f; // 0x40490FD8 (0x52B738)
// C++17 has no std::bit_cast; MSVC's __builtin_bit_cast is accepted in
// constant expressions, so the static_assert pins the literal above to
// the original .rdata bit pattern.
static_assert(__builtin_bit_cast(std::uint32_t, kPiFloat) == 0x40490FD8u, "flt_52B738 bit-exact");
constexpr float k35deg5311A4 = 0.61086512f; // 0x3F1C61A8 (0x5311A4)
constexpr float k30deg53119C = 0.52359867f; // 0x3F060A90 (0x53119C)
constexpr float kNeg0d7 = -0.69999999f;     // 0xBF333333 (0x531190)
constexpr float kNeg0d3 = -0.30000001f;     // 0xBE99999A (0x531198)
constexpr float k75f = 75.0f;               // 0x42960000 (0x531194)
constexpr float kSentinel = -999.0;         // dbl 0x5311B0 (x87 compares
                                            // float mem against the double)

} // namespace

// ---------------------------------------------------------------------------
// VA 0x004C2430 - ClearWaveContextFields(this): clear five fields of the 0x25C wave/
// timeline context object.  Called from the WinMain init path 0x47A5B0
// (0x47A665) right after the zeroing ctor 0x4C2450; the array pointers are
// nulled WITHOUT freeing (leak-preserving, matching the original).
// __thiscall(ctx) -> returns ctx.
// ---------------------------------------------------------------------------
void* ClearWaveContextFields(void* obj) {
    auto* audio = static_cast<WaveAudioContext*>(obj);
    audio->path[0] = L'\0';       // 0x4C2434
    audio->englishUI = 0;         // 0x4C2438
    audio->waveformMax = nullptr; // 0x4C243E
    audio->waveformMin = nullptr; // 0x4C2440
    audio->volume = 0;            // 0x4C2443
    return audio;                 // 0x4C2449
}

// ---------------------------------------------------------------------------
// VA 0x004A60F0 - BuildLookAtQuaternion(out, quat, ax, ay, az, bx, by, bz,
// mode):
// look-at quaternion builder.  Rotates the (b - a) direction by
// inverse(quat) * RotY(pi), normalizes it (mode 5 zeroes Y first), then
// constructs a yaw/roll rotation for the chosen axis mode (0..5) and
// returns the quaternion with its Y component negated.
// __stdcall; `out` is the return value (4 floats).  Sole caller: 0x4A6520
// (10 call sites).
// ---------------------------------------------------------------------------
float* BuildLookAtQuaternion(float out[4], const float quat[4], float ax, float ay, float az,
                             float bx, float by, float bz, unsigned char mode) {
    auto* d3 = &d3dx::Get();
    FnQuatInverseLocal quatInverse = LocalQuatInverse();

    float qInv[4];
    quatInverse(qInv, quat); // 0x4A6109
    d3dx::D3DXMATRIXF m1;
    d3->matrixRotationQuaternion(&m1, qInv); // 0x4A611B
    d3dx::D3DXMATRIXF m2;
    d3->rotY(&m2, kPiFloat); // 0x4A612F
    d3dx::D3DXMATRIXF m3;
    d3->multiply(&m3, &m1, &m2); // 0x4A6146

    float dir[3] = {bx - ax, by - ay, bz - az}; // 0x4A616C..
    float t[4];
    d3->vec3Transform(t, dir, &m3); // 0x4A61C3

    float n[3] = {t[0], mode == 5 ? 0.0f : t[1], t[2]}; // 0x4A61D9..
    d3->vec3Normalize(n, n);                            // 0x4A6201

    const float len = sqrtf(n[2] * n[2] + n[0] * n[0]); // 0x4A6216 (v46)
    float c;                                            // v40
    float s;                                            // v44
    float ang = 0.0f;                                   // v42

    if (mode == 5) {
        c = -n[2] / len; // 0x4A6244
        s = n[0] / len;  // 0x4A624C
    } else {
        c = n[0] / len; // 0x4A6266
        s = n[2] / len; // 0x4A6270
    }
    if (mode == 1) {              // 0x4A6274
        const float a = acosf(c); // 0x4A6285
        ang = s < 0.0f ? -a : a;  // v15 = 0, v14 = v46 kept
    } else if (mode >= 2) {       // mode 0: v15 = 0         // 0x4A6316
        // modes 2/3/4/5: two-term angle decomposition with the x87
        // flag-sensitive sign selection (fnstsw parity + C0/C3 tests at
        // 0x4A633F..0x4A6349 map to ordered "< 0" tests; NaN falls to the
        // ">= 0" arm).
        const float ang0 = acosf(c); // 0x4A6323
        float r;
        if (s < 0.0f) {
            r = !(c < 0.0f) ? -acosf(-s) : acosf(-s); // 0x4A634D/5C
            ang = -ang0 - r * len;                    // 0x4A6385
        } else {
            r = !(c < 0.0f) ? -acosf(s) : acosf(s); // 0x4A6395/A4
            ang = ang0 - r * len;                   // 0x4A63C9
        }
    }
    {
        // first 2D rotation from (c, s) (0x4A62AE..)
        d3dx::D3DXMATRIXF& a = m1; // reused v53
        std::memset(&a, 0, sizeof(a));
        a.m[0][0] = c;    // 0x4A62E6
        a.m[0][2] = -s;   // 0x4A62F2
        a.m[2][0] = s;    // 0x4A62F6
        a.m[2][2] = c;    // 0x4A62FA
        a.m[1][1] = 1.0f; // 0x4A62DE
        a.m[3][3] = 1.0f; // 0x4A62DA
    }
    float e1, e2;    // v41, v45
    if (mode != 0) { // 0x4A62FE
        e1 = -n[1];  // 0x4A63DA
        e2 = len;    // 0x4A63DE
    } else {
        e1 = len;  // 0x4A6304
        e2 = n[1]; // 0x4A630A
    }
    {
        d3dx::D3DXMATRIXF& b = m2; // reused v54
        std::memset(&b, 0, sizeof(b));
        b.m[0][0] = e1;   // 0x4A643F
        b.m[0][1] = -e2;  // 0x4A644B
        b.m[1][0] = e2;   // 0x4A644F
        b.m[1][1] = e1;   // 0x4A6456
        b.m[2][2] = 1.0f; // 0x4A6434
        b.m[3][3] = 1.0f; // 0x4A642D
    }
    d3dx::D3DXMATRIXF r;                 // v57
    if (mode < 4) {                      // 0x4A645D
        d3->multiply(&m3, &m1, &m2);     // 0x4A6477
        std::memcpy(&r, &m3, sizeof(r)); // 0x4A648F
    } else {
        std::memcpy(&r, &m1, sizeof(r)); // 0x4A645F
    }
    if (mode == 1 || mode == 2 || mode == 3) { // 0x4A6493
        d3->rotY(&m1, -ang);                   // 0x4A64BE
        d3->multiply(&m3, &r, &m1);            // 0x4A64D8
        std::memcpy(&r, &m3, sizeof(r));       // 0x4A64F0
    }
    d3->quatFromMatrix(out, &r); // 0x4A6502
    out[1] = -out[1];            // 0x4A650D
    return out;                  // 0x4A6512
}

// ---------------------------------------------------------------------------
// VA 0x004A6520 - InitStandardSkeletonQuats(model, flag):
// standard-skeleton base-quaternion
// initializer.  Using the standard-bone probe positions cached at
// model+14284..14568 (-999.0 = absent), builds the rest orientation
// quaternions for the upper body (model+64), neck (+80), arm chains
// (+96..+188), lower body (+192) and leg chains (+208..+300), plus the two
// shoulder mid rotations (+304/+320) and the leg length cache (+14568).
// `flag` (a2): when a probe group is absent, initialize that quaternion to
// identity instead of leaving it (some blocks do this unconditionally -
// preserved).  Original returns an undefined eax; ported as void.
// __thiscall(model).  Sole caller: 0x4B5760.
// ---------------------------------------------------------------------------
void InitStandardSkeletonQuats(mdl::ModelRecord* m, unsigned char flag) {
    auto* d3 = &d3dx::Get();
    FnQuatRotationAxisLocal quatRotationAxis = LocalQuatRotationAxis();

    auto& state = *m;
    auto& joints = state.currentJoints;
    auto& pose = state.standardPose;
    const auto normalizeComponents = [d3](float& x, float& y, float& z) {
        float value[3] = {x, y, z};
        d3->vec3Normalize(value, value);
        x = value[0];
        y = value[1];
        z = value[2];
    };
    float qByVal[4]; // by-value quaternion reads (original passes 4 dwords)
    // The arm twist blocks rotate about (0, 0, 1) - the v139/v140 register
    // trio always carries (0, 0, 1) at those call sites.
    const float kAxisZ[3] = {0.0f, 0.0f, 1.0f};

    // scratch quats (v144/v148/v132-style stack temps)
    float qA[4], qB[4], t1[4], t2[4];

    // q150/q155/q159 persist across blocks; the original leaves them as
    // uninitialized stack when producer blocks are skipped - zeroed here
    // (deterministic deviation, consumed only on data-dependent paths).
    float q150[4] = {}, q155[4] = {}, q159[4] = {};

    // ---- leg length cache (0x4A6543..0x4A6814) ---------------------------
    if (state.skeletonCalibration.legLength == -1.0f) {
        if (joints[mdl::TrackedJoint::RightKnee][1] != kSentinel &&
            joints[mdl::TrackedJoint::RightAnkle][1] != kSentinel &&
            joints[mdl::TrackedJoint::RightHip][1] != kSentinel) {
            const float d1x =
                joints[mdl::TrackedJoint::RightAnkle][0] - joints[mdl::TrackedJoint::RightKnee][0];
            const float d1y =
                joints[mdl::TrackedJoint::RightAnkle][1] - joints[mdl::TrackedJoint::RightKnee][1];
            const float d1z =
                joints[mdl::TrackedJoint::RightAnkle][2] - joints[mdl::TrackedJoint::RightKnee][2];
            const float d2x =
                joints[mdl::TrackedJoint::RightKnee][0] - joints[mdl::TrackedJoint::RightHip][0];
            const float d2y =
                joints[mdl::TrackedJoint::RightKnee][1] - joints[mdl::TrackedJoint::RightHip][1];
            const float d2z =
                joints[mdl::TrackedJoint::RightKnee][2] - joints[mdl::TrackedJoint::RightHip][2];
            const float len1 = sqrtf(d1y * d1y + d1x * d1x + d1z * d1z); // 0x4A6661
            const float len2 = sqrtf(d2x * d2x + d2y * d2y + d2z * d2z); // 0x4A6690
            state.skeletonCalibration.legLength = len2 + len1;           // 0x4A680E
        } else if (joints[mdl::TrackedJoint::RightKnee][1] != kSentinel &&
                   joints[mdl::TrackedJoint::LeftAnkle][1] != kSentinel &&
                   joints[mdl::TrackedJoint::LeftHip][1] != kSentinel) {
            const float d1x =
                joints[mdl::TrackedJoint::LeftAnkle][0] - joints[mdl::TrackedJoint::LeftKnee][0];
            const float d1y =
                joints[mdl::TrackedJoint::LeftAnkle][1] - joints[mdl::TrackedJoint::LeftKnee][1];
            const float d1z =
                joints[mdl::TrackedJoint::LeftAnkle][2] - joints[mdl::TrackedJoint::LeftKnee][2];
            const float d2x =
                joints[mdl::TrackedJoint::LeftKnee][0] - joints[mdl::TrackedJoint::LeftHip][0];
            const float d2y =
                joints[mdl::TrackedJoint::LeftKnee][1] - joints[mdl::TrackedJoint::LeftHip][1];
            const float d2z =
                joints[mdl::TrackedJoint::LeftKnee][2] - joints[mdl::TrackedJoint::LeftHip][2];
            const float lenA = sqrtf(d1y * d1y + d1x * d1x + d1z * d1z); // 0x4A67C6
            const float lenB = sqrtf(d2x * d2x + d2y * d2y + d2z * d2z); // 0x4A67F5
            state.skeletonCalibration.legLength = lenB + lenA;           // 0x4A680E
        }
    }

    // ---- head / upper-body base quat, model+64 (0x4A6827..0x4A6A5E) ------
    if (joints[mdl::TrackedJoint::Torso][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightShoulder][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftShoulder][1] != kSentinel &&
        joints[mdl::TrackedJoint::Neck][1] != kSentinel) {
        float dx =
            joints[mdl::TrackedJoint::Neck][0] - joints[mdl::TrackedJoint::Torso][0]; // 0x4A68AB
        float dy =
            joints[mdl::TrackedJoint::Neck][1] - joints[mdl::TrackedJoint::Torso][1]; // 0x4A68C8
        float dz =
            joints[mdl::TrackedJoint::Neck][2] - joints[mdl::TrackedJoint::Torso][2]; // 0x4A68E3
        normalizeComponents(dx, dy, dz);                                              // 0x4A68EF
        float cx = dz * 1.0f - dy * 0.0f;                                             // 0x4A6914
        float cy = dx * 0.0f - dz * 0.0f;                                             // 0x4A6935
        float cz = dy * 0.0f - dx * 1.0f;                                             // 0x4A6950
        const float dot = 1.0f * dy + dx * 0.0f + 0.0f * dz;                          // 0x4A696C
        float axis[3] = {cx, cy, cz};
        d3->vec3Normalize(axis, axis);   // 0x4A6970
        const float ang = acosf(dot);    // 0x4A697E
        quatRotationAxis(qA, axis, ang); // 0x4A6994
        std::memcpy(qByVal, pose.upperBody, sizeof qByVal);
        BuildLookAtQuaternion(q150, qByVal, joints[mdl::TrackedJoint::LeftShoulder][0],
                              joints[mdl::TrackedJoint::LeftShoulder][1],
                              joints[mdl::TrackedJoint::LeftShoulder][2],
                              joints[mdl::TrackedJoint::RightShoulder][0],
                              joints[mdl::TrackedJoint::RightShoulder][1],
                              joints[mdl::TrackedJoint::RightShoulder][2], 4); // 0x4A6A06
        std::memcpy(pose.upperBody, q150, 16);                                 // 0x4A6A0D..
        d3->quatMultiply(t1, pose.upperBody, qA);                              // 0x4A6A30
        std::memcpy(pose.upperBody, t1, 16);                                   // 0x4A6A4B..
    } else if (flag != 0) {
        pose.upperBody[3] = 1.0f; // 0x4A6A60
        pose.upperBody[0] = 0.0f; // 0x4A6A65
        pose.upperBody[1] = 0.0f; // 0x4A6A68
        pose.upperBody[2] = 0.0f; // 0x4A6A6B
    }

    // ---- neck quat, model+80 (0x4A6A7F..0x4A6C60) ------------------------
    if (joints[mdl::TrackedJoint::Center][1] != kSentinel &&
        joints[mdl::TrackedJoint::Head][1] != kSentinel &&
        joints[mdl::TrackedJoint::Neck][1] != kSentinel) {
        std::memcpy(qByVal, pose.upperBody, sizeof qByVal);
        BuildLookAtQuaternion(
            t1, qByVal, joints[mdl::TrackedJoint::Head][0], joints[mdl::TrackedJoint::Head][1],
            joints[mdl::TrackedJoint::Head][2], joints[mdl::TrackedJoint::Neck][0],
            joints[mdl::TrackedJoint::Neck][1], joints[mdl::TrackedJoint::Neck][2], 1); // 0x4A6B33
        std::memcpy(pose.neck, t1, 16);                                 // 0x4A6B4B..
        if (joints[mdl::TrackedJoint::HeadDirection][1] != kSentinel) { // 0x4A6B49
            d3->quatMultiply(qB, pose.neck, pose.upperBody);            // 0x4A6B75
            BuildLookAtQuaternion(qA, qB, joints[mdl::TrackedJoint::HeadDirection][0],
                                  joints[mdl::TrackedJoint::HeadDirection][1],
                                  joints[mdl::TrackedJoint::HeadDirection][2],
                                  joints[mdl::TrackedJoint::Head][0],
                                  joints[mdl::TrackedJoint::Head][1],
                                  joints[mdl::TrackedJoint::Head][2], 5); // 0x4A6BE4
            d3->quatMultiply(t1, qA, pose.neck);                          // 0x4A6C0F
            std::memcpy(pose.neck, t1, 16);                               // 0x4A6C2C..
        }
    } else {
        // unconditional identity (v32/v36 are constant 0/1 here)
        pose.neck[3] = 1.0f; // 0x4A6C55
        pose.neck[0] = 0.0f; // 0x4A6C5A
        pose.neck[1] = 0.0f; // 0x4A6C5D
        pose.neck[2] = 0.0f; // 0x4A6C60
    }

    // ---- shoulder mid quat #1, model+320 (0x4A6C4A..0x4A6E2C) -----------
    if (joints[mdl::TrackedJoint::LeftShoulder][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightClavicle][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightShoulder][1] != kSentinel) {
        const float mx = (joints[mdl::TrackedJoint::RightShoulder][0] +
                          joints[mdl::TrackedJoint::LeftShoulder][0]) *
                         0.5f; // 0x4A6CE9
        const float my = (joints[mdl::TrackedJoint::LeftShoulder][1] +
                          joints[mdl::TrackedJoint::RightShoulder][1]) *
                         0.5f; // 0x4A6CCF..
        const float mz = (joints[mdl::TrackedJoint::LeftShoulder][2] +
                          joints[mdl::TrackedJoint::RightShoulder][2]) *
                         0.5f; // 0x4A6CFB
        float ax = joints[mdl::TrackedJoint::RightShoulder][0] - mx,
              ay = joints[mdl::TrackedJoint::RightShoulder][1] - my,
              az = joints[mdl::TrackedJoint::RightShoulder][2] - mz; // 0x4A6D0F..
        float bx = joints[mdl::TrackedJoint::RightClavicle][0] - mx,
              by = joints[mdl::TrackedJoint::RightClavicle][1] - my,
              bz = joints[mdl::TrackedJoint::RightClavicle][2] - mz; // 0x4A6D5D..
        normalizeComponents(ax, ay, az);                             // 0x4A6D95
        normalizeComponents(bx, by, bz);                             // 0x4A6DA2
        float cx = bz * ay - by * az;                                // 0x4A6DC7
        float cy = bx * az - bz * ax;                                // 0x4A6DE8
        float cz = by * ax - bx * ay;                                // 0x4A6E03
        const float dot = ay * by + bx * ax + az * bz;               // 0x4A6E1F
        float axis[3] = {cx, cy, cz};
        d3->vec3Normalize(axis, axis);                          // 0x4A6E23
        quatRotationAxis(pose.rightShoulder, axis, acosf(dot)); // 0x4A6E49
    } else {
        pose.rightShoulder[3] = 1.0f; // 0x4A6E64
        pose.rightShoulder[0] = 0.0f; // 0x4A6E6C
        pose.rightShoulder[1] = 0.0f; // 0x4A6E6F
        pose.rightShoulder[2] = 0.0f; // 0x4A6E75
    }

    // ---- upper arm quat, model+144 (0x4A6E9C..0x4A7079) ------------------
    if (joints[mdl::TrackedJoint::RightShoulder][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightElbow][1] != kSentinel &&
        joints[mdl::TrackedJoint::Neck][1] != kSentinel) {
        quatRotationAxis(qA, kAxisZ, -k35deg5311A4);                    // 0x4A6EFB
        if (joints[mdl::TrackedJoint::RightClavicle][1] == kSentinel) { // 0x4A6F11
            std::memcpy(qByVal, pose.upperBody, sizeof qByVal);
            BuildLookAtQuaternion(q150, qByVal, joints[mdl::TrackedJoint::RightShoulder][0],
                                  joints[mdl::TrackedJoint::RightShoulder][1],
                                  joints[mdl::TrackedJoint::RightShoulder][2],
                                  joints[mdl::TrackedJoint::RightElbow][0],
                                  joints[mdl::TrackedJoint::RightElbow][1],
                                  joints[mdl::TrackedJoint::RightElbow][2], 0); // 0x4A6FF2
        } else {
            d3->quatMultiply(qB, pose.rightShoulder, pose.upperBody); // 0x4A6F1D
            BuildLookAtQuaternion(q150, qB, joints[mdl::TrackedJoint::RightClavicle][0],
                                  joints[mdl::TrackedJoint::RightClavicle][1],
                                  joints[mdl::TrackedJoint::RightClavicle][2],
                                  joints[mdl::TrackedJoint::RightElbow][0],
                                  joints[mdl::TrackedJoint::RightElbow][1],
                                  joints[mdl::TrackedJoint::RightElbow][2], 0); // 0x4A6F8A
        }
        d3->quatMultiply(pose.rightArm, qA, q150);  // 0x4A7023
    } else if (flag != 0) {                         // 0x4A702C
        quatRotationAxis(qA, kAxisZ, k35deg5311A4); // 0x4A7048
        std::memcpy(pose.rightArm, qA, 16);         // 0x4A7059..
    }

    // ---- forearm quat, model+176 (0x4A7090..0x4A7211) --------------------
    if (joints[mdl::TrackedJoint::RightElbow][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightWrist][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightShoulder][1] != kSentinel) {
        quatRotationAxis(qA, kAxisZ, -k30deg53119C);    // 0x4A70EF
        d3->quatMultiply(t1, q150, pose.rightShoulder); // 0x4A70FF
        d3->quatMultiply(t2, t1, pose.upperBody);       // 0x4A7112
        BuildLookAtQuaternion(
            q155, t2, joints[mdl::TrackedJoint::RightElbow][0],
            joints[mdl::TrackedJoint::RightElbow][1], joints[mdl::TrackedJoint::RightElbow][2],
            joints[mdl::TrackedJoint::RightWrist][0], joints[mdl::TrackedJoint::RightWrist][1],
            joints[mdl::TrackedJoint::RightWrist][2], 0);       // 0x4A7181
        d3->quatMultiply(pose.rightElbow, qA, q155);            // 0x4A71C1
        quatRotationAxis(qA, kAxisZ, k30deg53119C);             // 0x4A71DA
        d3->quatMultiply(pose.rightElbow, pose.rightElbow, qA); // 0x4A71E6
    } else if (flag != 0) {                                     // 0x4A7211
        pose.rightElbow[3] = 1.0f;                              // 0x4A7213
        pose.rightElbow[0] = 0.0f;                              // 0x4A721B
        pose.rightElbow[1] = 0.0f;                              // 0x4A7221
        pose.rightElbow[2] = 0.0f;                              // 0x4A7227
    }

    // ---- wrist quat, model+160 (0x4A7206..0x4A743E) ----------------------
    if (joints[mdl::TrackedJoint::RightWrist][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightHand][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightElbow][1] != kSentinel) {
        if (joints[mdl::TrackedJoint::RightHand][0] == joints[mdl::TrackedJoint::RightWrist][0] &&
            joints[mdl::TrackedJoint::RightHand][1] == joints[mdl::TrackedJoint::RightWrist][1] &&
            joints[mdl::TrackedJoint::RightHand][2] ==
                joints[mdl::TrackedJoint::RightWrist][2]) { // 0x4A72A8
            pose.rightWrist[3] = 1.0f;                      // 0x4A72AA
            pose.rightWrist[0] = 0.0f;                      // 0x4A72B2
            pose.rightWrist[1] = 0.0f;                      // 0x4A72B8
            pose.rightWrist[2] = 0.0f;                      // 0x4A72BE
        } else {
            quatRotationAxis(qA, kAxisZ, -k30deg53119C);  // 0x4A72E3
            d3->quatMultiply(t1, q155, q150);             // 0x4A72FA
            d3->quatMultiply(t2, t1, pose.rightShoulder); // 0x4A730D
            d3->quatMultiply(qB, t2, pose.upperBody);     // 0x4A7323
            BuildLookAtQuaternion(
                q159, qB, joints[mdl::TrackedJoint::RightWrist][0],
                joints[mdl::TrackedJoint::RightWrist][1], joints[mdl::TrackedJoint::RightWrist][2],
                joints[mdl::TrackedJoint::RightHand][0], joints[mdl::TrackedJoint::RightHand][1],
                joints[mdl::TrackedJoint::RightHand][2], 0);        // 0x4A7392
            d3->quatMultiply(pose.rightWrist, qA, q159);            // 0x4A73D2
            quatRotationAxis(qA, kAxisZ, k30deg53119C);             // 0x4A73EB
            d3->quatMultiply(pose.rightWrist, pose.rightWrist, qA); // 0x4A73F7
        }
    } else if (flag != 0) {        // 0x4A7420
        pose.rightWrist[3] = 1.0f; // 0x4A7422
        pose.rightWrist[0] = 0.0f; // 0x4A742A
        pose.rightWrist[1] = 0.0f; // 0x4A7430
        pose.rightWrist[2] = 0.0f; // 0x4A7436
    }

    // ---- shoulder mid quat #2, model+304 (0x4A7415..0x4A7668) -----------
    if (joints[mdl::TrackedJoint::LeftShoulder][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftClavicle][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightShoulder][1] != kSentinel) {
        const float mx = (joints[mdl::TrackedJoint::RightShoulder][0] +
                          joints[mdl::TrackedJoint::LeftShoulder][0]) *
                         0.5f; // 0x4A7490
        const float my = (joints[mdl::TrackedJoint::LeftShoulder][1] +
                          joints[mdl::TrackedJoint::RightShoulder][1]) *
                         0.5f; // 0x4A74A0
        const float mz = (joints[mdl::TrackedJoint::LeftShoulder][2] +
                          joints[mdl::TrackedJoint::RightShoulder][2]) *
                         0.5f; // 0x4A74B0
        float ax = joints[mdl::TrackedJoint::LeftShoulder][0] - mx,
              ay = joints[mdl::TrackedJoint::LeftShoulder][1] - my,
              az = joints[mdl::TrackedJoint::LeftShoulder][2] - mz; // 0x4A74E8..
        float bx = joints[mdl::TrackedJoint::LeftClavicle][0] - mx,
              by = joints[mdl::TrackedJoint::LeftClavicle][1] - my,
              bz = joints[mdl::TrackedJoint::LeftClavicle][2] - mz; // 0x4A7536..
        normalizeComponents(ax, ay, az);                            // 0x4A756E
        normalizeComponents(bx, by, bz);                            // 0x4A757B
        float cx = bz * ay - by * az;                               // 0x4A75A0
        float cy = bx * az - bz * ax;                               // 0x4A75C1
        float cz = by * ax - bx * ay;                               // 0x4A75DC
        const float dot = ay * by + bx * ax + az * bz;              // 0x4A75F8
        float axis[3] = {cx, cy, cz};
        d3->vec3Normalize(axis, axis);                         // 0x4A75FC
        quatRotationAxis(pose.leftShoulder, axis, acosf(dot)); // 0x4A7622
    } else {
        pose.leftShoulder[3] = 1.0f; // 0x4A765F
        pose.leftShoulder[0] = 0.0f; // 0x4A7667
        pose.leftShoulder[1] = 0.0f; // 0x4A766A
        pose.leftShoulder[2] = 0.0f; // 0x4A7670
    }

    // ---- lower arm quat, model+96 (0x4A764E..0x4A7835) -------------------
    if (joints[mdl::TrackedJoint::LeftShoulder][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftElbow][1] != kSentinel &&
        joints[mdl::TrackedJoint::Neck][1] != kSentinel) {
        quatRotationAxis(qA, kAxisZ, k35deg5311A4);                    // 0x4A76CE
        if (joints[mdl::TrackedJoint::LeftClavicle][1] == kSentinel) { // 0x4A76E4
            std::memcpy(qByVal, pose.upperBody, sizeof qByVal);
            BuildLookAtQuaternion(q150, qByVal, joints[mdl::TrackedJoint::LeftElbow][0],
                                  joints[mdl::TrackedJoint::LeftElbow][1],
                                  joints[mdl::TrackedJoint::LeftElbow][2],
                                  joints[mdl::TrackedJoint::LeftShoulder][0],
                                  joints[mdl::TrackedJoint::LeftShoulder][1],
                                  joints[mdl::TrackedJoint::LeftShoulder][2], 0); // 0x4A77B7
        } else {
            d3->quatMultiply(qB, pose.leftShoulder, pose.upperBody); // 0x4A76F0
            BuildLookAtQuaternion(q150, qB, joints[mdl::TrackedJoint::LeftElbow][0],
                                  joints[mdl::TrackedJoint::LeftElbow][1],
                                  joints[mdl::TrackedJoint::LeftElbow][2],
                                  joints[mdl::TrackedJoint::LeftClavicle][0],
                                  joints[mdl::TrackedJoint::LeftClavicle][1],
                                  joints[mdl::TrackedJoint::LeftClavicle][2], 0); // 0x4A774F
        }
        d3->quatMultiply(pose.leftArm, qA, q150);    // 0x4A77E5
    } else if (flag != 0) {                          // 0x4A77EE
        quatRotationAxis(qA, kAxisZ, -k35deg5311A4); // 0x4A780A
        std::memcpy(pose.leftArm, qA, 16);           // 0x4A781B..
    }

    // ---- lower forearm quat, model+128 (0x4A7846..0x4A79CD) --------------
    if (joints[mdl::TrackedJoint::LeftElbow][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftWrist][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftShoulder][1] != kSentinel) {
        quatRotationAxis(qA, kAxisZ, k30deg53119C);    // 0x4A78A5
        d3->quatMultiply(t1, q150, pose.leftShoulder); // 0x4A78B8
        d3->quatMultiply(t2, t1, pose.upperBody);      // 0x4A78CE
        BuildLookAtQuaternion(
            q155, t2, joints[mdl::TrackedJoint::LeftWrist][0],
            joints[mdl::TrackedJoint::LeftWrist][1], joints[mdl::TrackedJoint::LeftWrist][2],
            joints[mdl::TrackedJoint::LeftElbow][0], joints[mdl::TrackedJoint::LeftElbow][1],
            joints[mdl::TrackedJoint::LeftElbow][2], 0);      // 0x4A793D
        d3->quatMultiply(pose.leftElbow, qA, q155);           // 0x4A797D
        quatRotationAxis(qA, kAxisZ, -k30deg53119C);          // 0x4A7996
        d3->quatMultiply(pose.leftElbow, pose.leftElbow, qA); // 0x4A79A2
    } else if (flag != 0) {                                   // 0x4A79CD
        pose.leftElbow[3] = 1.0f;                             // 0x4A79CF
        pose.leftElbow[0] = 0.0f;                             // 0x4A79D7
        pose.leftElbow[1] = 0.0f;                             // 0x4A79DD
        pose.leftElbow[2] = 0.0f;                             // 0x4A79E3
    }

    // ---- lower wrist quat, model+112 (0x4A79C2..0x4A7C21) ----------------
    if (joints[mdl::TrackedJoint::LeftWrist][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftHand][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftElbow][1] != kSentinel) {
        if (joints[mdl::TrackedJoint::LeftHand][0] == joints[mdl::TrackedJoint::LeftWrist][0] &&
            joints[mdl::TrackedJoint::LeftHand][1] == joints[mdl::TrackedJoint::LeftWrist][1] &&
            joints[mdl::TrackedJoint::LeftHand][2] ==
                joints[mdl::TrackedJoint::LeftWrist][2]) { // 0x4A7A64
            pose.leftWrist[3] = 1.0f;                      // 0x4A7A66
            pose.leftWrist[0] = 0.0f;                      // 0x4A7A6B
            pose.leftWrist[1] = 0.0f;                      // 0x4A7A6E
            pose.leftWrist[2] = 0.0f;                      // 0x4A7A71
        } else {
            quatRotationAxis(qA, kAxisZ, k30deg53119C);  // 0x4A7A93
            d3->quatMultiply(t1, q155, q150);            // 0x4A7AAD
            d3->quatMultiply(t2, t1, pose.leftShoulder); // 0x4A7AC0
            d3->quatMultiply(qB, t2, pose.upperBody);    // 0x4A7AD3
            BuildLookAtQuaternion(
                q159, qB, joints[mdl::TrackedJoint::LeftHand][0],
                joints[mdl::TrackedJoint::LeftHand][1], joints[mdl::TrackedJoint::LeftHand][2],
                joints[mdl::TrackedJoint::LeftWrist][0], joints[mdl::TrackedJoint::LeftWrist][1],
                joints[mdl::TrackedJoint::LeftWrist][2], 0);      // 0x4A7B42
            d3->quatMultiply(pose.leftWrist, qA, q159);           // 0x4A7B7F
            quatRotationAxis(qA, kAxisZ, -k30deg53119C);          // 0x4A7B98
            d3->quatMultiply(pose.leftWrist, pose.leftWrist, qA); // 0x4A7BA4
        }
    } else if (flag != 0) {       // 0x4A7C21
        pose.leftWrist[3] = 1.0f; // 0x4A7C23
        pose.leftWrist[0] = 0.0f; // 0x4A7C28
        pose.leftWrist[1] = 0.0f; // 0x4A7C2B
        pose.leftWrist[2] = 0.0f; // 0x4A7C2E
    }

    // ---- lower body base quat, model+192 (0x4A7BC1..0x4A7E7B) -----------
    const float lmx =
        (joints[mdl::TrackedJoint::LeftHip][0] + joints[mdl::TrackedJoint::RightHip][0]) *
        0.5f; // 0x4A7BC1
    const float lmy =
        (joints[mdl::TrackedJoint::LeftHip][1] + joints[mdl::TrackedJoint::RightHip][1]) *
        0.5f; // 0x4A7BD1
    const float lmz =
        (joints[mdl::TrackedJoint::LeftHip][2] + joints[mdl::TrackedJoint::RightHip][2]) *
        0.5f; // 0x4A7BE1
    if (joints[mdl::TrackedJoint::Torso][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightHip][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftHip][1] != kSentinel && lmy != kSentinel) { // 0x4A7C16
        float ax = joints[mdl::TrackedJoint::Torso][0] - lmx,
              ay = joints[mdl::TrackedJoint::Torso][1] - lmy,
              az = joints[mdl::TrackedJoint::Torso][2] - lmz; // 0x4A7CAA..
        normalizeComponents(ax, ay, az);                      // 0x4A7CE6
        float cx = az * 1.0f - ay * 0.0f;                     // 0x4A7D0B
        float cy = ax * 0.0f - az * 0.0f;                     // 0x4A7D2C
        float cz = ay * 0.0f - ax * 1.0f;                     // 0x4A7D47
        const float dot = 1.0f * ay + ax * 0.0f + 0.0f * az;  // 0x4A7D63
        float axis[3] = {cx, cy, cz};
        d3->vec3Normalize(axis, axis);   // 0x4A7D67
        const float ang = acosf(dot);    // 0x4A7D75
        quatRotationAxis(qA, axis, ang); // 0x4A7D8B
        BuildLookAtQuaternion(
            t1, qA, joints[mdl::TrackedJoint::LeftHip][0], joints[mdl::TrackedJoint::LeftHip][1],
            joints[mdl::TrackedJoint::LeftHip][2], joints[mdl::TrackedJoint::RightHip][0],
            joints[mdl::TrackedJoint::RightHip][1], joints[mdl::TrackedJoint::RightHip][2],
            4);                                   // 0x4A7DFD
        std::memcpy(pose.lowerBody, t1, 16);      // 0x4A7E04..
        d3->quatMultiply(t2, pose.lowerBody, qA); // 0x4A7E2D
        std::memcpy(pose.lowerBody, t2, 16);      // 0x4A7E48..
    } else if (flag != 0) {
        pose.lowerBody[3] = 1.0f; // 0x4A7E61
        pose.lowerBody[0] = 0.0f; // 0x4A7E69
        pose.lowerBody[1] = 0.0f; // 0x4A7E6F
        pose.lowerBody[2] = 0.0f; // 0x4A7E75
    }

    // ---- hip quat, model+256 (0x4A7E8A..0x4A7F7D) ------------------------
    if (joints[mdl::TrackedJoint::RightHip][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightKnee][1] != kSentinel &&
        joints[mdl::TrackedJoint::Center][1] != kSentinel) {
        std::memcpy(qByVal, pose.lowerBody, sizeof qByVal);
        BuildLookAtQuaternion(
            t1, qByVal, joints[mdl::TrackedJoint::RightHip][0],
            joints[mdl::TrackedJoint::RightHip][1], joints[mdl::TrackedJoint::RightHip][2],
            joints[mdl::TrackedJoint::RightKnee][0], joints[mdl::TrackedJoint::RightKnee][1],
            joints[mdl::TrackedJoint::RightKnee][2], 2); // 0x4A7F47
        std::memcpy(pose.rightLeg, t1, 16);              // 0x4A7F58..
    } else if (flag != 0) {                              // 0x4A7F7D
        pose.rightLeg[3] = 1.0f;                         // 0x4A7F81
        pose.rightLeg[0] = 0.0f;                         // 0x4A7F89
        pose.rightLeg[1] = 0.0f;                         // 0x4A7F8F
        pose.rightLeg[2] = 0.0f;                         // 0x4A7F95
    }

    // ---- other hip quat, model+208 (0x4A7FAC..0x4A809F) ------------------
    if (joints[mdl::TrackedJoint::LeftHip][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftKnee][1] != kSentinel &&
        joints[mdl::TrackedJoint::Center][1] != kSentinel) {
        std::memcpy(qByVal, pose.lowerBody, sizeof qByVal);
        BuildLookAtQuaternion(
            t1, qByVal, joints[mdl::TrackedJoint::LeftHip][0],
            joints[mdl::TrackedJoint::LeftHip][1], joints[mdl::TrackedJoint::LeftHip][2],
            joints[mdl::TrackedJoint::LeftKnee][0], joints[mdl::TrackedJoint::LeftKnee][1],
            joints[mdl::TrackedJoint::LeftKnee][2], 3); // 0x4A8069
        std::memcpy(pose.leftLeg, t1, 16);              // 0x4A807A..
    } else if (flag != 0) {                             // 0x4A809F
        pose.leftLeg[3] = 1.0f;                         // 0x4A80A3
        pose.leftLeg[0] = 0.0f;                         // 0x4A80AB
        pose.leftLeg[1] = 0.0f;                         // 0x4A80B1
        pose.leftLeg[2] = 0.0f;                         // 0x4A80B7
    }

    // ---- knee quat #1, model+272 (0x4A80CE..0x4A81D1) --------------------
    if (joints[mdl::TrackedJoint::RightKnee][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightAnkle][1] != kSentinel &&
        joints[mdl::TrackedJoint::Center][1] != kSentinel) {
        d3->quatMultiply(qB, pose.rightLeg, pose.lowerBody); // 0x4A812C
        BuildLookAtQuaternion(
            t1, qB, joints[mdl::TrackedJoint::RightKnee][0],
            joints[mdl::TrackedJoint::RightKnee][1], joints[mdl::TrackedJoint::RightKnee][2],
            joints[mdl::TrackedJoint::RightAnkle][0], joints[mdl::TrackedJoint::RightAnkle][1],
            joints[mdl::TrackedJoint::RightAnkle][2], 1); // 0x4A819B
        std::memcpy(pose.rightKnee, t1, 16);              // 0x4A81AC..
    } else if (flag != 0) {                               // 0x4A81D1
        pose.rightKnee[3] = 1.0f;                         // 0x4A81D5
        pose.rightKnee[0] = 0.0f;                         // 0x4A81DD
        pose.rightKnee[1] = 0.0f;                         // 0x4A81E3
        pose.rightKnee[2] = 0.0f;                         // 0x4A81E9
    }

    // ---- knee quat #2, model+224 (0x4A8200..0x4A831F) --------------------
    if (joints[mdl::TrackedJoint::LeftKnee][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftAnkle][1] != kSentinel &&
        joints[mdl::TrackedJoint::Center][1] != kSentinel) {
        d3->quatMultiply(qB, pose.leftLeg, pose.lowerBody); // 0x4A825E
        BuildLookAtQuaternion(
            t1, qB, joints[mdl::TrackedJoint::LeftKnee][0], joints[mdl::TrackedJoint::LeftKnee][1],
            joints[mdl::TrackedJoint::LeftKnee][2], joints[mdl::TrackedJoint::LeftAnkle][0],
            joints[mdl::TrackedJoint::LeftAnkle][1], joints[mdl::TrackedJoint::LeftAnkle][2],
            1);                             // 0x4A82CD
        std::memcpy(pose.leftKnee, t1, 16); // 0x4A82DA..
    } else if (flag != 0) {                 // 0x4A82FF
        pose.leftKnee[3] = 1.0f;            // 0x4A8303
        pose.leftKnee[0] = 0.0f;            // 0x4A8309
        pose.leftKnee[1] = 0.0f;            // 0x4A830F
        pose.leftKnee[2] = 0.0f;            // 0x4A8315
    }

    // ---- ankle/foot quat #1, model+288 (0x4A832E..0x4A864E) --------------
    if (joints[mdl::TrackedJoint::RightFoot][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightAnkle][1] != kSentinel) {
        float ax = joints[mdl::TrackedJoint::RightAnkle][0] -
                   joints[mdl::TrackedJoint::RightKnee][0],
              ay = joints[mdl::TrackedJoint::RightAnkle][1] -
                   joints[mdl::TrackedJoint::RightKnee][1],
              az = joints[mdl::TrackedJoint::RightAnkle][2] -
                   joints[mdl::TrackedJoint::RightKnee][2]; // 0x4A8365..
        normalizeComponents(ax, ay, az);                    // 0x4A83A9
        float bx = joints[mdl::TrackedJoint::RightFoot][0] -
                   joints[mdl::TrackedJoint::RightAnkle][0],
              by = joints[mdl::TrackedJoint::RightFoot][1] -
                   joints[mdl::TrackedJoint::RightAnkle][1],
              bz = joints[mdl::TrackedJoint::RightFoot][2] -
                   joints[mdl::TrackedJoint::RightAnkle][2]; // 0x4A83BA..
        normalizeComponents(bx, by, bz);                     // 0x4A83FE
        const float dot = by * ay + bx * ax + bz * az;       // 0x4A841F
        bool straight = false;                               // 0x4A849B
        if (joints[mdl::TrackedJoint::RightFoot][2] - joints[mdl::TrackedJoint::RightAnkle][2] >
            0.0f) { // 0x4A8458
            const float w = fabsf(joints[mdl::TrackedJoint::RightFoot][0] -
                                  joints[mdl::TrackedJoint::RightAnkle][0]); // 0x4A8470
            if (w < k75f && dot > 0.5f)
                straight = true; // 0x4A8490
        }
        if (dot < kNeg0d3 || straight) {                         // 0x4A849D
            d3->quatMultiply(t1, pose.rightKnee, pose.rightLeg); // 0x4A84B7
            d3->quatMultiply(t2, t1, pose.lowerBody);            // 0x4A84D0
            std::memcpy(pose.rightFoot, t2, 16);                 // 0x4A84E1..
        } else {
            float ex = 0.0f, ey = kNeg0d7, ez = -1.0f;      // 0x4A8508..
            normalizeComponents(ex, ey, ez);                // 0x4A8524
            float cx = bz * ey - by * ez;                   // 0x4A8549
            float cy = bx * ez - bz * ex;                   // 0x4A856A
            float cz = by * ex - bx * ey;                   // 0x4A8585
            const float dot2 = ey * by + bx * ex + ez * bz; // 0x4A85A1
            float axis[3] = {cx, cy, cz};
            d3->vec3Normalize(axis, axis);                       // 0x4A85A5
            quatRotationAxis(pose.rightFoot, axis, acosf(dot2)); // 0x4A85C9
        }
    } else {
        d3->quatMultiply(t1, pose.rightKnee, pose.rightLeg); // 0x4A860E
        d3->quatMultiply(t2, t1, pose.lowerBody);            // 0x4A8627
        std::memcpy(pose.rightFoot, t2, 16);                 // 0x4A8638..
    }

    // ---- ankle/foot quat #2, model+240 (0x4A8669..0x4A899D) --------------
    if (joints[mdl::TrackedJoint::LeftFoot][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftAnkle][1] != kSentinel) {
        float ax = joints[mdl::TrackedJoint::LeftAnkle][0] - joints[mdl::TrackedJoint::LeftKnee][0],
              ay = joints[mdl::TrackedJoint::LeftAnkle][1] - joints[mdl::TrackedJoint::LeftKnee][1],
              az = joints[mdl::TrackedJoint::LeftAnkle][2] -
                   joints[mdl::TrackedJoint::LeftKnee][2]; // 0x4A86A0..
        normalizeComponents(ax, ay, az);                   // 0x4A86E4
        float bx = joints[mdl::TrackedJoint::LeftFoot][0] - joints[mdl::TrackedJoint::LeftAnkle][0],
              by = joints[mdl::TrackedJoint::LeftFoot][1] - joints[mdl::TrackedJoint::LeftAnkle][1],
              bz = joints[mdl::TrackedJoint::LeftFoot][2] -
                   joints[mdl::TrackedJoint::LeftAnkle][2]; // 0x4A86F5..
        normalizeComponents(bx, by, bz);                    // 0x4A8739
        const float dot = by * ay + bx * ax + bz * az;      // 0x4A875A
        bool straight = false;                              // 0x4A87D6
        if (joints[mdl::TrackedJoint::LeftFoot][2] - joints[mdl::TrackedJoint::LeftAnkle][2] >
            0.0f) { // 0x4A8793
            const float w = fabsf(joints[mdl::TrackedJoint::LeftFoot][0] -
                                  joints[mdl::TrackedJoint::LeftAnkle][0]); // 0x4A87AB
            if (w < k75f && dot > 0.5f)
                straight = true; // 0x4A87CB
        }
        if (dot < kNeg0d3 || straight) {                       // 0x4A87D8
            d3->quatMultiply(t1, pose.leftKnee, pose.leftLeg); // 0x4A87F2
            d3->quatMultiply(t2, t1, pose.lowerBody);          // 0x4A880B
            std::memcpy(pose.leftFoot, t2, 16);                // 0x4A881C..
        } else {
            float ex = 0.0f, ey = kNeg0d7, ez = -1.0f;      // 0x4A884B..
            normalizeComponents(ex, ey, ez);                // 0x4A8867
            float cx = bz * ey - by * ez;                   // 0x4A888C
            float cy = bx * ez - bz * ex;                   // 0x4A88AD
            float cz = by * ex - bx * ey;                   // 0x4A88C8
            const float dot2 = ey * by + bx * ex + ez * bz; // 0x4A88E4
            float axis[3] = {cx, cy, cz};
            d3->vec3Normalize(axis, axis);                      // 0x4A88E8
            quatRotationAxis(pose.leftFoot, axis, acosf(dot2)); // 0x4A890C
        }
    } else {
        d3->quatMultiply(t1, pose.leftKnee, pose.leftLeg); // 0x4A895C
        d3->quatMultiply(t2, t1, pose.lowerBody);          // 0x4A8975
        std::memcpy(pose.leftFoot, t2, 16);                // 0x4A8986..
    }
}

} // namespace mikudancestudio
