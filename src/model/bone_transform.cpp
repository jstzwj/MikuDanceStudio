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
//     projection on bone+180 rows, and left/right knees get the hinge
//     flip (negate m12/m21 and quat.x when m21 < 0).  After each link the
//     chain's world matrices are recomputed bottom-up.
//   E 0x496881  post-IK pass for +596 bones (type != 4): type 9/5 take
//     the source quat from tail(+460)/parent(+488) using +348 when that
//     source bone is type 4 (IK-touched), rebuild +116 and world +52.
//
// Fresh PMD loads run with all pose quats zeroed; the D3DX rotation
// formulas degenerate zero quats to identity, so the model stays in bind
//
// Layout: the five passes live in the shared CPU kernel at the original
// phase boundaries; this adapter supplies native storage, math and probes.
// The kernel computes the shared locals and calls the passes in order (the two gate lambdas became
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
#if defined(_M_IX86)
#include "mikudancestudio/mmd_math.hpp"
#endif
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/cpu_bone_transform.hpp"
#include <utility>

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
        if (i != 0)
            std::fputc(',', stream);
        std::uint32_t bits = 0;
        std::memcpy(&bits, values + i, sizeof(bits));
        std::fprintf(stream, "\"%08X\"", bits);
    }
    std::fputc(']', stream);
}

void WriteIkProbe(const mdl::ModelRecord* model, int chainIndex, int iteration, int linkIndex,
                  int loopCount, int targetIndex, int rootIndex, int boneIndex, const float d1[3],
                  const float d2[3], const float axis[3], float dot, float half, const float acc[4],
                  const float rotq[4]) {
    if (InterlockedCompareExchange(&gIkProbeArmed, 0, 0) == 0 || chainIndex != 0)
        return;
    const LONG recordIndex = InterlockedIncrement(&gIkProbeWritten);
    if (recordIndex > 512)
        return;
    char directory[MAX_PATH]{};
    const DWORD length =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\ik_trace.jsonl", directory);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, recordIndex == 1 ? "wb" : "ab") != 0 || stream == nullptr)
        return;
    std::fprintf(stream,
                 "{\"bone_count\":%d,\"chain\":%d,\"iteration\":%d,\"loops\":%d,"
                 "\"link_position\": %d, \"target\": %d, \"root\": %d, "
                 "\"bone\":%d,\"d1_bits\":",
                 static_cast<int>(model->boneCount), chainIndex, iteration, loopCount, linkIndex,
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

void WriteIkPrecheck(const mdl::ModelRecord* model, int chainIndex, int iteration, int linkIndex,
                     const float d1[3], const float d2[3], float diff2,
                     const mdl::BoneRecord& target, const mdl::BoneRecord& root,
                     const mdl::BoneRecord& link) {
    if (InterlockedCompareExchange(&gIkProbeArmed, 0, 0) == 0 || chainIndex != 0 ||
        iteration != 1 || linkIndex != 0)
        return;
    char directory[MAX_PATH]{};
    const DWORD length =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
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
    const char* names[] = {"target_matrix_bits", "root_matrix_bits", "link_matrix_bits"};
    for (int record = 0; record < 3; ++record) {
        std::fprintf(stream, ",\n  \"%s\": [", names[record]);
        for (int i = 0; i < 16; ++i) {
            if (i != 0)
                std::fputc(',', stream);
            std::uint32_t bits = 0;
            std::memcpy(&bits, records[record]->matInit + i, sizeof(bits));
            std::fprintf(stream, "\"%08X\"", bits);
        }
        std::fputc(']', stream);
    }
    std::fputs("\n}\n", stream);
    std::fclose(stream);
}

#endif // MIKUDANCESTUDIO_DIAG

// The original's truncated "pi": 0x4048F5C3 (x64 dword_7FF7CB552B90,
// 3.1400001f - NOT float(pi), the mantissa-truncated constant both
// binaries carry).
struct NativeBoneMath {
    d3dx::Api* api = &d3dx::Get();

    void rotX(D3DXMATRIXF* o, float a) { api->rotX(o, a); }
    void rotY(D3DXMATRIXF* o, float a) { api->rotY(o, a); }
    void rotZ(D3DXMATRIXF* o, float a) { api->rotZ(o, a); }
    void mul(D3DXMATRIXF* o, const D3DXMATRIXF* a, const D3DXMATRIXF* b) { api->multiply(o, a, b); }
    void translation(D3DXMATRIXF* o, float x, float y, float z) {
        std::memset(o, 0, sizeof(*o));
        o->m[0][0] = o->m[1][1] = o->m[2][2] = 1.0f;
        o->m[3][0] = x;
        o->m[3][1] = y;
        o->m[3][2] = z;
        o->m[3][3] = 1.0f;
    }
    void quatFromMatrix(float out[4], const D3DXMATRIXF* m) { api->quatFromMatrix(out, m); }
    void matrixRotationQuaternion(D3DXMATRIXF* out, const float* q) {
        api->matrixRotationQuaternion(out, q);
    }
    void normalize(float* out, const float* in) { api->vec3Normalize(out, in); }
    void quatMultiply(float* out, const float* a, const float* b) { api->quatMultiply(out, a, b); }
#if defined(_M_IX86)
    static void transformPoint(float* out, const float* p, const float* m) {
        mmd_math::transformPoint(out, p, m);
    }
    static float dot3(const float* a, const float* b) { return mmd_math::dot3(a, b); }
    static float squaredDistance3(const float* a, const float* b) {
        return mmd_math::squaredDistance3(a, b);
    }
    static void cross3(float* out, const float* a, const float* b) { mmd_math::cross3(out, a, b); }
#endif
};
struct NativeBoneTraits {
    using Bone = mdl::BoneRecord;
    using Model = mdl::ModelRecord;
    using IkChain = mdl::IkChain;
    using Binding = mdl::BoneOrderEntry;
    using Matrix = d3dx::D3DXMATRIXF;
    using Math = NativeBoneMath;
    static int Index(const Model&, int index) { return index; }
    static const Bone& Parent(const Bone* bones, int index) { return bones[index]; }
#ifdef MIKUDANCESTUDIO_DIAG
    template <class... T> static void Probe(T&&... args) { WriteIkProbe(std::forward<T>(args)...); }
    template <class... T> static void Precheck(T&&... args) {
        WriteIkPrecheck(std::forward<T>(args)...);
    }
#endif
};
} // namespace

void ArmBoneTransformIkProbe() {
#ifdef MIKUDANCESTUDIO_DIAG
    InterlockedExchange(&gIkProbeArmed, 1);
    InterlockedExchange(&gIkProbeWritten, 0);
#endif
}

void BoneFrameTransform(mdl::ModelRecord* model, unsigned char afterPhysics, int layer,
                        mdl::ModelRecord* const* slots, int physicsMode) {
    cpu_bone::TransformKernel<NativeBoneTraits>::Run(model, afterPhysics, layer, slots,
                                                     physicsMode);
}

} // namespace mikudancestudio
