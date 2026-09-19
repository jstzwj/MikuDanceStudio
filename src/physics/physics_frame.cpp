// ===========================================================================
// Physics frame update: 0x004B22F0 / 0x004B3460 / 0x004B25D0 + the physics
// section of the FrameDriver (0x0046F0D0..0x0046FEB8 core)
// ===========================================================================
// Original per-frame flow (verified from the binary):
//   FrameDriver 0x46F0xx region, three "physics blocks" sharing one Bullet
//   world (scene->world, app+0x9EDB0):
//     block 1/2 (0x46F12C / 0x46F412): catch-up loops.  While the physics
//       cursor app+0x9E64C is behind the target time, each tick runs
//       [morph apply 0x4970B0 + SetPhysicsMode 0x4A9220 per model (ordered
//       by model+0x2D7D), kinematic sync 0x4B22F0 per model (reverse slot
//       order), world->stepSimulation(1/60, 10, 1/60)] and advances the
//       cursor by 1/60 (dbl_52EA08).  flt_52EA00 = dbl_52EA08 = 1/60.
//     block 3 (0x46FC77): per-model [morph apply 0x4970B0 (0x46FCB1) +
//       SetPhysicsMode], then - gated on
//       (app+0xA066C moved | app+0x9EDB5 settle | app+0x330 frame advanced)
//       and model count 1..3 and app+0xA0B74==0 - the settle section:
//         all models: 0x4B22F0 (kinematic sync)
//         stepSimulation (twice when dragged with 3 models)
//         app+0x9EDB5: 3x [all models: 0x4B3460 (reseat) + step], clear
//         all models: 0x4B25D0 (physics readback into the bones)
//         clear app+0xA066C
//       then a second per-model [morph apply 0x4970B0 (0x46FE93) +
//       SetPhysicsMode(a2=1)] pass (0x46FE5F).  (0x4970B0 was labelled
//       "IK" in early notes - it is the morph application pass; the CCD IK
//       solve runs inside BoneFrameTransform 0x493A60 phase D, reached via
//       SetPhysicsMode.)
//     0x46F7DB..0x46F808: inside the selection gate (var_14A1 selActive &&
//       app+0x2F8==0 && app+0x9ED90==0), `cmp byte [app+0xA03E8],0 / jnz`
//       guards `mov byte [app+0x9EDB5],1`.  0xA03E8 is NOT a "not playing"
//       flag - it is the x86 twin of the x64 0xA137C stay-behind latch: the
//       pump tail (0x47985A..0x47985E) writes this pump's var_14A1 back
//       into it, so the settle request fires on the selActive RISING EDGE
//       only, exactly like x64 0x7FF7CB44C12E (`cmp [0xA137C],0 / jnz /
//       mov [0x9FCC1],1`).
//   stepSimulation is reached through vtable slot 7 (+0x1C) loaded into
//   eax ("call eax" - byte pattern search for FF/2 disp8 finds nothing);
//   parameters timeStep == fixedTimeStep == 1/60f, maxSubSteps = 10.
//
// btRigidBody::saveKinematicState (0x4F1000) reads the motion state into
// m_worldTransform before deriving velocities - stock bullet 2.75 already
// behaves this way (verified against the repo's bullet-2.75 source), so
// 0x4B22F0 only has to write the motion state.
//
// Port scope / deviations (docs/ARCHITECTURE.md section 8):
//   - 0x4970B0 (morph application) is ported as ModelApplyMorphs and is
//     called in both ordered per-model passes, matching 0x46FCB1/0x46FE93.
//   - The block 1/2 catch-up loops (frame-step 0x46F12C / playing 0x46F3D6,
//     cursor 0x9E64C driven by 0x9E654 + wall clock, clamped by 0x9E658)
//     live in playback_catchup.cpp (PlaybackCatchup), called by FrameDriver
//     right before this settle section.
//   - 0x4B3460 / x64 sub_7FF7CB4E45D0 resets only the CURRENT transform
//     plus the four velocity slots (linear/angular, current and
//     interpolation); the interpolation world transform is NOT rewritten
//     and neither clearForces nor the motion state nor activation is
//     touched (write sequence 0x7FF7CB4E4856..0x7FF7CB4E4927).  The port
//     uses the matching public Bullet setters rather than writing
//     implementation-private members.
//   - The settle request latches on the selActive rising edge in BOTH
//     originals (x86 `cmp byte [0xA03E8],0` at 0x46F7FF, x64
//     `cmp [0xA137C],r15b` at 0x7FF7CB44C12E; layout_pins.hpp pins
//     selectionActiveLatch to 0xA03E8 / 0xA137C).  The only per-arch delta
//     is the pump-tail latch write-back: x86 0x47985E sits inside the
//     "model selection present && not playing" gate (0x4797C2
//     `jz 0x479ADC` skips the write when model+0x2D90<0 or app+0x330!=0,
//     so during playback the latch keeps its old value and the request
//     re-fires per pass), while x64 0x7FF7CB456F21 writes it back
//     unconditionally.  That write-back lives in frame_driver.cpp (which
//     follows x64); physics_frame.cpp itself is arch-identical here.
//   - x64 gates the flow twice where x86 (the outline above) gates once:
//     gate A 0x7FF7CB44B8E7 (pump counter app+0xA1E18 == 0) skips the
//     whole section, gate B 0x7FF7CB44C65B (accessory dialog app+0xA166D
//     != 0) skips only the pose passes / world pass / readback / moved
//     clear.  The readback loop and the moved clear sit INSIDE the settle
//     gate in BOTH layouts: x86 0x46FD39 `jz loc_46FE5F` and x64
//     0x7FF7CB44C73D `jz 0x7FF7CB44C85F` both jump past them straight to
//     the second pose pass (the only xrefs into x86 0x46FE34 are the
//     in-gate entries 0x46FDD3/0x46FE2D).  The port matches both.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/physics_world.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "btBulletDynamicsCommon.h"

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "../app/frame_state_dump.hpp"

namespace mikudancestudio {

using d3dx::D3DXMATRIXF;

namespace {

// ---- D3DX-equivalent 4x4 helpers (exact same formulas, row-major) --------
// The follow-matrix chain (0x4B22F0/0x4B3460) calls the REAL d3dx9_32.dll:
// D3DXMatrixMultiply is pure x87 there (every element = four extended
// products summed on the FPU stack, one rounding at the store) and the
// rotation builders use fsin/fcos.  MSVC SSE float chains drift by ULPs,
// which moves every kinematic hair target, so route these through the
// runtime-resolved DLL exactly like bone_transform.cpp does; the plain
// float versions remain only as a no-DLL fallback.
namespace {
// ---- D3DX-equivalent 4x4 helpers (exact same formulas, row-major) --------
// D3DXMatrixMultiply(out, a, b): out = a * b.
void MatMul(float* out, const float* a, const float* b) {
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] +
                           a[i * 4 + 1] * b[1 * 4 + j] +
                           a[i * 4 + 2] * b[2 * 4 + j] +
                           a[i * 4 + 3] * b[3 * 4 + j];
    std::memcpy(out, r, sizeof r);
}

void MatTranslation(float* out, float x, float y, float z) {
    std::memset(out, 0, 16 * sizeof(float));
    out[0] = out[5] = out[10] = out[15] = 1.0f;
    out[12] = x; out[13] = y; out[14] = z;
}

void MatRotX(float* out, float a) {
    const float c = std::cos(a), s = std::sin(a);
    std::memset(out, 0, 16 * sizeof(float));
    out[0] = 1.0f; out[5] = c; out[6] = s; out[9] = -s; out[10] = c;
    out[15] = 1.0f;
}

void MatRotY(float* out, float a) {
    const float c = std::cos(a), s = std::sin(a);
    std::memset(out, 0, 16 * sizeof(float));
    out[0] = c; out[2] = -s; out[5] = 1.0f; out[8] = s; out[10] = c;
    out[15] = 1.0f;
}

void MatRotZ(float* out, float a) {
    const float c = std::cos(a), s = std::sin(a);
    std::memset(out, 0, 16 * sizeof(float));
    out[0] = c; out[1] = s; out[4] = -s; out[5] = c; out[10] = 1.0f;
    out[15] = 1.0f;
}

// D3DXQuaternionRotationMatrix (largest-diagonal form).
void QuatFromMatrix(float out[4], const float* m) {
    const float trace = m[0] + m[5] + m[10];
    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f);
        out[3] = s * 0.5f;
        s = 0.5f / s;
        out[0] = (m[6] - m[9]) * s;
        out[1] = (m[8] - m[2]) * s;
        out[2] = (m[1] - m[4]) * s;
        return;
    }
    int i = 0;
    if (m[5] > m[0]) i = 1;
    if (m[10] > m[4 * i + i]) i = 2;
    static const int kNext[3] = {1, 2, 0};
    const int j = kNext[i];
    const int k = kNext[j];
    float s = std::sqrt(m[4 * i + i] - m[4 * j + j] - m[4 * k + k] + 1.0f);
    float q[4];
    q[i] = 0.5f * s;
    s = 0.5f / s;
    q[3] = (m[4 * k + j] - m[4 * j + k]) * s;
    q[j] = (m[4 * j + i] + m[4 * i + j]) * s;
    q[k] = (m[4 * k + i] + m[4 * i + k]) * s;
    std::memcpy(out, q, sizeof q);
}

// D3DXQuaternionMultiply(out, a, b): out = a * b (x,y,z,w).
void QuatMul(float out[4], const float a[4], const float b[4]) {
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

// D3DXMatrixInverse (cofactor form; returns false on singular matrix).
bool MatInverse(float* out, const float* m) {
    float dst[16];
    dst[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] -
             m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    dst[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] +
             m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    dst[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] -
             m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    dst[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] +
              m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    dst[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] +
             m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    dst[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] -
             m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    dst[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] +
             m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    dst[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] -
              m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    dst[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] -
             m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    dst[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] +
             m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    dst[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] -
              m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    dst[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] +
              m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    dst[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] +
             m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    dst[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] -
             m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    dst[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] +
              m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    dst[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] -
              m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * dst[0] + m[4] * dst[1] + m[8] * dst[2] +
                      m[12] * dst[3];
    if (std::fabs(det) < 1e-12f)
        return false;
    const float inv = 1.0f / det;
    for (int i = 0; i < 16; ++i)
        out[i] = dst[i] * inv;
    return true;
}

// sub_0x401400 - transpose the body's raw transform into a D3DX matrix.
// The btTransform basis stores rows; the D3DX matrix gets the transpose,
// origin copied verbatim (matches 0x401400 byte for byte).
void TransformToMatrix(float out[16], const btTransform& t) {
    const btMatrix3x3& b = t.getBasis();
    const btVector3& o = t.getOrigin();
    out[0] = b[0].x();  out[1] = b[1].x();  out[2] = b[2].x();  out[3] = 0.0f;
    out[4] = b[0].y();  out[5] = b[1].y();  out[6] = b[2].y();  out[7] = 0.0f;
    out[8] = b[0].z();  out[9] = b[1].z();  out[10] = b[2].z(); out[11] = 0.0f;
    out[12] = o.x();    out[13] = o.y();    out[14] = o.z();    out[15] = 1.0f;
}

// The reverse of the above (the raw writes at 0x4B25D0/0x4B3460 body+16):
// basis rows = D3DX columns.
void MatrixToBasis(btTransform* t, const float* m) {
    t->getBasis().setValue(
        m[0], m[4], m[8],
        m[1], m[5], m[9],
        m[2], m[6], m[10]);
    t->setOrigin(btVector3(m[12], m[13], m[14]));
}

inline float F32(const unsigned char* p, std::size_t off) {
    return *reinterpret_cast<const float*>(p + off);
}

inline float& F32R(unsigned char* p, std::size_t off) {
    return *reinterpret_cast<float*>(p + off);
}

D3DXMATRIXF* D3dxRotX(D3DXMATRIXF* o, float a) {
    auto* api = &d3dx::Get();
    if (api->Load() && api->rotX) return api->rotX(o, a);
    MatRotX(reinterpret_cast<float*>(o), a);
    return o;
}
D3DXMATRIXF* D3dxRotY(D3DXMATRIXF* o, float a) {
    auto* api = &d3dx::Get();
    if (api->Load() && api->rotY) return api->rotY(o, a);
    MatRotY(reinterpret_cast<float*>(o), a);
    return o;
}
D3DXMATRIXF* D3dxRotZ(D3DXMATRIXF* o, float a) {
    auto* api = &d3dx::Get();
    if (api->Load() && api->rotZ) return api->rotZ(o, a);
    MatRotZ(reinterpret_cast<float*>(o), a);
    return o;
}
D3DXMATRIXF* D3dxMul(D3DXMATRIXF* o, const D3DXMATRIXF* a, const D3DXMATRIXF* b) {
    auto* api = &d3dx::Get();
    if (api->Load())
        return api->multiply(o, a, b);
    MatMul(reinterpret_cast<float*>(o), reinterpret_cast<const float*>(a),
           reinterpret_cast<const float*>(b));
    return o;
}
float* D3dxVec3Normalize(float o[3], const float* src) {
    auto* api = &d3dx::Get();
    if (api->Load() && api->vec3Normalize)
        return api->vec3Normalize(o, src);
    float x = src[0], y = src[1], z = src[2];
    float len = std::sqrt(x * x + y * y + z * z);
    if (len > 0.0f) {
        o[0] = x / len;
        o[1] = y / len;
        o[2] = z / len;
    } else {
        o[0] = x;
        o[1] = y;
        o[2] = z;
    }
    return o;
}
D3DXMATRIXF* D3dxTranslation(D3DXMATRIXF* o, float x, float y, float z) {
    auto* api = &d3dx::Get();
    if (api->Load())
        return api->translation(o, x, y, z);
    MatTranslation(reinterpret_cast<float*>(o), x, y, z);
    return o;
}
D3DXMATRIXF* D3dxInverse(D3DXMATRIXF* o, const D3DXMATRIXF* m) {
    auto* api = &d3dx::Get();
    if (api->Load())
        return api->inverse(o, nullptr, m);
    return MatInverse(reinterpret_cast<float*>(o),
                      reinterpret_cast<const float*>(m)) ? o : nullptr;
}
float* D3dxQuatFromMatrix(float out[4], const D3DXMATRIXF* m) {
    auto* api = &d3dx::Get();
    if (api->Load())
        return api->quatFromMatrix(out, m);
    QuatFromMatrix(out, reinterpret_cast<const float*>(m));
    return out;
}
float* D3dxQuatMul(float out[4], const float a[4], const float b[4]) {
    auto* api = &d3dx::Get();
    if (api->Load())
        return api->quatMultiply(out, a, b);
    QuatMul(out, a, b);
    return out;
}
}  // namespace

// Build the bone-follow matrix shared by 0x4B22F0 and 0x4B3460:
//   Rz(rec+72) * Rx(rec+64) * Ry(rec+68) * T(rec+52..60)
//   [ * T(bone+308..316) * boneMatrix(+52) when the bone link is valid ].
// boneOverride >= 0 forces the bone index (0x4B3460 uses the center bone
// for unlinkable records).
void BuildFollowMatrix(float out[16], unsigned char* m,
                       const mdl::RigidRecord& rigid,
                       int boneOverride) {
    mdl::BoneRecord* bones = mdl::Bones(m);
    D3DXMATRIXF t;
    auto* o = reinterpret_cast<D3DXMATRIXF*>(out);
    D3dxRotZ(o, rigid.rotation[2]);
    D3dxRotX(&t, rigid.rotation[0]);
    D3dxMul(o, o, &t);
    D3dxRotY(&t, rigid.rotation[1]);
    D3dxMul(o, o, &t);
    D3dxTranslation(&t, rigid.position[0], rigid.position[1],
                    rigid.position[2]);
    D3dxMul(o, o, &t);
    const int bone = boneOverride >= 0 ? boneOverride : rigid.boneIndex;
    if (bone >= 0) {
        const mdl::BoneRecord& bn = bones[bone];
        D3dxTranslation(&t, bn.position[0], bn.position[1], bn.position[2]);
        D3dxMul(o, o, &t);
        D3dxMul(o, o, reinterpret_cast<const D3DXMATRIXF*>(bn.matInit));
    }
}

inline btRigidBody* BodyOf(const mdl::RigidRecord& rigid) {
    return static_cast<btRigidBody*>(rigid.body);
}

// VA 0x004B3460 / x64 sub_7FF7CB4E45D0 - the settle reseat.  The x64 write
// sequence (0x7FF7CB4E4856..0x7FF7CB4E4927) is:
//   body+0x10..0x4F  <- follow transform (m_worldTransform, 4x16B rows)
//   body+0x150 = 0   (m_linearVelocity, 16B)
//   body+0x160 = 0   (m_angularVelocity, 16B)
//   body+0x90  = 0   (m_interpolationLinearVelocity, 16B)
//   body+0xA0  = 0   (m_interpolationAngularVelocity, 16B)
// and NOTHING else: the interpolation world transform (+0x50) keeps its old
// pose, clearForces is not called, the motion state is not written and the
// body is not activated.  (The earlier "resets both the current and
// interpolation transforms" claim came from the x86 notes and is wrong.)
void ReseatRigidBody(btRigidBody* body, const btTransform& transform) {
    const btVector3 zero(0.0f, 0.0f, 0.0f);
    body->setWorldTransform(transform);
    body->setLinearVelocity(zero);
    body->setAngularVelocity(zero);
    body->setInterpolationLinearVelocity(zero);
    body->setInterpolationAngularVelocity(zero);
}

// The over-stretch teleport inside the readback (x64 sub_7FF7CB4E3470,
// inline sequence 0x7FF7CB4E3798..0x7FF7CB4E384C) is a SMALLER reset: the
// world transform rows (+0x10..0x40) are stored and only the current
// velocity slots are zeroed (+0x150, +0x160).  The interpolation velocity
// slots (+0x90/+0xA0) are left alone - this is NOT sub_7FF7CB4E45D0.
void StopBodyAt(btRigidBody* body, const btTransform& transform) {
    const btVector3 zero(0.0f, 0.0f, 0.0f);
    body->setWorldTransform(transform);
    body->setLinearVelocity(zero);
    body->setAngularVelocity(zero);
}

#ifdef MIKUDANCESTUDIO_DIAG
// ---- rigid-body state dumps under MIKUDANCESTUDIO_STATE_DUMP_DIR ---------
// (porting-era A/B tooling; see frame_state_dump.hpp for the gate)

template <typename T>
void WriteStateWords(std::FILE* stream, const char* name, const T& value) {
    std::fprintf(stream, "],\"%s\":[", name);
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    for (std::size_t word = 0; word < sizeof(value) / 4; ++word) {
        if (word != 0)
            std::fputc(',', stream);
        std::uint32_t bits = 0;
        std::memcpy(&bits, bytes + 4 * word, sizeof(bits));
        std::fprintf(stream, "\"%08X\"", bits);
    }
}

void WriteEmptyStateField(std::FILE* stream, const char* name) {
    std::fprintf(stream, "],\"%s\":[", name);
}

void DumpRigidBodyState(MMDApp* app, const char* fileName) {
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    CreateDirectoryA(directory, nullptr);
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\%s", directory, fileName);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, "wb") != 0 || stream == nullptr)
        return;

    std::fputs("{\n  \"schema\": 1,\n  \"models\": [", stream);
    bool firstModel = true;
    // port diagnostic: bound = model-slot capacity (kModelSlotCount)
    for (unsigned slot = 0; slot < static_cast<unsigned>(kModelSlotCount);
         ++slot) {
        // Model slots are pointers.  The old x86 address expression used a
        // four-byte stride and silently walked through the middle of entries
        // in the x64 build, making diagnostic output (and any future caller
        // copied from it) observe arbitrary models.
        auto* model = app->ModelSlot(slot);
        if (model == nullptr)
            continue;
        auto* rigids = mdl::Rigids(model);
        const int count = static_cast<int>(mdl::Mdl(model)->rigidCount);
        if (!firstModel)
            std::fputc(',', stream);
        firstModel = false;
        std::fprintf(stream, "\n    {\"slot\": %u, \"rigid_count\": %d, "
                             "\"rigids\": [", slot, count);
        if (rigids != nullptr && count >= 0 && count <= 100000) {
            for (int i = 0; i < count; ++i) {
                if (i != 0)
                    std::fputc(',', stream);
                const mdl::RigidRecord& rec = rigids[i];
                btRigidBody* body = BodyOf(rec);
                std::fprintf(stream,
                    "{\"index\":%d,\"bone\":%d,\"mode\":%d,"
                    "\"world_bits\":[",
                    i, rec.boneIndex,
                    static_cast<int>(static_cast<signed char>(rec.mode)));
                if (body != nullptr) {
                    const auto* transform = reinterpret_cast<const unsigned char*>(
                        &body->getWorldTransform());
                    for (int word = 0; word < 16; ++word) {
                        if (word != 0)
                            std::fputc(',', stream);
                        std::uint32_t value = 0;
                        std::memcpy(&value, transform + 4 * word, 4);
                        std::fprintf(stream, "\"%08X\"", value);
                    }
                }
                if (body != nullptr) {
                    WriteStateWords(stream, "interpolation_world_bits",
                                    body->getInterpolationWorldTransform());
                    WriteStateWords(stream, "interpolation_linear_bits",
                                    body->getInterpolationLinearVelocity());
                    WriteStateWords(stream, "interpolation_angular_bits",
                                    body->getInterpolationAngularVelocity());
                    WriteStateWords(stream, "linear_bits",
                                    body->getLinearVelocity());
                    WriteStateWords(stream, "angular_bits",
                                    body->getAngularVelocity());
                } else {
                    WriteEmptyStateField(stream, "interpolation_world_bits");
                    WriteEmptyStateField(stream, "interpolation_linear_bits");
                    WriteEmptyStateField(stream, "interpolation_angular_bits");
                    WriteEmptyStateField(stream, "linear_bits");
                    WriteEmptyStateField(stream, "angular_bits");
                }
                std::fputs("]}", stream);
            }
        }
        std::fputs("]}", stream);
    }
    if (!firstModel)
        std::fputc('\n', stream);
    std::fputs("  ]\n}\n", stream);
    std::fclose(stream);
}

void DumpWorldSolverInfo(btDiscreteDynamicsWorld* world) {
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH || world == nullptr)
        return;

    CreateDirectoryA(directory, nullptr);
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\physics_world.json", directory);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, "wb") != 0 || stream == nullptr)
        return;

    const btContactSolverInfo& info = world->getSolverInfo();
    const auto* worldBytes = reinterpret_cast<const unsigned char*>(world);
    const auto* infoBytes = reinterpret_cast<const unsigned char*>(&info);
    std::fprintf(stream,
                 "{\n  \"solver_info_offset\": %u,\n"
                 "  \"solver_info_size\": %u,\n  \"words\": [",
                 static_cast<unsigned>(infoBytes - worldBytes),
                 static_cast<unsigned>(sizeof(info)));
    for (std::size_t i = 0; i < sizeof(info) / 4; ++i) {
        if (i != 0)
            std::fputc(',', stream);
        std::uint32_t word = 0;
        std::memcpy(&word, infoBytes + i * 4, 4);
        std::fprintf(stream, "\"%08X\"", word);
    }
    std::fputs("]\n}\n", stream);
    std::fclose(stream);
}

#endif  // MIKUDANCESTUDIO_DIAG

}  // namespace

// VA 0x004B22F0 - push kinematic rigid bodies to the bones.
// For every mode==0 record the bone-follow transform is written into the
// body's motion state; btRigidBody::saveKinematicState (called from
// stepSimulation) pulls it into m_worldTransform and derives velocities.
void ModelKinematicSync(unsigned char* m) {
    const int count = mdl::Mdl(m)->rigidCount;
    if (count <= 0)
        return;
    mdl::RigidRecord* rigids = mdl::Rigids(m);
    for (int i = 0; i < count; ++i) {
        const mdl::RigidRecord& rec = rigids[i];
        if (rec.mode != 0)
            continue;
        btRigidBody* body = BodyOf(rec);
        if (body == nullptr)
            continue;                                       // port guard
        float mat[16];
        BuildFollowMatrix(mat, m, rec, -1);
        btTransform tr;
        tr.setIdentity();
        MatrixToBasis(&tr, mat);
        if (btMotionState* ms = body->getMotionState())
            ms->setWorldTransform(tr);
    }
}

// VA 0x004B3460 / x64 sub_7FF7CB4E45D0 - reseat dynamic rigid bodies onto
// their bone-follow pose.  Teleports every mode>0 body to the follow
// transform and stops it (current transform + the four velocity slots only;
// see ReseatRigidBody for the exact x64 write set).
void ModelDynamicReseat(unsigned char* m) {
    const int count = mdl::Mdl(m)->rigidCount;
    if (count <= 0)
        return;
    mdl::RigidRecord* rigids = mdl::Rigids(m);
    const int centerBone = mdl::Mdl(m)->centerBone;   // 0x4B3460
#ifdef MIKUDANCESTUDIO_DIAG
    static int reseatCalls = 0;
    const bool traceReseat = getenv("MIKUDANCESTUDIO_TRACE_RESEAT") != nullptr;
    if (traceReseat && reseatCalls < 12) {
        int active = 0;
        for (int i = 0; i < count; ++i)
            if (static_cast<signed char>(rigids[i].mode) > 0)
                ++active;
        std::fprintf(stderr,
                     "RESEAT call=%d count=%d active(rec80>0)=%d\n",
                     reseatCalls, count, active);
    }
    ++reseatCalls;
#endif
    for (int i = 0; i < count; ++i) {
        const mdl::RigidRecord& rec = rigids[i];
        if (static_cast<signed char>(rec.mode) <= 0)
            continue;
        btRigidBody* body = BodyOf(rec);
        if (body == nullptr)
            continue;                                       // port guard
#ifdef MIKUDANCESTUDIO_DIAG
        if (traceReseat && reseatCalls <= 3) {
            const int id = body->m_debugBodyId;
            if (id == 3) {
                unsigned bits;
                const float ox = body->getWorldTransform().getOrigin().getX();
                std::memcpy(&bits, &ox, 4);
                std::fprintf(stderr,
                             "RESEAT body3 iter=%d rec80=%d pre_origin=%08X\n",
                             reseatCalls, (int)rec.mode, bits);
            }
        }
#endif
        float mat[16];
        const int link = rec.boneIndex;
        BuildFollowMatrix(mat, m, rec, link < 0 ? centerBone : -1);
        btTransform tr;
        tr.setIdentity();
        MatrixToBasis(&tr, mat);
        ReseatRigidBody(body, tr);
    }
}

// VA 0x004B25D0 - pull the simulation result back into the bones.
//  part 1: over-stretched joints (distance - limit >= 2) teleport body B
//          onto the clamped position of body A and stop it.
//  part 2: bone world matrix (+52) = rigid offset matrix (+108) *
//          body world transform.
//  part 3: local matrix (+116) = world * inverse(parent world); physics
//          pose pos +0x188 / quat +0x194 derived from it, plus the
//          type-5 and limit-flag corrections and the mode==2 collapse.
void ModelPhysicsReadback(unsigned char* m) {
    mdl::BoneRecord* bones = mdl::Bones(m);
    mdl::RigidRecord* rigids = mdl::Rigids(m);
    const int rcount = mdl::Mdl(m)->rigidCount;
    mdl::JointRecord* joints = mdl::Joints(m);
    const int jcount = mdl::Mdl(m)->jointCount;

    // ---- part 1: joint overstretch clamp ------------------------------
    for (int j = 0; j < jcount; ++j) {
        const mdl::JointRecord& joint = joints[j];
        const int ra = joint.rigidA;
        const int rb = joint.rigidB;
        if (ra < 0 || ra >= rcount || rb < 0 || rb >= rcount)
            continue;                                       // port guard
        btRigidBody* bodyA = BodyOf(rigids[ra]);
        btRigidBody* bodyB = BodyOf(rigids[rb]);
        if (bodyA == nullptr || bodyB == nullptr)
            continue;                                       // port guard
        float ma[16], mb[16];
        TransformToMatrix(ma, bodyA->getWorldTransform());
        TransformToMatrix(mb, bodyB->getWorldTransform());
        // Original 0x4B26B5 / x64 0x7FF7CB4E3660..0x7FF7CB4E36D9: d2 =
        // ((dy*dy + dx*dx) + dz*dz) and everything downstream runs single
        // precision - sqrtf, the (dist - limit) comiss, divss limit/dist
        // for k, and the (mb-ma)*k products as mulss into the translation.
        const float dy = ma[13] - mb[13], dx = ma[12] - mb[12],
                    dz = ma[14] - mb[14];
        const float d2 = dy * dy + dx * dx + dz * dz;
        const float dist = std::sqrt(d2);
        const float limit = joint.radiusBound;
        if (dist - limit < 2.0f)
            continue;
        const float k = limit / dist;
        float t[16];
        D3dxTranslation(reinterpret_cast<D3DXMATRIXF*>(t),
                        (mb[12] - ma[12]) * k,
                        (mb[13] - ma[13]) * k,
                        (mb[14] - ma[14]) * k);
        D3dxMul(reinterpret_cast<D3DXMATRIXF*>(ma),
                reinterpret_cast<const D3DXMATRIXF*>(ma),
                reinterpret_cast<const D3DXMATRIXF*>(t));
        btTransform tr = bodyB->getWorldTransform();
        tr.setOrigin(btVector3(ma[12], ma[13], ma[14]));
        StopBodyAt(bodyB, tr);   // x64 inline reset, see StopBodyAt
    }

    // ---- part 2: dynamic bodies drive the bone world matrices ---------
    for (int i = 0; i < rcount; ++i) {
        const mdl::RigidRecord& rec = rigids[i];
        if (static_cast<signed char>(rec.mode) <= 0)
            continue;
        const int bone = rec.boneIndex;
        if (bone < 0)
            continue;
        btRigidBody* body = BodyOf(rec);
        if (body == nullptr)
            continue;                                       // port guard
        float w[16];
        TransformToMatrix(w, body->getWorldTransform());
        // Original 0x4B29CE routes this through the real D3DXMatrixMultiply
        // (x87, one rounding per element); an SSE float chain drifts 1-2 ULP
        // into every physics bone.
        D3dxMul(reinterpret_cast<D3DXMATRIXF*>(mdl::Bones(m)[bone].matInit),
                reinterpret_cast<const D3DXMATRIXF*>(rec.invTransform),
                reinterpret_cast<const D3DXMATRIXF*>(w));
    }

    // ---- part 3: derive the per-bone physics pose ----------------------
    for (int i = 0; i < rcount; ++i) {
        const mdl::RigidRecord& rigid = rigids[i];
        if (static_cast<signed char>(rigid.mode) <= 0)
            continue;
        const int bi = rigid.boneIndex;
        if (bi < 0)
            continue;
        mdl::BoneRecord& bone = mdl::Bones(m)[bi];
        float* world = bone.matInit;
        float* local = bone.matLocal;
        const int parent = bone.parent;
        float inv[16];
        bool haveLocal;
        if (parent < 0) {
            std::memcpy(local, world, 16 * sizeof(float));
            haveLocal = true;
        } else if (D3dxInverse(reinterpret_cast<D3DXMATRIXF*>(inv),
                               reinterpret_cast<const D3DXMATRIXF*>(
                                   mdl::Bones(m)[parent].matInit))) {
            D3dxMul(reinterpret_cast<D3DXMATRIXF*>(local),
                    reinterpret_cast<const D3DXMATRIXF*>(world),
                    reinterpret_cast<const D3DXMATRIXF*>(inv));
            haveLocal = true;
        } else {
            haveLocal = false;
        }
        if (!haveLocal) {
            // singular parent world matrix: original falls straight through
            // to the +244 copy below.
            std::memcpy(bone.matExtra, bone.matInit,
                        sizeof(bone.matExtra));               // 0x4B419x
            continue;
        }

        float* q = bone.ikBackup + 3;                         // +0x194
        D3dxQuatFromMatrix(q, reinterpret_cast<const D3DXMATRIXF*>(local));

        // +0x188 position = translation of T(p) * local * T(-p); the
        // T(p) * local product is kept for the mode==2 branch below.
        float work[16], pos[16], t[16];
        D3dxTranslation(reinterpret_cast<D3DXMATRIXF*>(t), bone.position[0],
                        bone.position[1], bone.position[2]);
        D3dxMul(reinterpret_cast<D3DXMATRIXF*>(work),
                reinterpret_cast<const D3DXMATRIXF*>(t),
                reinterpret_cast<const D3DXMATRIXF*>(local));
        D3dxTranslation(reinterpret_cast<D3DXMATRIXF*>(t), -bone.position[0],
                        -bone.position[1], -bone.position[2]);
        D3dxMul(reinterpret_cast<D3DXMATRIXF*>(pos),
                reinterpret_cast<const D3DXMATRIXF*>(work),
                reinterpret_cast<const D3DXMATRIXF*>(t));
        std::memcpy(bone.ikBackup, pos + 12, 3 * sizeof(float));

        const int target = bone.tailIdx;
        mdl::BoneRecord& tb = target >= 0 ? mdl::Bones(m)[target] : bone;

        if (bone.type == mdl::BoneType::RotateGrant) {        // tail type
            float conj[4] = {-tb.physicsQuat[0], -tb.physicsQuat[1],
                             -tb.physicsQuat[2], tb.physicsQuat[3]};
            float out[4];
            D3dxQuatMul(out, conj, q);
            std::memcpy(q, out, sizeof out);
        }
        if ((bone.flags & mdl::kBoneFlagRotInherit) != 0) {
            float limit[4];
            D3dxQuatFromMatrix(limit, reinterpret_cast<const D3DXMATRIXF*>(
                                           tb.matLocal));
            if (limit[3] > 1.0f) limit[3] = 1.0f;
            if (limit[3] < -1.0f) limit[3] = -1.0f;
            // x64 0x7FF7CB4E3C85: acosf (float CRT), then subss with the
            // truncated-pi float 0x4048F5C3.
            float angle = std::acos(limit[3]);
            if (limit[3] < 0.0f)
                angle -= 3.140000104904175f;                 // 0x529xxx
            // Original 0x4B2CED / x64 0x7FF7CB4E3CD6: ((y*y + x*x) + z*z)
            // through sqrtf.
            const float axisLen = std::sqrt(limit[1] * limit[1] +
                                            limit[0] * limit[0] +
                                            limit[2] * limit[2]);
            if (angle != 0.0f && axisLen >= 0.00000011920929f) {
                const float a = angle * bone.inheritRatio;
                // x64 0x7FF7CB4E3D0E/0x7FF7CB4E3D5F: sinf/cosf and a
                // divss sin/axisLen.
                const float s = std::sin(a) / axisLen;
                const float rot[4] = {-limit[0] * s, -limit[1] * s,
                                      -limit[2] * s,
                                      std::cos(a)};
                // Multiplication order differs per unrolled original path:
                // parent>=0 (0x4B2DB6) computes rot*q; parent<0 (0x4B3339)
                // computes q*rot in place.
                float out[4];
                if (parent >= 0)
                    D3dxQuatMul(out, rot, q);
                else
                    D3dxQuatMul(q, q, rot);
                if (parent >= 0)
                    std::memcpy(q, out, sizeof out);
            }
        }
        if ((bone.flags & mdl::kBoneFlagTransInherit) != 0) {
            for (int c = 0; c < 3; ++c)
                bone.physicsOffset[c] -= tb.physicsOffset[c] * bone.inheritRatio;
        }
        if (rigid.mode == 2) {
            // mode 2: collapse the physics pose onto the bone position and
            // rebuild the local/world matrices around it.
            std::memset(bone.trans, 0, sizeof(bone.trans));
            std::memset(bone.ikBackup, 0, 3 * sizeof(float));
            work[12] = work[13] = work[14] = 0.0f;
            D3dxTranslation(reinterpret_cast<D3DXMATRIXF*>(t),
                            -bone.position[0], -bone.position[1],
                            -bone.position[2]);
            D3dxMul(reinterpret_cast<D3DXMATRIXF*>(local),
                    reinterpret_cast<const D3DXMATRIXF*>(t),
                    reinterpret_cast<const D3DXMATRIXF*>(work));
            D3dxTranslation(reinterpret_cast<D3DXMATRIXF*>(t),
                            bone.position[0], bone.position[1],
                            bone.position[2]);
            D3dxMul(reinterpret_cast<D3DXMATRIXF*>(local),
                    reinterpret_cast<const D3DXMATRIXF*>(local),
                    reinterpret_cast<const D3DXMATRIXF*>(t));
            if (parent >= 0)
                D3dxMul(reinterpret_cast<D3DXMATRIXF*>(world),
                        reinterpret_cast<const D3DXMATRIXF*>(local),
                        reinterpret_cast<const D3DXMATRIXF*>(
                            mdl::Bones(m)[parent].matInit));
        }
        std::memcpy(bone.matExtra, bone.matInit, sizeof(bone.matExtra));
    }
}

// Physics section of the FrameDriver (original 0x46F0D0..0x46FEB8 core).
// One shared Bullet world steps at a fixed 1/60 with a 10-substep cap;
// kinematic bodies are pushed from the bones before the step and the
// dynamic result is read back after the settle iterations.
void PhysicsFrame(MMDApp* app, unsigned char selActive) {
    auto& s = *app;
    // ---- gate A: the shared message-seen / pump counter ------------------
    // ONE field in both originals (x64 app+0xA1E18 / x86 app+0xA0D6C =
    // state.messageSeen), with three jobs: the Present gate in section 9 of
    // FrameDriver, the pump-region gates, and this physics gate. Every
    // window message (MainWndProc entry) and each dialog-proc entry point
    // resets it to 1; the pump prologue (AdvanceFrameRenderGate,
    // 0x7FF7CB44755E..0x7FF7CB4475DD / x86 0x46B0B1..0x46B117, called at the
    // top of FrameDriver) refreshes it to 1 while playing ([0x368]/[0x330]),
    // while the idle-physics suppress toggle is OFF ([0xA54CC] / x86
    // 0xA4420 = FrameVolumeControlEnabled, menu 299) or while the modeless
    // model-edge dialog is open ([0xA1B98] / x86 0xA0B50 = FrameRangeDialog,
    // menu 259); otherwise it walks 1->2->3->0.  0x7FF7CB44B8E7
    // (`cmp [app+0xA1E18],0 / jz -> 0x7FF7CB44C8D6`, x86 twin 0x46EFBE)
    // skips the ENTIRE physics section - wind, gravity, settle request,
    // pose passes, world pass and readback - once it has decayed to 0, i.e.
    // two pump passes after the last trigger dropped.  A mouse hovering the
    // window keeps it alive through WM_MOUSEMOVE, like the original.
    // (PlaybackCatchup is not behind this gate in the port: the x64
    // catch-up blocks share it but can only run while playing, which
    // already holds the counter open.)
    if (s.state.messageSeen == 0)
        return;                       // 0x7FF7CB44B8E7 -> 0x7FF7CB44C8D6
    PhysicsScene* scene = s.Physics();
    if (scene == nullptr)
        return;
    btDiscreteDynamicsWorld* world = scene->world;   // slot 64 / 0x40
    if (world == nullptr)
        return;
    const int count = s.PlaybackPhysicsMode();
    // ---- random-wind / gravity-noise block (0x46F582..0x46F6B4) ----------
    // With the noise mode byte 0xA0CD4 set, this REPLACES the normal
    // per-pass gravity apply (both branches jump to 0x46F7DB, right past
    // the normal setGravity at 0x46F7D9 - everything else in the pass,
    // including the settle steps, still runs): the wind timer (0x9EDCC)
    // accumulates the real dt and every 0.5s a gravity perturbation is
    // applied through world vtable slot
    // 0x34 (setGravity - the MMD "wind" is a randomized gravity vector).
    //   strength = rand()*10.0/32767.0 * [0x9EDC8] + [0x9EDC4]
    //   axis     = (rand()*0.2/32767.0 - 0.100000001490116 + [0x9EDBx])
    //              * strength   (x87 doubles, float stores; constants
    //                          0x52C170/0x52E9F8/0x52E9F0/0x52BEA8)
    // x64 (0x7FF7CB44BECF..0x7FF7CB44BF98) computes the same chain in
    // single precision (cvtdq2ps + mulss/divss/addss) with float
    // constants: divisor 0x7FF7CB552D30 = 0x46FFFE00 = 32767.0f EXACTLY
    // (an earlier audit note read this as 32768.0f/0x47000000 - that is a
    // misread; the bytes are 00 FE FF 46), 0.2f at 0x7FF7CB552D2C, 0.1f
    // at 0x7FF7CB552B38 and 10.0f preloaded from 0x7FF7CB552980.  The
    // divisor below therefore stays 32767.0 in BOTH paths; only the
    // arithmetic precision differs (see the #if paths).
    // Vector element order is (0x9EDB8, 0x9EDBC, 0x9EDC0, 0).
    bool windRan = false;
    if (count > 0 && s.state.gravityNoiseEnabled != 0) {
        // x64 0x7FF7CB44BEA4..0x7FF7CB44BEC9: the timer accumulates with a
        // single float add (addss, result stored back unconditionally) and
        // the compare is `comiss xmm0,[0x7FF7CB55298C=0.5f]; jbe skip` -
        // the perturbation fires only when the timer is STRICTLY greater
        // than 0.5; an exactly-0.5 timer does not fire.
        s.state.gravityNoiseTimer = s.state.gravityNoiseTimer + s.DeltaTime();
        if (s.state.gravityNoiseTimer > 0.5f) {                          // flt_52960C
#if defined(_M_IX86)
            // x87 transcription of 0x46F5BA..0x46F66E: every intermediate
            // stays extended; strength is stored to a FLOAT slot at
            // 0x46F5DF and reloaded for each component multiply.
            const int strengthInt =
                s.GravityNoise();          // fimul (integer)
            const float gravmag = s.GravityMagnitude();
            static const double kTen = 10.0;            // dbl_52C170
            static const double kRandMax = 32767.0;     // dbl_52E9F8
            static const double kNoise = 0.20000000298023224;   // 52E9F0
            static const double kOffset = 0.10000000149011612;  // 52BEA8
            int rStrength = std::rand(), rX = std::rand(),
                rY = std::rand(), rZ = std::rand();
            float strengthF, e0, e1, e2;
            const float dirX = s.GravityX(),
                        dirY = s.GravityY(),
                        dirZ = s.GravityZ();
            __asm {
                fild        rStrength
                fmul        qword ptr [kTen]
                fdiv        qword ptr [kRandMax]
                fimul       strengthInt
                fadd        gravmag
                fstp        strengthF
                fild        rX
                fmul        qword ptr [kNoise]
                fdiv        qword ptr [kRandMax]
                fsub        qword ptr [kOffset]
                fadd        dirX
                fmul        strengthF
                fstp        e0
                fild        rY
                fmul        qword ptr [kNoise]
                fdiv        qword ptr [kRandMax]
                fsub        qword ptr [kOffset]
                fadd        dirY
                fmul        strengthF
                fstp        e1
                fild        rZ
                fmul        qword ptr [kNoise]
                fdiv        qword ptr [kRandMax]
                fsub        qword ptr [kOffset]
                fadd        dirZ
                fmul        strengthF
                fstp        e2
            }
            world->setGravity(btVector3(e0, e1, e2));
            s.state.gravityNoiseTimer = 0.0f;                     // 0x46F6AC
#else
            // x64 0x7FF7CB44BECF..0x7FF7CB44BFB7: the SAME chain as the
            // x87 block but in single precision - cvtdq2ps + mulss 10.0f
            // + divss 32767.0f (0x46FFFE00) for the strength, then per
            // axis mulss 0.2f / divss 32767.0f / subss 0.1f / addss dir /
            // mulss strength.  The strength stays a float value end to
            // end (no double round-trip).
            const int rStrength = std::rand();
            const float noise =
                static_cast<float>(s.GravityNoise());
            const float gravmag = s.GravityMagnitude();
            const float strength =
                ((static_cast<float>(rStrength) * 10.0f) / 32767.0f) *
                    noise + gravmag;
            const float e0 =
                ((static_cast<float>(std::rand()) * 0.2f) / 32767.0f -
                 0.1f + s.GravityX()) * strength;
            const float e1 =
                ((static_cast<float>(std::rand()) * 0.2f) / 32767.0f -
                 0.1f + s.GravityY()) * strength;
            const float e2 =
                ((static_cast<float>(std::rand()) * 0.2f) / 32767.0f -
                 0.1f + s.GravityZ()) * strength;
            world->setGravity(btVector3(e0, e1, e2));
            s.state.gravityNoiseTimer = 0.0f;                     // 0x46F6AC
#endif
        }
        windRan = true;  // -> 0x46F7DB: jump past the normal gravity apply
    }
    // ---- normal per-pass gravity apply (0x46F6B9..0x46F7D9) ------------
    // Runs every physics pass with the noise mode off: an all-zero
    // direction gets Y nudged to 0.1f (flt_0x529624), the direction goes
    // through the real D3DXVec3Normalize, and each gravity component is
    // (n_i*mag)*10.0f.  x64 0x7FF7CB44C0C1..0x7FF7CB44C0E9 runs that
    // product as two mulss (mag, then the preloaded 10.0f) - single
    // precision, one rounding per multiply.  The wind block above jumps
    // to 0x46F7DB - right past this setGravity - and physics-off
    // (A0CC4<=0) skips both.
    if (count > 0 && !windRan) {
        float dir[3] = {s.state.gravityX,
                        s.state.gravityY,
                        s.state.gravityZ};
        if (dir[0] == 0.0f && dir[1] == 0.0f && dir[2] == 0.0f)
            // x64 0x7FF7CB44C03A: 三轴 ucomiss 判全零后
            // `mov dword ptr [app+0x9FCC8], 3DCCCCCDh` - 0.1f 直接写回
            // 重力 Y 状态字段，不只是本地副本：后续风力 Y 基准、PMM 保存、
            // 重力对话框读到的都是被拨正后的 0.1。
            s.state.gravityY = dir[1] = 0.1f;                  // flt 0x529624
        D3dxVec3Normalize(dir, dir);                          // 0x46F741
        const float mag = s.state.gravityMagnitude;
        world->setGravity(btVector3(
            (dir[0] * mag) * 10.0f,
            (dir[1] * mag) * 10.0f,
            (dir[2] * mag) * 10.0f));                         // 0x46F7D9
    }
    // 0x46F575: `cmp [ebx+0xA0CC4], 0 / jle 0x46F7DB` - A0CC4 == 0
    // ("physical operation: no calculation") skips the random-wind block
    // AND the normal gravity apply (both jump to 0x46F7DB) and continues
    // into the settle request, the pose passes and the readback.
    // The early `return` the port used to have here skipped the second
    // pose pass, leaving the +0x16C/+0x178 pose block on the physics
    // source instead of restoring the kinematic one (proven by the
    // AllStar physics-off A/B: orig F-pose = identity, port F-pose =
    // the physics-source quaternion).
    unsigned char** models = s.ModelSlots();
#ifdef MIKUDANCESTUDIO_DIAG
    char stageCaptureValue[2]{};
    static LONG stagesCaptured = 0;
    // Arm on the first executable physics pass, including the synchronous
    // PMM-load pass.  Waiting for optflag[0] to clear observes a world that has
    // already been stepped and makes the original/MikuDanceStudio solver inputs
    // incomparable.
    const bool captureStages =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_AB_PHYSICS_STAGES",
                                stageCaptureValue,
                                sizeof(stageCaptureValue)) == 1 &&
        stageCaptureValue[0] == '1' &&
        // Startup executes an empty-scene physics pass before a PMM is
        // chosen.  Do not consume the one-shot capture there: the useful
        // baseline is the first pass that actually owns model rigid bodies.
        s.ModelSlots()[0] != nullptr &&
        InterlockedCompareExchange(&stagesCaptured, 1, 0) == 0;
    if (captureStages)
        ArmBoneTransformIkProbe();
    if (captureStages)
        DumpWorldSolverInfo(world);
#endif

    // 0x46F7DB..0x46F808: the settle request sits inside the
    // "selection active" gate - var_14A1 (selActive, from FrameDriver
    // 0x46DCA6) && app+0x2F8 == 0 && app+0x9ED90 == 0.  Without the gate
    // idle load frames keep re-requesting the 3x settle passes, stepping
    // the world four times per frame instead of once (proven by the
    // body-3 origin write-watch: original alternates proceed/sync, the
    // port emitted two proceedToTransform writes per sync).
    if (selActive != 0 &&
        s.state.optflag[0] == 0 &&        // 0x2F8 model-mode flag
        s.state.frameStepPlayback == 0) {     // 0x9ED90 frame-step flag
        // 0x46F7FF..0x46F808 / x64 0x7FF7CB44C12E..0x7FF7CB44C138：
        // 两版同一上升沿闩锁语义。x86 `cmp byte [app+0xA03E8],0 /
        // jnz 0x46F80F / mov byte [app+0x9EDB5],1`，x64 `cmp
        // [app+0xA137C],r15b / jnz / mov byte [app+0x9FCC1],1`。
        // 0xA03E8 与 0xA137C 都是上一趟泵的 selActive 滞留闩锁（x86 泵尾
        // 0x47985A..0x47985E、x64 泵尾 0x7FF7CB456F1C..0x7FF7CB456F21 把
        // 本趟 selActive 写回；初始化 x86 0x40ABDE / x64 0x7FF7CB42CA4C
        // 清零；layout_pins.hpp 把 selectionActiveLatch 钉在
        // 0xA03E8/0xA137C）。0xA03E8 不是"未播放"独立条件——2026-09 审计
        // 把闩锁本身误读成了第三个丢失条件。settle 只在 selActive 上升沿
        // （上一趟未激活）请求一次；持续激活期间闩锁抑制重复请求。两版
        // 唯一差异在泵尾写回的外围门（x86 0x4797C2 套 model+0x2D90>=0 &&
        // app+0x330==0，x64 无条件），见文件头注释与 frame_driver.cpp。
        if (s.state.selectionActiveLatch == 0)
            s.PhysicsResetPending() = 1;
    }

    // ---- gate B: accessory-edit dialog flag (x64 0x7FF7CB44C65B) --------
    // `cmp [app+0xA166D],0 / jnz -> 0x7FF7CB44C8D6` (the same landing label
    // gate A jumps to) skips ONLY the two pose passes, the world pass, the
    // readback and the moved-flag clear below.  The gravity/wind blocks and
    // the settle request above have already run by then - the previous
    // top-of-function early return on accessoryEditDialogOpen (which also killed gravity and
    // the settle request) did not match x64.
    if (s.state.accessoryEditDialogOpen != 0)
        return;                       // 0x7FF7CB44C65B -> 0x7FF7CB44C8D6

#ifdef MIKUDANCESTUDIO_DIAG
    int stepCount = 0;
    const char* stepLabels[16]{};
    const bool traceSteps = getenv("MIKUDANCESTUDIO_TRACE_STEPS") != nullptr;
    if (traceSteps) {
        // m_localTime = the stepSimulation substep accumulator (protected
        // member of btDiscreteDynamicsWorld); use the owned world interface.
        const float localTime =
            static_cast<const PhysicsWorld*>(world)->SubstepRemainder();
        unsigned localBits = 0;
        std::memcpy(&localBits, &localTime, 4);
        fprintf(stderr,
                "PHYSFRAME enter settle=%d count=%d localTime=%08X\n",
                (int)(s.PhysicsResetPending() != 0), count,
                localBits);
    }
    auto stepWorld = [world, &stepCount, &stepLabels](const char* label) {
        if (stepCount < 16) stepLabels[stepCount] = label;
        ++stepCount;
        world->stepSimulation(1.0f / 60.0f, 10, 1.0f / 60.0f);  // 0x46F206
    };
#else
    auto stepWorld = [world](const char*) {
        world->stepSimulation(1.0f / 60.0f, 10, 1.0f / 60.0f);  // 0x46F206
    };
#endif

    // var_14C8 - the MAIN step's timeStep.  Initialised in PlaybackCatchup
    // (0x46EFDE = app+0xA06BC, overwritten with 1/fps in frame-step mode at
    // 0x46F00D) and DECREMENTED by 1/60 per catch-up substep (0x46F226 and
    // the BLOCK 2 twin): the settle main step steps only the REMAINDER of
    // the frame's dt, not the full dt again.  Stepping the full dt here
    // double-counts the catch-up substeps - in recording mode that is a
    // 1.5x physics overdrive every pass, which progressively destabilises
    // the spring/constraint solver (the ③-C upside-down explosion).
    // The extra step (0x46FD7F) and the settle steps (0x46FE0C) push
    // flt_52EA00 for BOTH timeStep and fixedTimeStep - they stay 1/60.
    float mainTimeStep = g_CatchupDtBudget;
#ifdef MIKUDANCESTUDIO_DIAG
    // DIAGNOSTIC ONLY: force the main step's timeStep to probe which
    // per-pass dt the original effectively runs at in real time.
    if (const char* forced = getenv("MIKUDANCESTUDIO_FORCE_MAINDT"))
        mainTimeStep = static_cast<float>(atof(forced));
    // DIAGNOSTIC ONLY: zero the substep accumulator after the first
    // (load) physics pass, isolating the load-pass main substep from the
    // idle evolution.
    static bool zeroLocalTimeAfterLoad =
        getenv("MIKUDANCESTUDIO_ZERO_LT_AFTER_LOAD") != nullptr;
#endif

    // Per-model morph application + pose source selection, ordered by the
    // PMM model-display order (the x86 field was model+0x2D7D).
    const int posePassMode = s.PhysicsResetPending() != 0
                       ? 0
                       : count;
    // x64 pump sub_7FF7CB4474F0: both pose passes run order and slot walks
    // to 255 (pass 1: cmp rbx/edi, 0FFh at 0x7FF7CB44C69A/0x7FF7CB44C6DD;
    // pass 2: 0x7FF7CB44C89A/0x7FF7CB44C8CE; comboSelIndex2 = model+0x3109).
    for (int order = 0; order < kModelSlotCount; ++order) {
        for (int j = 0; j < kModelSlotCount; ++j) {
            unsigned char* mdl = models[j];
            if (mdl != nullptr && mdl::Mdl(mdl)->comboSelIndex2 == order) {
                ModelApplyMorphs(mdl);                            // 0x46FCB1
                SetPhysicsMode(mdl, 0, models, posePassMode);
            }
        }
    }
#ifdef MIKUDANCESTUDIO_DIAG
    if (captureStages)
        DumpFrameEntryState(app, "physics_pose0.json", false);
#endif

    // Settle section gate (0x46FCEF..0x46FD39 `test eax,ecx /
    // jz loc_46FE5F`; x64 0x7FF7CB44C6E5..0x7FF7CB44C73D,
    // `test eax,edx / jz 0x7FF7CB44C85F`): moved | settle | frame advanced,
    // model count 1..3, no seek pending (x86 0xA0B74 / x64
    // app+0xA1BC8 == 0).
    const bool moved = s.state.physicsBodiesMoved != 0;
    const bool settle = s.PhysicsResetPending() != 0;
    const bool frameAdv = s.PlaybackActive() != 0;           // 0x330
    const bool runWorldPass = count == 1 || count == 2 ||
        (count == 3 && (moved || settle || frameAdv));
#ifdef MIKUDANCESTUDIO_DIAG
    // DIAGNOSTIC ONLY (never set in production): MIKUDANCESTUDIO_IDLE_NO_STEP=1
    // skips the world pass entirely on passes with no settle/moved/frame
    // activity, freezing the post-seek state so a capture can tell whether
    // a rendering divergence originates in the seek pass itself or in the
    // idle substep evolution that follows it.
    const bool idleNoStep = getenv("MIKUDANCESTUDIO_IDLE_NO_STEP") != nullptr &&
        !(moved || settle || frameAdv);
#else
    constexpr bool idleNoStep = false;
#endif
    // NOTE: the wind block's jump (0x46F6B4 -> 0x46F7DB) skips only the
    // normal setGravity (handled by the !windRan gate above).  The world
    // pass itself runs identically with or without wind - do NOT add
    // !windRan here, that freezes physics while the noise mode is active.
    if (runWorldPass && !idleNoStep &&
        s.state.frameCopyDialog == 0) {
        // x64 twin: 255 count-down walk (mov edi, 0FFh at 0x7FF7CB44C74B).
        for (int j = 0; j < kModelSlotCount; ++j)
            if (models[j] != nullptr)
                ModelKinematicSync(models[j]);              // 0x46FD4D
#ifdef MIKUDANCESTUDIO_DIAG
        if (captureStages)
            DumpRigidBodyState(app, "physics_sync.rigids.json");
        if (captureStages)
            DumpFrameEntryState(app, "physics_sync.json", false);
#endif

        // 0x46FDBB..0x46FDCA: the single-step path pushes var_14C8 as
        // timeStep (fixedTimeStep stays flt_52EA00 = 1/60).  In the
        // moved && count==3 path (0x46FD7F) BOTH the main and extra
        // steps push flt_52EA00 - var_14C8 is skipped entirely there.
        const float mainDt =
            (moved && count == 3) ? (1.0f / 60.0f) : mainTimeStep;
#ifdef MIKUDANCESTUDIO_DIAG
        if (stepCount < 16) stepLabels[stepCount] = "main";
        ++stepCount;
#endif
        world->stepSimulation(mainDt, 10, 1.0f / 60.0f);
        if (moved && count == 3)
            stepWorld("extra");                              // 0x46FD97
#ifdef MIKUDANCESTUDIO_DIAG
        if (captureStages)
            DumpRigidBodyState(app, "physics_step.rigids.json");
        if (captureStages)
            DumpFrameEntryState(app, "physics_step.json", false);
#endif

        if (settle) {
            for (int iter = 3; iter >= 1; --iter) {
                // x64 twin: 255 count-down walk (mov edi, 0FFh at 0x7FF7CB44C7E8).
                for (int j = 0; j < kModelSlotCount; ++j)
                    if (models[j] != nullptr)
                        ModelDynamicReseat(models[j]);      // 0x46FDF7
#ifdef MIKUDANCESTUDIO_DIAG
                if (captureStages) {
                    char name[64]{};
                    std::snprintf(name, sizeof(name),
                                  "physics_settle_pre%d.rigids.json", 4 - iter);
                    DumpRigidBodyState(app, name);
                }
#endif
                stepWorld("settle");
#ifdef MIKUDANCESTUDIO_DIAG
                if (captureStages) {
                    char name[64]{};
                    std::snprintf(name, sizeof(name),
                                  "physics_settle_post%d.rigids.json", 4 - iter);
                    DumpRigidBodyState(app, name);
                }
#endif
            }
            s.PhysicsResetPending() = 0;
        }
#ifdef MIKUDANCESTUDIO_DIAG
        if (captureStages)
            DumpRigidBodyState(app, "physics_settle.rigids.json");
        if (captureStages)
            DumpFrameEntryState(app, "physics_settle.json", false);
#endif

        // 0x46FE34..0x46FE58 / x64 0x7FF7CB44C834..0x7FF7CB44C857: the
        // readback walk and the moved clear sit INSIDE the settle gate in
        // BOTH layouts.  The x86 gate failure 0x46FD39 `jz loc_46FE5F`
        // and the x64 0x7FF7CB44C73D `jz 0x7FF7CB44C85F` both land on
        // the second pose pass, PAST the readback walk (x86 100-slot walk
        // 0x46FE42..0x46FE56, sub_4B25D0 per model; x64 255-slot walk at
        // 0x7FF7CB44C83C, sub_7FF7CB4E3470 per model) and past the moved
        // clear (x86 `mov byte [app+0xA066Ch],0` at 0x46FE58; x64
        // `mov [app+0xA1678h],r15b` at 0x7FF7CB44C857).  The only entries
        // into x86 0x46FE34 are the in-gate branches 0x46FDD3 (settle
        // skip) and 0x46FE2D (settle-loop exit).  (The old comment here
        // claimed x86 ran them outside the gate - a misread; no per-arch
        // branch is needed.)
        for (int j = 0; j < kModelSlotCount; ++j)
            if (models[j] != nullptr)
                ModelPhysicsReadback(models[j]);            // 0x46FE49
#ifdef MIKUDANCESTUDIO_DIAG
        if (captureStages)
            DumpRigidBodyState(app, "physics_readback.rigids.json");
        if (captureStages)
            DumpFrameEntryState(app, "physics_readback.json", false);
#endif
        s.state.physicsBodiesMoved = 0;                       // 0x7FF7CB44C857
    }

#ifdef MIKUDANCESTUDIO_DIAG
    if (traceSteps && stepCount > 0) {
        fprintf(stderr, "PHYSFRAME steps=%d settle=%d moved=%d count=%d labels=",
                stepCount, (int)settle, (int)moved, count);
        for (int i = 0; i < stepCount && i < 16; ++i)
            fprintf(stderr, "%s%s", i ? "," : "",
                    stepLabels[i] ? stepLabels[i] : "?");
        fprintf(stderr, "\n");
    }
#endif

    // Second pose pass with afterPhysics = 1 and physicsMode = model
    // count (0x46FE5F; x64
    // 0x7FF7CB44C85F..0x7FF7CB44C8D4, morph call / SetPhysicsMode twin at
    // 0x7FF7CB44C8A8/0x7FF7CB44C8C7).  x64 keeps this pass INSIDE gate B -
    // it is skipped while the accessory-edit dialog holds physics.
    for (int order = 0; order < kModelSlotCount; ++order) {
        for (int j = 0; j < kModelSlotCount; ++j) {
            unsigned char* mdl = models[j];
            if (mdl != nullptr && mdl::Mdl(mdl)->comboSelIndex2 == order) {
                ModelApplyMorphs(mdl);                            // 0x46FE93
                SetPhysicsMode(mdl, 1, models, count);
            }
        }
    }
#ifdef MIKUDANCESTUDIO_DIAG
    if (zeroLocalTimeAfterLoad) {
        static_cast<PhysicsWorld*>(world)->ClearSubstepRemainder();
        zeroLocalTimeAfterLoad = false;
    }
    if (captureStages)
        DumpFrameEntryState(app, "physics_pose1.json", false);
#endif
}

}  // namespace mikudancestudio
