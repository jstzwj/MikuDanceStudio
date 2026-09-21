// ===========================================================================
// VA 0x00493A60 - BoneFrameTransform  (original: sub_493A60, 0x364D bytes)
// ===========================================================================
// The per-frame bone transform updater, __thiscall on the model block.
// Called once per bone transform layer by SetPhysicsMode:
//
//   BoneFrameTransform(model, afterPhysics(a2), layer(a3),
//                        modelSlots(a4), physicsMode(a5))
//     afterPhysics  channel router: 0 -> bones WITHOUT flag 0x1000 (before
//                    physics), 1 -> bones WITH flag 0x1000 (after physics)
//     layer          frame/layer selector - compared against bone+496
//     modelSlots     model-slot array (app storage+1920); used for external-
//                    parent cross-model chains (model+314596 table).  Null
//                    at plain loads.
//     physicsMode    selector, same value as 0x4A9220's physicsMode:
//                    1 = kinematic-all, >=2 = per-bone gate via bone+493.
//
// Five phases (original addresses):
//   A 0x493A85  local matrix for physics-swept bones (+596 flag set):
//     inherit 0x100 from bone+488's +116, own quat +376, 0x200 translation
//     inherit -> bone+116 = T(-rest308) * R * T(off364) * T(rest308).
//   B 0x493E71  local matrix for the rest: type 4 resets scale +348..360,
//     type 9 axis-angle from tail(+460) quat x rate(int)(+488)/100,
//     type 5 quat = parent(+488)quat * own, 0x100 inherit; after the
//     sandwich, 0x200 bones add the inherited translation to +364.
//   C 0x49455F  world pass: external parent (bone+600 -> model+314596
//     table {slot@+12, bone@+16}) chains the OTHER model's bone+244;
//     else +52 = +116 * parent(+48)+52; +180 = parent world rotation-only
//     mirror; root keeps +52 = +116 and +180 = identity.  +244 copy gate
//     ((physicsMode==1 || (physicsMode>=2 && bone493==0)) & bone492) == 0.
//   D 0x494A56  CCD IK per 24-byte chain at model+9920
//     {+0 effector, +4 root, +8 links, +12 child*, +16 loops, +18 enabled,
//      +20 angle}: effector/root/link world pos from +52 rows, rotation
//     axis = normalize(link-root) x normalize(link-effector), half-angle
//     acos(dot)*0.5*axisAlignment clamped to +-2*chainAngle*(linkIdx+1),
//     accumulated in quat +348 (prepended with +376 on iteration 0).
//     PMX (model+14590==2) link limits via bone+568 flag and +572..592
//     planes (X/Y/Z-primacy decompositions); PMD constrains the axis by
//     projection on bone+180 rows, and 左ひざ/右ひざ knees get the hinge
//     flip (negate m12/m21 and quat.x when m21 < 0).  After each link the
//     chain's world matrices are recomputed bottom-up.
//   E 0x496881  post-IK pass for +596 bones (type != 4): type 9/5 take
//     the source quat from tail(+460)/parent(+488) using +348 when that
//     source bone is type 4 (IK-touched), rebuild +116 and world +52.
//
// Fresh PMD loads run with all pose quats zeroed; the D3DX rotation
// formulas degenerate zero quats to identity, so the model stays in bind
//
// Layout: the five passes live in file-local functions at the original
// phase boundaries; BoneFrameTransform computes the shared locals and
// calls them in order (the two gate lambdas of the original body became
// ChannelPass/CopyGate with the captured parameters explicit):
//   BoneTransform_PhysicsSweptLocals  A 0x493A85
//   BoneTransform_StandardLocals      B 0x493E71
//   BoneTransform_WorldPass           C 0x49455F
//   BoneTransform_CcdIk               D 0x494A56
//   BoneTransform_PostIkPass          E 0x496881
// pose exactly like the original.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/model.hpp"

namespace mikudancestudio {
namespace {

using d3dx::D3DXMATRIXF;

#ifdef MIKUDANCESTUDIO_DIAG
// ---- IK CCD probe under MIKUDANCESTUDIO_STATE_DUMP_DIR --------------------
// (porting-era A/B tooling; see ../app/frame_state_dump.hpp for the gate.
//  Lives in the hottest IK loop, so the OFF build must carry none of it.)

LONG gIkProbeArmed = 0;
LONG gIkProbeWritten = 0;

void WriteFloatBits(std::FILE* stream, const float* values, int count) {
    std::fputc('[', stream);
    for (int i = 0; i < count; ++i) {
        if (i != 0) std::fputc(',', stream);
        std::uint32_t bits = 0;
        std::memcpy(&bits, values + i, sizeof(bits));
        std::fprintf(stream, "\"%08X\"", bits);
    }
    std::fputc(']', stream);
}

void WriteIkProbe(unsigned char* model, int chainIndex, int iteration,
                  int linkIndex, int loopCount, int targetIndex, int rootIndex,
                  int boneIndex, const float d1[3], const float d2[3],
                  const float axis[3], float dot, float half,
                  const float acc[4], const float rotq[4]) {
    if (InterlockedCompareExchange(&gIkProbeArmed, 0, 0) == 0 ||
        chainIndex != 0)
        return;
    const LONG recordIndex = InterlockedIncrement(&gIkProbeWritten);
    if (recordIndex > 512)
        return;
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\ik_trace.jsonl", directory);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, recordIndex == 1 ? "wb" : "ab") != 0 ||
        stream == nullptr)
        return;
    std::fprintf(stream,
        "{\"bone_count\":%d,\"chain\":%d,\"iteration\":%d,\"loops\":%d,"
        "\"link_position\": %d, \"target\": %d, \"root\": %d, "
        "\"bone\":%d,\"d1_bits\":",
        static_cast<int>(mdl::Mdl(model)->boneCount), chainIndex, iteration,
        loopCount,
        linkIndex,
        targetIndex, rootIndex, boneIndex);
    WriteFloatBits(stream, d1, 3);
    std::fputs(",\"d2_bits\":", stream);
    WriteFloatBits(stream, d2, 3);
    std::fputs(",\"axis_bits\":", stream);
    WriteFloatBits(stream, axis, 3);
    std::fputs(",\"dot_bits\":", stream);
    WriteFloatBits(stream, &dot, 1);
    std::fputs(",\"half_bits\":", stream);
    WriteFloatBits(stream, &half, 1);
    std::fputs(",\"acc_bits\":", stream);
    WriteFloatBits(stream, acc, 4);
    std::fputs(",\"rotation_bits\":", stream);
    WriteFloatBits(stream, rotq, 4);
    std::fputs("}\n", stream);
    std::fclose(stream);
}

void WriteIkPrecheck(unsigned char* model, int chainIndex, int iteration,
                     int linkIndex, const float d1[3], const float d2[3],
                     float diff2, const mdl::BoneRecord& target,
                     const mdl::BoneRecord& root,
                     const mdl::BoneRecord& link) {
    if (InterlockedCompareExchange(&gIkProbeArmed, 0, 0) == 0 ||
        chainIndex != 0 || iteration != 1 || linkIndex != 0)
        return;
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\ik_precheck.json", directory);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, "wb") != 0 || stream == nullptr)
        return;
    std::fputs("{\n  \"d1_bits\": ", stream);
    WriteFloatBits(stream, d1, 3);
    std::fputs(",\n  \"d2_bits\": ", stream);
    WriteFloatBits(stream, d2, 3);
    std::uint32_t diffBits = 0;
    std::memcpy(&diffBits, &diff2, sizeof(diffBits));
    std::fprintf(stream, ",\n  \"diff2_bits\": \"%08X\"", diffBits);
    const mdl::BoneRecord* records[] = {&target, &root, &link};
    const char* names[] = {"target_matrix_bits", "root_matrix_bits",
                           "link_matrix_bits"};
    for (int record = 0; record < 3; ++record) {
        std::fprintf(stream, ",\n  \"%s\": [", names[record]);
        for (int i = 0; i < 16; ++i) {
            if (i != 0) std::fputc(',', stream);
            std::uint32_t bits = 0;
            std::memcpy(&bits, records[record]->matInit + i, sizeof(bits));
            std::fprintf(stream, "\"%08X\"", bits);
        }
        std::fputc(']', stream);
    }
    std::fputs("\n}\n", stream);
    std::fclose(stream);
}

#endif  // MIKUDANCESTUDIO_DIAG

// The original's truncated "pi": 0x4048F5C3 (x64 dword_7FF7CB552B90,
// 3.1400001f - NOT float(pi), the mantissa-truncated constant both
// binaries carry).
constexpr float kPiF = 3.140000104904175f;
constexpr float kEps = 1.1920929e-7f;              // 0x493BF7 FLT_EPSILON
constexpr float kHalfPi = 1.570796012878418f;      // 0x495947 (float pi/2)
constexpr float kKneeClamp = 1.535889f;            // 0x4959CC

// 左ひざ / 右ひざ (SJIS), 6-byte compares at 0x4955F1/0x495620 and
// 0x496468/0x496483; string constants 0x530F60 / 0x530F58.
const char kNameKneeLJp[7] = "\x8d\xb6\x82\xd0\x82\xb4";
const char kNameKneeRJp[7] = "\x89\x45\x82\xd0\x82\xb4";

bool IsKnee(const mdl::BoneRecord& bone) {
    return std::strncmp(bone.name,
                        kNameKneeLJp, 6) == 0 ||
           std::strncmp(bone.name,
                        kNameKneeRJp, 6) == 0;
}

// D3DX imports preserve the runtime's floating-point behavior.

void MatRotQuat(D3DXMATRIXF* out, const float q[4]) {
    d3dx::Get().matrixRotationQuaternion(out, q);
}

void Vec3Norm(float out[3], const float in[3]) {
    d3dx::Get().vec3Normalize(out, in);
}

// D3DXQuaternionMultiply semantics: out = q2 * q1.
void QuatMul(float out[4], const float q1[4], const float q2[4]) {
    d3dx::Get().quatMultiply(out, q1, q2);
}

struct D3 {
    d3dx::Api* api = &d3dx::Get();

    void rotX(D3DXMATRIXF* o, float a) { api->rotX(o, a); }
    void rotY(D3DXMATRIXF* o, float a) { api->rotY(o, a); }
    void rotZ(D3DXMATRIXF* o, float a) { api->rotZ(o, a); }
    void mul(D3DXMATRIXF* o, const D3DXMATRIXF* a, const D3DXMATRIXF* b) {
        api->multiply(o, a, b);
    }
    void translation(D3DXMATRIXF* o, float x, float y, float z) {
        std::memset(o, 0, sizeof(*o));
        o->m[0][0] = o->m[1][1] = o->m[2][2] = 1.0f;
        o->m[3][0] = x;
        o->m[3][1] = y;
        o->m[3][2] = z;
        o->m[3][3] = 1.0f;
    }
    void quatFromMatrix(float out[4], const D3DXMATRIXF* m) {
        api->quatFromMatrix(out, m);
    }

};

// ---- shared pieces --------------------------------------------------------

// quat of the source bone's +116 matrix scaled about its own axis by
// `rate`, post-multiplied onto q (0x493B12..0x493CA4 pattern).
// x64 (0x7FF7CB4DA373..0x7FF7CB4DA476): acosf/sqrtf/sinf/cosf and a
// divss sin/len - single precision end to end; kPiF is the float image
// 0x4048F5C3 (dword_7FF7CB552B90).
void InheritRotQuat(float q[4], const mdl::BoneRecord* bones, int srcIdx,
                    float rate) {
    D3 d3;
    D3DXMATRIXF rot;
    std::memcpy(&rot, bones[srcIdx].matLocal, sizeof(rot));
    float sq[4];
    d3.quatFromMatrix(sq, &rot);
    if (sq[3] <= 1.0f) {
        if (sq[3] < -1.0f) sq[3] = -1.0f;
    } else {
        sq[3] = 1.0f;
    }
    float angle = std::acos(sq[3]);
    if (sq[3] < 0.0f)
        angle -= kPiF;
    const float len = std::sqrt(sq[0] * sq[0] + sq[1] * sq[1] +
                                sq[2] * sq[2]);
    float part[4];
    if (angle == 0.0f || len < kEps) {
        part[0] = 0.0f;
        part[1] = 0.0f;
        part[2] = 0.0f;
        part[3] = 1.0f;
    } else {
        const float scaled = angle * rate;
        const float s = std::sin(scaled) / len;
        part[0] = s * sq[0];
        part[1] = s * sq[1];
        part[2] = s * sq[2];
        part[3] = std::cos(scaled);
    }
    float out[4];
    QuatMul(out, part, q);
    q[0] = out[0];
    q[1] = out[1];
    q[2] = out[2];
    q[3] = out[3];
}

// world = local * parentWorld; matWorld is the parent rotation-only mirror.
void WorldFromParent(mdl::BoneRecord& bone, const mdl::BoneRecord& parent,
                     D3* d3) {
    D3DXMATRIXF local, pw, t;
    std::memcpy(&local, bone.matLocal, sizeof(local));
    std::memcpy(&pw, parent.matInit, sizeof(pw));
    d3->mul(reinterpret_cast<D3DXMATRIXF*>(bone.matInit), &local, &pw);
    std::memcpy(&t, parent.matInit, sizeof(t));
    t.m[3][0] = t.m[3][1] = t.m[3][2] = 0.0f;
    t.m[3][3] = 1.0f;
    std::memcpy(bone.matWorld, &t, sizeof(t));
}

// World position of `bone`: the matrix at +52 applied to the rest position
// at +308.  The original v63[13]/[17]/[21]/[25] indices were relative to
// the bone record; `mw` is already rebased to +52, so they become 0/4/8/12.
void WorldPos(float dst[3], const mdl::BoneRecord& bone) {
    const float* mw = bone.matInit;
    const float* rest = bone.position;
    // 0x494B5D..0x494BD1 and 0x494D3A..0x494E4C use x87 and keep
    // each multiply/add chain in extended precision until the final fstp.
    // MSVC's SSE expression evaluation rounds after every operation and a
    // few ULP here become visible after many CCD iterations.
#if defined(_M_IX86)
    __asm {
        mov eax, mw
        mov edx, rest
        mov ecx, dst

        fld dword ptr [eax]
        fmul dword ptr [edx]
        fld dword ptr [eax+16]
        fmul dword ptr [edx+4]
        faddp st(1), st
        fld dword ptr [eax+32]
        fmul dword ptr [edx+8]
        faddp st(1), st
        fadd dword ptr [eax+48]
        fstp dword ptr [ecx]

        fld dword ptr [eax+4]
        fmul dword ptr [edx]
        fld dword ptr [eax+20]
        fmul dword ptr [edx+4]
        faddp st(1), st
        fld dword ptr [eax+36]
        fmul dword ptr [edx+8]
        faddp st(1), st
        fadd dword ptr [eax+52]
        fstp dword ptr [ecx+4]

        fld dword ptr [eax+8]
        fmul dword ptr [edx]
        fld dword ptr [eax+24]
        fmul dword ptr [edx+4]
        faddp st(1), st
        fld dword ptr [eax+40]
        fmul dword ptr [edx+8]
        faddp st(1), st
        fadd dword ptr [eax+56]
        fstp dword ptr [ecx+8]
    }
#else
    // x64 original: same source compiled for SSE2 - every multiply/add
    // rounds to float, which the plain float expression reproduces.
    dst[0] = mw[0] * rest[0] + mw[4] * rest[1] + mw[8] * rest[2] + mw[12];
    dst[1] = mw[1] * rest[0] + mw[5] * rest[1] + mw[9] * rest[2] + mw[13];
    dst[2] = mw[2] * rest[0] + mw[6] * rest[1] + mw[10] * rest[2] + mw[14];
#endif
}

float Dot3X87(const float a[3], const float b[3]) {
#if defined(_M_IX86)
    float result;
    __asm {
        mov eax, a
        mov edx, b
        fld dword ptr [eax]
        fmul dword ptr [edx]
        fld dword ptr [eax+4]
        fmul dword ptr [edx+4]
        faddp st(1), st
        fld dword ptr [eax+8]
        fmul dword ptr [edx+8]
        faddp st(1), st
        fstp dword ptr [result]
    }
    return result;
#else
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
#endif
}

// x64 0x7FF7CB4DB862..0x7FF7CB4DB88A: the CCD alignment test subtracts,
// squares and accumulates in SINGLE precision (subss/mulss/addss, the
// y term first, then x, then z) and compares against comiss 1e-7f
// (dword_7FF7CB552C48).
float SquaredDiff3X87(const float a[3], const float b[3]) {
#if defined(_M_IX86)
    float result;
    __asm {
        mov eax, a
        mov edx, b
        fld dword ptr [eax]
        fsub dword ptr [edx]
        fmul st, st
        fld dword ptr [eax+4]
        fsub dword ptr [edx+4]
        fmul st, st
        faddp st(1), st
        fld dword ptr [eax+8]
        fsub dword ptr [edx+8]
        fmul st, st
        faddp st(1), st
        fstp dword ptr [result]
    }
    return result;
#else
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return dy * dy + dx * dx + dz * dz;
#endif
}

void Cross3X87(float dst[3], const float a[3], const float b[3]) {
#if defined(_M_IX86)
    __asm {
        mov eax, a
        mov edx, b
        mov ecx, dst

        fld dword ptr [eax+4]
        fmul dword ptr [edx+8]
        fld dword ptr [eax+8]
        fmul dword ptr [edx+4]
        fsubp st(1), st
        fstp dword ptr [ecx]

        fld dword ptr [eax+8]
        fmul dword ptr [edx]
        fld dword ptr [eax]
        fmul dword ptr [edx+8]
        fsubp st(1), st
        fstp dword ptr [ecx+4]

        fld dword ptr [eax]
        fmul dword ptr [edx+4]
        fld dword ptr [eax+4]
        fmul dword ptr [edx]
        fsubp st(1), st
        fstp dword ptr [ecx+8]
    }
#else
    dst[0] = a[1] * b[2] - a[2] * b[1];
    dst[1] = a[2] * b[0] - a[0] * b[2];
    dst[2] = a[0] * b[1] - a[1] * b[0];
#endif
}

// axis = transpose(bone+180 rotation) * axis, normalized (LABEL_135)
void ProjectAxisOnLocal(float axis[3], const mdl::BoneRecord& bone) {
    const float* r = bone.matWorld;
    float out[3];
    out[0] = Dot3X87(r, axis);
    out[1] = Dot3X87(r + 4, axis);
    out[2] = Dot3X87(r + 8, axis);
    Vec3Norm(axis, out);
}

// reflect-clamp one euler angle into [lo, hi]; `twist` disables the
// reflection and pins to the bound (0x495A91 pattern).
void ClampEuler(float& a, float lo, float hi, bool twist) {
    if (lo > a) {
        const float refl = 2.0f * lo - a;
        a = (hi < refl || twist) ? lo : refl;
    }
    if (hi < a) {
        const float refl = 2.0f * hi - a;
        a = (lo > refl || twist) ? hi : refl;
    }
}

// ---- BoneFrameTransform phase passes --------------------------------------

// Channel router (BoneFrameTransform a2): 0 -> bones WITHOUT flag 0x1000
// (before physics), 1 -> bones WITH flag 0x1000 (after physics).
bool ChannelPass(const mdl::BoneRecord& bone, unsigned char afterPhysics) {
    const bool has = (bone.flags & mdl::kBoneFlagAfterPhysics) != 0;
    return afterPhysics ? has : !has;
}

// +244 copy gate (LABEL_83): ((physicsMode==1 || (physicsMode>=2 &&
// bone+493==0)) & bone+492) == 0.
bool CopyGate(const mdl::BoneRecord& bone, int physicsMode) {
    return ((physicsMode == 1 || (physicsMode >= 2 && bone.physicsDisabled == 0)) & bone.hasRigidBody) == 0;
}

// phase A (0x493A85): local matrix for physics-swept bones (+596 flag
// set): inherit 0x100 from bone+488's +116, own quat +376, 0x200
// translation inherit -> bone+116 = T(-rest308) * R * T(off364) *
// T(rest308).
void BoneTransform_PhysicsSweptLocals(D3& d3, mdl::BoneRecord* boneRecords,
                                      int boneCount,
                                      unsigned char afterPhysics,
                                      int layer) {
    for (int i = 0; i < boneCount; ++i) {
        mdl::BoneRecord& bone = boneRecords[i];
        if (!ChannelPass(bone, afterPhysics))
            continue;
        if (bone.layer != layer || bone.hasFlag == 0)
            continue;
        float q[4] = {bone.physicsQuat[0], bone.physicsQuat[1], bone.physicsQuat[2], bone.physicsQuat[3]};
        if (bone.flags & mdl::kBoneFlagRotInherit)
            InheritRotQuat(q, boneRecords, bone.tailIdx, bone.inheritRatio);
        MatRotQuat(reinterpret_cast<D3DXMATRIXF*>(bone.matLocal), q);
        if (bone.flags & mdl::kBoneFlagTransInherit) {
            mdl::BoneRecord& src = boneRecords[bone.tailIdx];
            const float r = bone.inheritRatio;
            D3DXMATRIXF tmp, cur;
            d3.translation(&tmp, src.physicsOffset[0] * r, src.physicsOffset[1] * r,
                           src.physicsOffset[2] * r);
            std::memcpy(&cur, bone.matLocal, sizeof(cur));
            d3.mul(reinterpret_cast<D3DXMATRIXF*>(bone.matLocal), &cur, &tmp);
        }
        D3DXMATRIXF tmp, cur;
        d3.translation(&tmp, -bone.position[0], -bone.position[1],
                       -bone.position[2]);
        std::memcpy(&cur, bone.matLocal, sizeof(cur));
        d3.mul(&cur, &tmp, &cur);
        d3.translation(&tmp, bone.physicsOffset[0], bone.physicsOffset[1], bone.physicsOffset[2]);
        d3.mul(reinterpret_cast<D3DXMATRIXF*>(bone.matLocal), &cur, &tmp);
        std::memcpy(&cur, bone.matLocal, sizeof(cur));
        d3.translation(&tmp, bone.position[0], bone.position[1],
                       bone.position[2]);
        d3.mul(reinterpret_cast<D3DXMATRIXF*>(bone.matLocal), &cur, &tmp);
    }
}

// phase B (0x493E71): local matrix for the rest: type 4 resets scale
// +348..360, type 9 axis-angle from tail(+460) quat x rate(int)(+488)
// /100, type 5 quat = parent(+488)quat * own, 0x100 inherit; after the
// sandwich, 0x200 bones add the inherited translation to +364.
void BoneTransform_StandardLocals(D3& d3, mdl::BoneRecord* boneRecords,
                                  int boneCount,
                                  unsigned char afterPhysics,
                                  int layer) {
    for (int i = 0; i < boneCount; ++i) {
        mdl::BoneRecord& bone = boneRecords[i];
        if (!ChannelPass(bone, afterPhysics))
            continue;
        if (bone.layer != layer || bone.hasFlag != 0)
            continue;
        if (bone.type == mdl::BoneType::UnderIk) {     // 0x493EDC
            bone.rotQuat2[0] = bone.rotQuat2[1] = bone.rotQuat2[2] = 0.0f;
            bone.rotQuat2[3] = 1.0f;
        }
        if (bone.type == mdl::BoneType::CoRotate) {    // 0x493F1F
            // x64 0x7FF7CB4DD069..0x7FF7CB4DD18A: acosf/sqrtf/sinf/cosf,
            // ((float)rate * angle) / 100.0f (divss), single precision.
            const mdl::BoneRecord& tail = boneRecords[bone.tailBone];
            float sq[4] = {tail.physicsQuat[0], tail.physicsQuat[1], tail.physicsQuat[2],
                           tail.physicsQuat[3]};
            if (sq[3] <= 1.0f) {
                if (sq[3] < -1.0f) sq[3] = -1.0f;
            } else {
                sq[3] = 1.0f;
            }
            float angle = std::acos(sq[3]);
            if (sq[3] < 0.0f)
                angle -= kPiF;
            const float len = std::sqrt(sq[0] * sq[0] + sq[1] * sq[1] +
                                        sq[2] * sq[2]);
            if (angle == 0.0f || len < kEps) {
                bone.physicsQuat[0] = bone.physicsQuat[1] = bone.physicsQuat[2] = 0.0f;
                bone.physicsQuat[3] = 1.0f;
            } else {
                const float scaled =
                    (static_cast<float>(bone.tailIdx) * angle) / 100.0f;
                const float s = std::sin(scaled) / len;
                bone.physicsQuat[0] = s * sq[0];
                bone.physicsQuat[1] = s * sq[1];
                bone.physicsQuat[2] = s * sq[2];
                bone.physicsQuat[3] = std::cos(scaled);
            }
        }
        float q[4] = {bone.physicsQuat[0], bone.physicsQuat[1], bone.physicsQuat[2], bone.physicsQuat[3]};
        if (bone.type == mdl::BoneType::RotateGrant) { // 0x49412F
            const mdl::BoneRecord& src = boneRecords[bone.tailIdx];
            float sq[4] = {src.physicsQuat[0], src.physicsQuat[1], src.physicsQuat[2], src.physicsQuat[3]};
            float out[4];
            QuatMul(out, sq, q);
            q[0] = out[0];
            q[1] = out[1];
            q[2] = out[2];
            q[3] = out[3];
        }
        if (bone.flags & mdl::kBoneFlagRotInherit)
            InheritRotQuat(q, boneRecords, bone.tailIdx, bone.inheritRatio);
        MatRotQuat(reinterpret_cast<D3DXMATRIXF*>(bone.matLocal), q);
        float inhX = 0.0f, inhY = 0.0f, inhZ = 0.0f;
        if (bone.flags & mdl::kBoneFlagTransInherit) {
            const mdl::BoneRecord& src = boneRecords[bone.tailIdx];
            const float r = bone.inheritRatio;
            inhX = src.physicsOffset[0] * r;
            inhY = src.physicsOffset[1] * r;
            inhZ = src.physicsOffset[2] * r;
            D3DXMATRIXF tmp, cur;
            d3.translation(&tmp, inhX, inhY, inhZ);
            std::memcpy(&cur, bone.matLocal, sizeof(cur));
            d3.mul(reinterpret_cast<D3DXMATRIXF*>(bone.matLocal), &cur, &tmp);
        }
        D3DXMATRIXF tmp, cur;
        d3.translation(&tmp, -bone.position[0], -bone.position[1],
                       -bone.position[2]);
        std::memcpy(&cur, bone.matLocal, sizeof(cur));
        d3.mul(&cur, &tmp, &cur);
        d3.translation(&tmp, bone.physicsOffset[0], bone.physicsOffset[1], bone.physicsOffset[2]);
        d3.mul(reinterpret_cast<D3DXMATRIXF*>(bone.matLocal), &cur, &tmp);
        std::memcpy(&cur, bone.matLocal, sizeof(cur));
        d3.translation(&tmp, bone.position[0], bone.position[1],
                       bone.position[2]);
        d3.mul(reinterpret_cast<D3DXMATRIXF*>(bone.matLocal), &cur, &tmp);
        if (bone.flags & mdl::kBoneFlagTransInherit) { // 0x4944F0
            bone.physicsOffset[0] += inhX;
            bone.physicsOffset[1] += inhY;
            bone.physicsOffset[2] += inhZ;
        }
    }
}

// phase C (0x49455F): world pass: external parent (bone+600 ->
// model+314596 table {slot@+12, bone@+16}) chains the OTHER model's
// bone+244; else +52 = +116 * parent(+48)+52; +180 = parent world
// rotation-only mirror; root keeps +52 = +116 and +180 = identity.
// +244 copy gate per CopyGate.
void BoneTransform_WorldPass(D3& d3, mdl::BoneRecord* boneRecords,
                             int boneCount, mdl::BoneOrderEntry* extTable,
                             unsigned char* const* modelSlots,
                             unsigned char afterPhysics, int layer,
                             int physicsMode) {
    for (int i = 0; i < boneCount; ++i) {
        mdl::BoneRecord& bone = boneRecords[i];
        if (!ChannelPass(bone, afterPhysics))
            continue;
        if (bone.layer != layer)
            continue;
        const std::int32_t parent = bone.parent;
        const std::int32_t extIdx = bone.slotIndex;
        bool handled = false;
        bool viaGlobalExt = false;
        if (extIdx >= 0) {
            const std::int32_t slot = extTable[extIdx].linkedModel;
            if (slot >= 0) {                           // 0x4945FE
                unsigned char* model2 = modelSlots[slot];
                mdl::BoneRecord* bones2 = mdl::Bones(model2);
                const std::int32_t extBone = extTable[extIdx].linkedBone;
                const mdl::BoneRecord& src = bones2[extBone];
                D3DXMATRIXF ext, tmp, local;
                std::memcpy(&ext, src.matExtra, sizeof(ext));
                d3.translation(&tmp, src.position[0], src.position[1],
                               src.position[2]);
                d3.mul(&ext, &tmp, &ext);
                d3.translation(&tmp, -bone.position[0], -bone.position[1],
                               -bone.position[2]);
                d3.mul(&ext, &tmp, &ext);
                std::memcpy(&local, bone.matLocal, sizeof(local));
                d3.mul(reinterpret_cast<D3DXMATRIXF*>(bone.matInit), &local,
                       &ext);
                ext.m[3][0] = ext.m[3][1] = ext.m[3][2] = 0.0f;
                ext.m[3][3] = 1.0f;
                std::memcpy(bone.matWorld, &ext, sizeof(ext));
                handled = true;
            } else if (slot == -1) {
                if (parent >= 0) {
                    WorldFromParent(bone, boneRecords[parent], &d3);
                    handled = true;
                } else {
                    viaGlobalExt = true;
                }
            }
        } else if (parent >= 0) {
            WorldFromParent(bone, boneRecords[parent], &d3);
            handled = true;
        } else {
            viaGlobalExt = true;
        }
        if (viaGlobalExt) {
            // LABEL_80 (0x49488F): global external parent from the table.
            // x64: both the extIdx<0,parent<0 route (0x7FF7CB4DB0CD) and the
            // slot==-1,parent<0 route (0x7FF7CB4DB015) converge on the same
            // extTable[0].linkedModel test at 0x7FF7CB4DB0D8.
            if (extTable[0].linkedModel >= 0) {
                unsigned char* model2 = modelSlots[extTable[0].linkedModel];
                mdl::BoneRecord* bones2 = mdl::Bones(model2);
                const mdl::BoneRecord& src =
                    bones2[extTable[0].linkedBone];
                D3DXMATRIXF ext, tmp, local;
                std::memcpy(&ext, src.matExtra, sizeof(ext));
                d3.translation(&tmp, src.position[0], src.position[1],
                               src.position[2]);
                d3.mul(&ext, &tmp, &ext);
                std::memcpy(&local, bone.matLocal, sizeof(local));
                d3.mul(reinterpret_cast<D3DXMATRIXF*>(bone.matInit), &local,
                       &ext);
                ext.m[3][0] = ext.m[3][1] = ext.m[3][2] = 0.0f;
                ext.m[3][3] = 1.0f;
                std::memcpy(bone.matWorld, &ext, sizeof(ext));
                handled = true;
            }
        }
        if (!handled) {                                // 0x494999 root
            std::memcpy(bone.matInit, bone.matLocal, sizeof(bone.matInit));
            D3DXMATRIXF id;
            std::memset(&id, 0, sizeof(id));
            id.m[0][0] = id.m[1][1] = id.m[2][2] = id.m[3][3] = 1.0f;
            std::memcpy(bone.matWorld, &id, sizeof(id));
        }
        if (CopyGate(bone, physicsMode))                       // LABEL_83
            std::memcpy(bone.matExtra, bone.matInit, sizeof(bone.matExtra));
    }
}

// phase D (0x494A56): CCD IK per 24-byte chain at model+9920 {+0
// effector, +4 root, +8 links, +12 child*, +16 loops, +18 enabled,
// +20 angle}; PMX limit planes, PMD axis projection and the knee hinge
// flip per the file header.  The floating-point sequence is ported
// verbatim (x87 fidelity notes included) - do not reorder.
void BoneTransform_CcdIk(D3& d3, unsigned char* m,
                         mdl::BoneRecord* boneRecords, mdl::IkChain* iks,
                         int ikCount, unsigned char afterPhysics,
                         int layer, int physicsMode, bool pmx2) {
    for (int ci = 0; ci < ikCount; ++ci) {
        mdl::IkChain& ik = iks[ci];
        mdl::BoneRecord& target = boneRecords[ik.boneIndex];
        if (!ChannelPass(target, afterPhysics))
            continue;
        if (target.layer != layer || ik.enabled == 0)
            continue;
        mdl::BoneRecord& ikRoot = boneRecords[ik.targetBone];
        const mdl::BoneRecord& rootParent = boneRecords[ikRoot.parent];
        if (((physicsMode == 1 || (physicsMode >= 2 && rootParent.physicsDisabled == 0)) &
             rootParent.hasRigidBody) != 0)
            continue;
        float eff[3];
        WorldPos(eff, target);                         // 0x494B7F
        // rebuild the chain root local matrix (0x494BE5)
        {
            mdl::BoneRecord& root = ikRoot;
            float q[4] = {root.physicsQuat[0], root.physicsQuat[1], root.physicsQuat[2],
                          root.physicsQuat[3]};
            MatRotQuat(reinterpret_cast<D3DXMATRIXF*>(root.matLocal), q);
            D3DXMATRIXF tmp, cur;
            d3.translation(&tmp, -root.position[0], -root.position[1],
                           -root.position[2]);
            std::memcpy(&cur, root.matLocal, sizeof(cur));
            d3.mul(&cur, &tmp, &cur);
            d3.translation(&tmp, root.physicsOffset[0], root.physicsOffset[1], root.physicsOffset[2]);
            d3.mul(reinterpret_cast<D3DXMATRIXF*>(root.matLocal), &cur, &tmp);
            std::memcpy(&cur, root.matLocal, sizeof(cur));
            d3.translation(&tmp, root.position[0], root.position[1],
                           root.position[2]);
            d3.mul(reinterpret_cast<D3DXMATRIXF*>(root.matLocal), &cur, &tmp);
        }
        const std::uint16_t* child = ik.links;
        const int loops = ik.iterations;
        const float chainAngle = ik.maxAngle;
        if (ik.linkCount == 0)
            continue;
        // 0x494CEE..0x494D08: the original halves the IK iteration count
        // at +16, not the child-link count at +8.  This threshold selects
        // the relaxed PMX-limit pass before the constrained iterations.
        const int twist = loops >> 1;                  // v341
        int iter = 0;
        while (iter < loops) {
            if (ik.linkCount == 0)
                break;                                 // LABEL_264 advance
            bool exitAll = false;
            for (int li = 0; li < ik.linkCount; ++li) {
                mdl::BoneRecord& root = ikRoot;
                float rootPos[3];
                WorldPos(rootPos, root);               // v391..393
                mdl::BoneRecord& link = boneRecords[child[li]];
                float linkPos[3];
                WorldPos(linkPos, link);               // v394..396
                float d1[3] = {linkPos[0] - rootPos[0],
                               linkPos[1] - rootPos[1],
                               linkPos[2] - rootPos[2]};
                float d2[3] = {linkPos[0] - eff[0], linkPos[1] - eff[1],
                               linkPos[2] - eff[2]};
                Vec3Norm(d1, d1);
                Vec3Norm(d2, d2);
                const float diff2 = SquaredDiff3X87(d1, d2);
#ifdef MIKUDANCESTUDIO_DIAG
                WriteIkPrecheck(m, ci, iter, li, d1, d2, diff2, target,
                                root, link);
#endif
                if (diff2 < 1e-7f) {                   // 0x494F37 / x64 comiss
                    iter = loops;                      // skip all iterations
                    exitAll = true;
                    break;
                }
                float axis[3];
                Cross3X87(axis, d1, d2);
                float align = 1.0f;                    // v331
                const float* r180 = link.matWorld;
                if (!pmx2) {                           // 0x4955F7 PMD path
                    if (IsKnee(link)) {
                        const float dot1 = r180[0] * axis[0] +
                                           r180[1] * axis[1] +
                                           r180[2] * axis[2];
                        if (dot1 >= 0.0f) {
                            axis[0] = 1.0f;
                            axis[1] = 0.0f;
                            axis[2] = 0.0f;
                        } else {
                            axis[0] = -1.0f;
                            axis[1] = 0.0f;
                            axis[2] = 0.0f;
                        }
                    } else {
                        ProjectAxisOnLocal(axis, link);
                    }
                } else if (link.twistEnable != 0 && iter < twist) {
                    const float limXlo = link.ikLimitMin[0];
                    const float limXhi = link.ikLimitMax[0];
                    const float limYlo = link.ikLimitMin[1];
                    const float limYhi = link.ikLimitMax[1];
                    const float limZlo = link.ikLimitMin[2];
                    const float limZhi = link.ikLimitMax[2];
                    if (limYlo != 0.0f || limYhi != 0.0f || limZlo != 0.0f ||
                        limZhi != 0.0f) {
                        if (limXlo == 0.0f && limXhi == 0.0f &&
                            limZlo == 0.0f && limZhi == 0.0f) {
                            // X-plane: axis = +-Y by row-2 dot (0x4950E0)
                            const float dot2 = r180[4] * axis[0] +
                                               r180[5] * axis[1] +
                                               r180[6] * axis[2];
                            axis[0] = 0.0f;
                            axis[1] = dot2 < 0.0f ? -1.0f : 1.0f;
                            axis[2] = 0.0f;
                        } else if (limXlo == 0.0f && limXhi == 0.0f &&
                                   limYlo == 0.0f && limYhi == 0.0f) {
                            // Y-plane: axis = +-Z by row-3 dot (0x495187)
                            const float dot3 = r180[8] * axis[0] +
                                               r180[9] * axis[1] +
                                               r180[10] * axis[2];
                            axis[0] = 0.0f;
                            axis[1] = 0.0f;
                            axis[2] = dot3 < 0.0f ? -1.0f : 1.0f;
                        } else if (link.flags & mdl::kBoneFlagFixedAxis) {
                            // fixed axis +508..516 (0x4951D7)
                            ProjectAxisOnLocal(axis, link);
                            const float fx = link.axis[0];
                            const float fy = link.axis[1];
                            const float fz = link.axis[2];
                            align = fy * axis[1] + fx * axis[0] + fz * axis[2];
                            axis[0] = fx;
                            axis[1] = fy;
                            axis[2] = fz;
                        } else {
                            ProjectAxisOnLocal(axis, link);
                        }
                    } else {
                        // hinge: axis = +-X by row-1 dot (0x495038)
                        const float dot1 = r180[0] * axis[0] +
                                           r180[1] * axis[1] +
                                           r180[2] * axis[2];
                        if (dot1 < 0.0f) {
                            axis[0] = -1.0f;
                            axis[1] = 0.0f;
                            axis[2] = 0.0f;
                        } else {
                            axis[0] = 1.0f;
                            axis[1] = 0.0f;
                            axis[2] = 0.0f;
                        }
                    }
                } else {
                    if ((link.flags & mdl::kBoneFlagFixedAxis) == 0) {
                        ProjectAxisOnLocal(axis, link);   // 0x495544 block
                    } else {                             // 0x4953DF
                        ProjectAxisOnLocal(axis, link);
                        const float fx = link.axis[0];
                        const float fy = link.axis[1];
                        const float fz = link.axis[2];
                        align = fy * axis[1] + fx * axis[0] + fz * axis[2];
                        axis[0] = fx;
                        axis[1] = fy;
                        axis[2] = fz;
                    }
                }
                // LABEL_136: half-angle with alignment and weight clamp.
                // x64 0x7FF7CB4DC0A8..0x7FF7CB4DC194: single precision -
                // float dot (3x mulss + 2 addss), +-1.0f clamp (comiss
                // against 1.0f / -1.0f, NOT the slerp's 0.999999), acosf,
                // half = acos * 0.5f * align (mulss chain), the limit
                // ((float)(li+1) * chainAngle) * +-2.0f, and rotq built
                // from sinf(half)/cosf(half) with per-component mulss.
                float dot = Dot3X87(d2, d1);
                if (dot > 1.0f)
                    dot = 1.0f;
                else if (dot < -1.0f)
                    dot = -1.0f;
                float half = (std::acos(dot) * 0.5f) * align;
                const float wlim =
                    (static_cast<float>(li + 1) * chainAngle) * 2.0f;
                if (half >= 0.0f) {
                    if (wlim < half)
                        half = wlim;
                } else {
                    if (-wlim > half)
                        half = -wlim;
                }
                const float sinHalf = std::sin(half);
                float rotq[4] = {
                    sinHalf * axis[0],
                    sinHalf * axis[1],
                    sinHalf * axis[2],
                    std::cos(half)};
                float acc[4] = {link.rotQuat2[0], link.rotQuat2[1],
                                link.rotQuat2[2], link.rotQuat2[3]};
#ifdef MIKUDANCESTUDIO_DIAG
                WriteIkProbe(m, ci, iter, li, loops,
                             ik.boneIndex, ik.targetBone, child[li], d1, d2,
                             axis, dot, static_cast<float>(half), acc, rotq);
#endif
                float outq[4];
                QuatMul(outq, acc, rotq);              // acc = rot * acc
                link.rotQuat2[0] = outq[0];
                link.rotQuat2[1] = outq[1];
                link.rotQuat2[2] = outq[2];
                link.rotQuat2[3] = outq[3];
                if (iter == 0) {                       // 0x49489A
                    float q0[4] = {link.physicsQuat[0], link.physicsQuat[1], link.physicsQuat[2],
                                   link.physicsQuat[3]};
                    float linkQuat[4] = {link.rotQuat2[0], link.rotQuat2[1],
                                   link.rotQuat2[2], link.rotQuat2[3]};
                    QuatMul(outq, q0, linkQuat);             // acc = acc * q376
                    link.rotQuat2[0] = outq[0];
                    link.rotQuat2[1] = outq[1];
                    link.rotQuat2[2] = outq[2];
                    link.rotQuat2[3] = outq[3];
                }
                float lq[4] = {link.rotQuat2[0], link.rotQuat2[1],
                               link.rotQuat2[2], link.rotQuat2[3]};
                D3DXMATRIXF rotM;
                MatRotQuat(&rotM, lq);
                if (pmx2 && link.twistEnable != 0) {
                    // euler decomposition + limit clamp + rebuild
                    // (0x495921..0x496429; x64 0x7FF7CB4DC2D3..DC845: the
                    // off-axis angles come from atan2f(elem/c, elem/c))
                    float eX = 0.0f, eY = 0.0f, eZ = 0.0f;
                    D3DXMATRIXF final;
                    if (link.ikLimitMin[0] > -kHalfPi &&
                        link.ikLimitMax[0] < kHalfPi) {
                        float a = std::asin(-rotM.m[2][1]);
                        float c = std::cos(a);
                        if (std::fabs(a) > kKneeClamp) {
                            a = a > 0.0f ? kKneeClamp : -kKneeClamp;
                            c = std::cos(a);
                        }
                        eX = a;
                        eY = std::atan2(rotM.m[2][0] / c, rotM.m[2][2] / c);
                        eZ = std::atan2(rotM.m[0][1] / c, rotM.m[1][1] / c);
                        ClampEuler(eX, link.ikLimitMin[0], link.ikLimitMax[0],
                                   iter >= twist);
                        ClampEuler(eY, link.ikLimitMin[1], link.ikLimitMax[1],
                                   iter >= twist);
                        ClampEuler(eZ, link.ikLimitMin[2], link.ikLimitMax[2],
                                   iter >= twist);
                        D3DXMATRIXF mx, my, mz, t1;
                        d3.rotX(&mx, eX);
                        d3.rotY(&my, eY);
                        d3.rotZ(&mz, eZ);
                        d3.mul(&t1, &mz, &mx);         // RotZ*RotX
                        d3.mul(&final, &t1, &my);      // RotZ*RotX*RotY
                    } else if (link.ikLimitMin[1] > -kHalfPi &&
                               link.ikLimitMax[1] < kHalfPi) {
                        float a = std::asin(-rotM.m[0][2]);
                        float c = std::cos(a);
                        if (std::fabs(a) > kKneeClamp) {
                            a = a > 0.0f ? kKneeClamp : -kKneeClamp;
                            c = std::cos(a);
                        }
                        eY = a;
                        eX = std::atan2(rotM.m[1][2] / c, rotM.m[2][2] / c);
                        eZ = std::atan2(rotM.m[0][1] / c, rotM.m[0][0] / c);
                        ClampEuler(eX, link.ikLimitMin[0], link.ikLimitMax[0],
                                   iter >= twist);
                        ClampEuler(eY, link.ikLimitMin[1], link.ikLimitMax[1],
                                   iter >= twist);
                        ClampEuler(eZ, link.ikLimitMin[2], link.ikLimitMax[2],
                                   iter >= twist);
                        D3DXMATRIXF mx, my, mz, t1;
                        d3.rotX(&mx, eX);
                        d3.rotY(&my, eY);
                        d3.rotZ(&mz, eZ);
                        d3.mul(&t1, &mx, &my);         // RotX*RotY
                        d3.mul(&final, &t1, &mz);      // RotX*RotY*RotZ
                    } else {
                        // Z-primacy: eZ from m10, eX from m12, eY from m20
                        float a = std::asin(-rotM.m[1][0]);
                        float c = std::cos(a);
                        if (std::fabs(a) > kKneeClamp) {
                            a = a > 0.0f ? kKneeClamp : -kKneeClamp;
                            c = std::cos(a);
                        }
                        eZ = a;
                        eX = std::atan2(rotM.m[1][2] / c, rotM.m[1][1] / c);
                        eY = std::atan2(rotM.m[2][0] / c, rotM.m[0][0] / c);
                        ClampEuler(eX, link.ikLimitMin[0], link.ikLimitMax[0],
                                   iter >= twist);
                        ClampEuler(eY, link.ikLimitMin[1], link.ikLimitMax[1],
                                   iter >= twist);
                        ClampEuler(eZ, link.ikLimitMin[2], link.ikLimitMax[2],
                                   iter >= twist);
                        D3DXMATRIXF mx, my, mz, t1;
                        d3.rotX(&mx, eX);
                        d3.rotY(&my, eY);
                        d3.rotZ(&mz, eZ);
                        d3.mul(&t1, &my, &mz);         // RotY*RotZ
                        d3.mul(&final, &t1, &mx);      // RotY*RotZ*RotX
                    }
                    rotM = final;
                    float nq[4];
                    d3.quatFromMatrix(nq, &rotM);
                    link.rotQuat2[0] = nq[0];
                    link.rotQuat2[1] = nq[1];
                    link.rotQuat2[2] = nq[2];
                    link.rotQuat2[3] = nq[3];
                } else if (!pmx2 && IsKnee(link) &&
                           std::atan2(rotM.m[2][1], rotM.m[2][2]) < 0.0f) {
                    // PMD knee hinge flip (0x4964DD; x64 0x7FF7CB4DCAF2
                    // gates on the sign of atan2f(m[2][1], m[2][2]))
                    rotM.m[1][2] = -rotM.m[1][2];
                    rotM.m[2][1] = -rotM.m[2][1];
                    link.rotQuat2[0] = -link.rotQuat2[0];
                }
                // local matrix + sandwich (0x496510..0x496601)
                std::memcpy(link.matLocal, &rotM, sizeof(rotM));
                {
                    D3DXMATRIXF tmp, cur;
                    d3.translation(&tmp, -link.position[0], -link.position[1],
                                   -link.position[2]);
                    std::memcpy(&cur, link.matLocal, sizeof(cur));
                    d3.mul(&cur, &tmp, &cur);
                    d3.translation(&tmp, link.physicsOffset[0], link.physicsOffset[1],
                                   link.physicsOffset[2]);
                    d3.mul(reinterpret_cast<D3DXMATRIXF*>(link.matLocal),
                           &cur, &tmp);
                    std::memcpy(&cur, link.matLocal, sizeof(cur));
                    d3.translation(&tmp, link.position[0], link.position[1],
                                   link.position[2]);
                    d3.mul(reinterpret_cast<D3DXMATRIXF*>(link.matLocal),
                           &cur, &tmp);
                }
                // upward world recompute li..0 (0x496620); the +52 gate
                // uses the a5==3 form, the +244 copy the LABEL_83 form
                for (int lj = li; lj >= 0; --lj) {
                    mdl::BoneRecord& linkedBone = boneRecords[child[lj]];
                    const bool j3 = (physicsMode == 3);
                    if ((linkedBone.hasRigidBody & (linkedBone.physicsDisabled == 0 ? 1 : 0) &
                         (j3 ? 1 : 0)) == 0) {
                        WorldFromParent(linkedBone,
                                        boneRecords[linkedBone.parent], &d3);
                        if (((physicsMode == 1 || (physicsMode >= 2 && linkedBone.physicsDisabled == 0)) &
                             linkedBone.hasRigidBody) == 0)
                            std::memcpy(linkedBone.matExtra,
                                        linkedBone.matInit,
                                        sizeof(linkedBone.matExtra));
                    }
                }
                // chain root world recompute (0x496748)
                {
                    mdl::BoneRecord& root = ikRoot;
                    WorldFromParent(root, boneRecords[root.parent], &d3);
                    if (((physicsMode == 1 || (physicsMode >= 2 && root.physicsDisabled == 0)) &
                         root.hasRigidBody) == 0)
                        std::memcpy(root.matExtra, root.matInit,
                                    sizeof(root.matExtra));
                }
            }
            if (exitAll)
                break;
            ++iter;
        }
    }
}

// phase E (0x496881): post-IK pass for +596 bones (type != 4): type 9/5
// take the source quat from tail(+460)/parent(+488) using +348 when
// that source bone is type 4 (IK-touched), rebuild +116 and world +52.
void BoneTransform_PostIkPass(D3& d3, mdl::BoneRecord* boneRecords,
                              int boneCount, unsigned char afterPhysics,
                              int layer, int physicsMode) {
    for (int i = 0; i < boneCount; ++i) {
        mdl::BoneRecord& bone = boneRecords[i];
        if (!ChannelPass(bone, afterPhysics))
            continue;
        if (bone.layer != layer || bone.type == mdl::BoneType::UnderIk ||
            bone.hasFlag == 0)
            continue;
        if (bone.type == mdl::BoneType::CoRotate) {    // 0x4968F3
            // x64 twin of the phase-B block (acosf/sqrtf/sinf/cosf).
            const mdl::BoneRecord& tail = boneRecords[bone.tailBone];
            const float* sourceQuat =
                tail.type == mdl::BoneType::UnderIk ? tail.rotQuat2
                                                    : tail.physicsQuat;
            float sq[4] = {sourceQuat[0], sourceQuat[1], sourceQuat[2],
                           sourceQuat[3]};
            if (sq[3] <= 1.0f) {
                if (sq[3] < -1.0f) sq[3] = -1.0f;
            } else {
                sq[3] = 1.0f;
            }
            float angle = std::acos(sq[3]);
            if (sq[3] < 0.0f)
                angle -= kPiF;
            const float len = std::sqrt(sq[0] * sq[0] + sq[1] * sq[1] +
                                        sq[2] * sq[2]);
            if (angle == 0.0f || len < kEps) {
                bone.physicsQuat[0] = bone.physicsQuat[1] = bone.physicsQuat[2] = 0.0f;
                bone.physicsQuat[3] = 1.0f;
            } else {
                const float scaled =
                    (static_cast<float>(bone.tailIdx) * angle) / 100.0f;
                const float s = std::sin(scaled) / len;
                bone.physicsQuat[0] = s * sq[0];
                bone.physicsQuat[1] = s * sq[1];
                bone.physicsQuat[2] = s * sq[2];
                bone.physicsQuat[3] = std::cos(scaled);
            }
        }
        float q[4] = {bone.physicsQuat[0], bone.physicsQuat[1], bone.physicsQuat[2], bone.physicsQuat[3]};
        if (bone.type == mdl::BoneType::RotateGrant) { // 0x496B53
            const mdl::BoneRecord& src = boneRecords[bone.tailIdx];
            const float* sourceQuat =
                src.type == mdl::BoneType::UnderIk ? src.rotQuat2 : src.physicsQuat;
            float sq[4] = {sourceQuat[0], sourceQuat[1], sourceQuat[2],
                           sourceQuat[3]};
            float out[4];
            QuatMul(out, sq, q);
            q[0] = out[0];
            q[1] = out[1];
            q[2] = out[2];
            q[3] = out[3];
        }
        if (bone.flags & mdl::kBoneFlagRotInherit)
            InheritRotQuat(q, boneRecords, bone.tailIdx, bone.inheritRatio);
        MatRotQuat(reinterpret_cast<D3DXMATRIXF*>(bone.matLocal), q);
        float inhX = 0.0f, inhY = 0.0f, inhZ = 0.0f;
        if (bone.flags & mdl::kBoneFlagTransInherit) {
            const mdl::BoneRecord& src = boneRecords[bone.tailIdx];
            const float r = bone.inheritRatio;
            inhX = src.physicsOffset[0] * r;
            inhY = src.physicsOffset[1] * r;
            inhZ = src.physicsOffset[2] * r;
            D3DXMATRIXF tmp, cur;
            d3.translation(&tmp, inhX, inhY, inhZ);
            std::memcpy(&cur, bone.matLocal, sizeof(cur));
            d3.mul(reinterpret_cast<D3DXMATRIXF*>(bone.matLocal), &cur, &tmp);
        }
        D3DXMATRIXF tmp, cur;
        d3.translation(&tmp, -bone.position[0], -bone.position[1],
                       -bone.position[2]);
        std::memcpy(&cur, bone.matLocal, sizeof(cur));
        d3.mul(&cur, &tmp, &cur);
        d3.translation(&tmp, bone.physicsOffset[0], bone.physicsOffset[1], bone.physicsOffset[2]);
        d3.mul(reinterpret_cast<D3DXMATRIXF*>(bone.matLocal), &cur, &tmp);
        std::memcpy(&cur, bone.matLocal, sizeof(cur));
        d3.translation(&tmp, bone.position[0], bone.position[1],
                       bone.position[2]);
        d3.mul(reinterpret_cast<D3DXMATRIXF*>(bone.matLocal), &cur, &tmp);
        if (bone.flags & mdl::kBoneFlagTransInherit) {
            bone.physicsOffset[0] += inhX;
            bone.physicsOffset[1] += inhY;
            bone.physicsOffset[2] += inhZ;
        }
        WorldFromParent(bone, boneRecords[bone.parent], &d3);
        if (CopyGate(bone, physicsMode))                       // 0x49706F
            std::memcpy(bone.matExtra, bone.matInit, sizeof(bone.matExtra));
    }
}

}  // namespace

void ArmBoneTransformIkProbe() {
#ifdef MIKUDANCESTUDIO_DIAG
    InterlockedExchange(&gIkProbeArmed, 1);
    InterlockedExchange(&gIkProbeWritten, 0);
#endif
}

void BoneFrameTransform(unsigned char* m, unsigned char afterPhysics,
                        int layer, unsigned char* const* modelSlots,
                        int physicsMode) {                             // 0x493A60
    D3 d3;
    mdl::ModelRecord& model = *mdl::Mdl(m);
    const int boneCount = static_cast<int>(model.boneCount);
    mdl::BoneRecord* boneRecords = model.boneTable;
    mdl::IkChain* iks = mdl::IkChains(m);
    const int ikCount = static_cast<int>(model.ikChainCount);
    const bool pmx2 = model.physicsMode == 2;
    mdl::BoneOrderEntry* extTable = mdl::BoneOrder(m);

    // five sequential passes at the original phase boundaries (header)
    BoneTransform_PhysicsSweptLocals(d3, boneRecords, boneCount,
                                     afterPhysics,
                                     layer);                         // A 0x493A85
    BoneTransform_StandardLocals(d3, boneRecords, boneCount,
                                 afterPhysics,
                                 layer);                             // B 0x493E71
    BoneTransform_WorldPass(d3, boneRecords, boneCount, extTable,
                            modelSlots, afterPhysics, layer,
                            physicsMode);                            // C 0x49455F
    BoneTransform_CcdIk(d3, m, boneRecords, iks, ikCount, afterPhysics,
                        layer, physicsMode,
                        pmx2);                                        // D 0x494A56
    BoneTransform_PostIkPass(d3, boneRecords, boneCount, afterPhysics,
                              layer,
                              physicsMode);                          // E 0x496881
}

}  // namespace mikudancestudio