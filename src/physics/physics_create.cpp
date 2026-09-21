// ===========================================================================
// MikuDanceStudio: Bullet physics object creation (phase 8)
// ===========================================================================
// VA 0x004064F0  CreateRigidBody  - shape + motion state + rigid body
// VA 0x00406010  CreatePhysJoint  - 6DOF spring constraint
// VA 0x004A9220  SetPhysicsMode   - bone pose source selector + propagation
//
// Binary analysis notes (verified against IDA session mmd932):
//   * All three are __thiscall on the physics scene wrapper stored at
//     model+60: scene+64 = btDiscreteDynamicsWorld*, scene+56 = constraint
//     id counter (0x406010 increments it and returns the old value).
//   * 0x4064F0 tail: out[0] = *(body+548) = btRigidBody::m_debugBodyId
//     (stock 2.75 assigns it from the static uniqueId counter inside
//     setupRigidBody at 0x4F0CD0: this[137]=dword_5459AC++).  out[1] is
//     the body pointer.  ModelDispose's ReleasePhysRigid (0x4068D0)
//     matches candidates by the same +548 field - the layout probe
//     (btRigidBody 560 bytes, m_debugBodyId@+548) confirms our
//     bullet275 package reproduces this exactly when WIN32 is defined.
//   * 0x4064F0 writes body+232/-236 (friction/restitution) and the
//     kinematic flag (+212 |= 2) raw; ported as API calls with identical
//     field effects.  The linear limits in 0x406010 were raw writes into
//     m_linearLimits at +816; our package build places that struct at
//     +800 (btTypedConstraint member order differs, uid@+24 vs +96), so
//     the port uses setLinearLowerLimit/setLinearUpperLimit and
//     setUserConstraintId instead of raw offsets (docs/ARCHITECTURE.md §8).
//   * Original allocates shapes and the body via btAlignedAlloc(560,16)
//     + placement construction; the port uses C++ new/delete (over-aligned
//     new under C++17), which disposes through the same deleting dtors.
//   * D3DXQuaternionRotationMatrix / D3DXQuaternionMultiply are imports in
//     the original and remain normal PE imports in this build.
// =========================================================================//

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <cstring>

#include "btBulletDynamicsCommon.h"
#include "BulletDynamics/ConstraintSolver/btGeneric6DofSpringConstraint.h"

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

void QuatFromMatrix(float out[4], const void* m16) {
    d3dx::Get().quatFromMatrix(
        out, reinterpret_cast<const d3dx::D3DXMATRIXF*>(m16));
}

void QuatMultiply(float out[4], const float a[4], const float b[4]) {
    d3dx::Get().quatMultiply(out, a, b);
}

#if defined(_M_IX86)
static const double kSetRotationTwo = 2.0;

// Original 0x401000, btMatrix3x3::setRotation.  The VC9 build deliberately
// keeps selected intermediates in x87 extended precision and spills others
// to float.  That exact pattern affects Bullet's initial world transforms.
__declspec(naked) void __fastcall SetRotationOriginal(
    float* /*basis*/, const float* /*quaternion*/) {
    __asm {
        sub esp, 24h
        mov eax, edx
        fld dword ptr [eax+4]
        fld dword ptr [eax]
        fld dword ptr [eax+8]
        fld dword ptr [eax+0Ch]
        fld st(2)
        fmulp st(3), st
        fld st(3)
        fmulp st(4), st
        fxch st(2)
        faddp st(3), st
        fmul st, st
        faddp st(2), st
        fmul st, st
        faddp st(1), st
        fstp dword ptr [esp+20h]
        fld dword ptr [esp+20h]
        fdivr qword ptr [kSetRotationTwo]
        fstp dword ptr [esp+20h]
        fld dword ptr [eax]
        fld dword ptr [esp+20h]
        fld st
        fmulp st(2), st
        fxch st(1)
        fstp dword ptr [esp+20h]
        fld st
        fmul dword ptr [eax+4]
        fstp dword ptr [esp+4]
        fmul dword ptr [eax+8]
        fstp dword ptr [esp+8]
        fld dword ptr [esp+20h]
        fld st
        fmul dword ptr [eax+0Ch]
        fstp dword ptr [esp]
        fld dword ptr [esp+4]
        fld st
        fmul dword ptr [eax+0Ch]
        fstp dword ptr [esp+20h]
        fld dword ptr [esp+8]
        fld st
        fmul dword ptr [eax+0Ch]
        fstp dword ptr [esp+10h]
        fld dword ptr [eax]
        fmulp st(3), st
        fxch st(2)
        fstp dword ptr [esp+18h]
        fld dword ptr [eax]
        fmul st, st(1)
        fstp dword ptr [esp+0Ch]
        fld dword ptr [eax]
        fmul st, st(2)
        fstp dword ptr [esp+14h]
        fmul dword ptr [eax+4]
        fstp dword ptr [esp+4]
        fld st
        fmul dword ptr [eax+4]
        fstp dword ptr [esp+1Ch]
        fmul dword ptr [eax+8]
        fstp dword ptr [esp+8]
        fld dword ptr [esp+8]
        fld st
        fld dword ptr [esp+4]
        fld st
        faddp st(2), st
        fld1
        fld st
        fsubrp st(3), st
        fxch st(2)
        fstp dword ptr [ecx]
        fld dword ptr [esp+0Ch]
        fld st
        fld dword ptr [esp+10h]
        fld st
        fsubp st(2), st
        fxch st(1)
        fstp dword ptr [ecx+4]
        fld dword ptr [esp+14h]
        fld st
        fadd dword ptr [esp+20h]
        fstp dword ptr [ecx+8]
        fldz
        fst dword ptr [ecx+0Ch]
        fxch st(3)
        faddp st(2), st
        fxch st(1)
        fstp dword ptr [ecx+10h]
        fld dword ptr [esp+18h]
        fld st
        faddp st(6), st
        fld st(4)
        fsubrp st(6), st
        fxch st(5)
        fstp dword ptr [ecx+14h]
        fld dword ptr [esp+1Ch]
        fld st
        fsub dword ptr [esp]
        fstp dword ptr [ecx+18h]
        fxch st(2)
        fst dword ptr [ecx+1Ch]
        fld dword ptr [esp+20h]
        fsubp st(2), st
        fxch st(1)
        fstp dword ptr [ecx+20h]
        fld dword ptr [esp]
        faddp st(2), st
        fxch st(1)
        fstp dword ptr [ecx+24h]
        fxch st(1)
        faddp st(3), st
        fxch st(1)
        fsubrp st(2), st
        fxch st(1)
        fstp dword ptr [ecx+28h]
        fstp dword ptr [ecx+2Ch]
        add esp, 24h
        ret
    }
}
#else
void SetRotationOriginal(float* basis, const float* quaternion) {
    auto* matrix = reinterpret_cast<btMatrix3x3*>(basis);
    matrix->setRotation(btQuaternion(quaternion[0], quaternion[1],
                                     quaternion[2], quaternion[3]));
}
#endif

// Porting-era dumps under MIKUDANCESTUDIO_STATE_DUMP_DIR (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stubs below keep the call
// sites valid and inline away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
void DumpRigidCreateState(int shape, int mode, const float* inputMatrix,
                          const float* quaternion, const btRigidBody* body) {
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH || body == nullptr)
        return;
    CreateDirectoryA(directory, nullptr);
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\rigid_create.jsonl", directory);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, "ab") != 0 || stream == nullptr)
        return;
    const auto writeWords = [stream](const void* source, int count) {
        const auto* bytes = static_cast<const unsigned char*>(source);
        for (int i = 0; i < count; ++i) {
            if (i != 0)
                std::fputc(',', stream);
            std::uint32_t value = 0;
            std::memcpy(&value, bytes + 4 * i, 4);
            std::fprintf(stream, "\"%08X\"", value);
        }
    };
    std::fprintf(stream, "{\"shape\":%d,\"mode\":%d,"
                         "\"input_matrix_bits\":[", shape, mode);
    writeWords(inputMatrix, 16);
    std::fputs("],\"quaternion_bits\":[", stream);
    writeWords(quaternion, 4);
    std::fputs("],\"world_bits\":[", stream);
    writeWords(&body->getWorldTransform(), 16);
    // Numeric solver state from m_invInertiaTensorWorld through the sleeping
    // thresholds.  The following optional motion-state/constraint-array fields
    // contain process pointers and are intentionally excluded.
    std::fputs("],\"solver_bits\":[", stream);
    writeWords(reinterpret_cast<const unsigned char*>(body) + 0x110,
               (0x208 - 0x110) / 4);
    std::fputs("]}\n", stream);
    std::fclose(stream);
}

void DumpConstraintCreateState(
        int index, btGeneric6DofSpringConstraint* constraint) {
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH || constraint == nullptr)
        return;
    CreateDirectoryA(directory, nullptr);
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\constraint_create.jsonl", directory);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, "ab") != 0 || stream == nullptr)
        return;
    const auto writeWords = [stream](const void* source, int count) {
        const auto* bytes = static_cast<const unsigned char*>(source);
        for (int i = 0; i < count; ++i) {
            if (i != 0)
                std::fputc(',', stream);
            std::uint32_t value = 0;
            std::memcpy(&value, bytes + 4 * i, 4);
            std::fprintf(stream, "\"%08X\"", value);
        }
    };
    const auto* base = reinterpret_cast<const unsigned char*>(constraint);
    const auto* linear = constraint->getTranslationalLimitMotor();
    std::fprintf(stream,
                 "{\"index\":%d,\"object_size\":%u,"
                 "\"user_constraint_id\":%d,"
                 "\"frame_a_offset\":%u,\"frame_b_offset\":%u,"
                 "\"linear_motor_offset\":%u,"
                 "\"angular_motor_offset\":%u,\"frame_a_bits\":[",
                 index, static_cast<unsigned>(sizeof(*constraint)),
                 constraint->getUserConstraintId(),
                 static_cast<unsigned>(
                     reinterpret_cast<const unsigned char*>(
                         &constraint->getFrameOffsetA()) - base),
                 static_cast<unsigned>(
                     reinterpret_cast<const unsigned char*>(
                         &constraint->getFrameOffsetB()) - base),
                 static_cast<unsigned>(
                     reinterpret_cast<const unsigned char*>(linear) - base),
                 static_cast<unsigned>(
                     reinterpret_cast<const unsigned char*>(
                         constraint->getRotationalLimitMotor(0)) - base));
    writeWords(&constraint->getFrameOffsetA(), 16);
    std::fputs("],\"frame_b_bits\":[", stream);
    writeWords(&constraint->getFrameOffsetB(), 16);
    std::fputs("],\"limit_bits\":[", stream);
    writeWords(&linear->m_lowerLimit, 3);
    std::fputc(',', stream);
    writeWords(&linear->m_upperLimit, 3);
    for (int axis = 0; axis < 3; ++axis) {
        const auto* motor = constraint->getRotationalLimitMotor(axis);
        std::fputc(',', stream);
        writeWords(&motor->m_loLimit, 1);
        std::fputc(',', stream);
        writeWords(&motor->m_hiLimit, 1);
    }
    std::fputs("],\"spring_tail_bits\":[", stream);
    writeWords(base + sizeof(*constraint) - 80, 20);
    std::fputs("]}\n", stream);
    std::fclose(stream);
}
#else
inline void DumpRigidCreateState(int, int, const float*, const float*,
                                 const btRigidBody*) {}
inline void DumpConstraintCreateState(int, btGeneric6DofSpringConstraint*) {}
#endif

}  // namespace

#if defined(_M_IX86)
// VC9 keeps each point/matrix dot product in x87 extended precision and
// rounds only at fstp (original PMX path 0x4C1775..0x4C183B).  Modern MSVC's
// mulss/addss chain rounds after every operation and perturbs the initial
// Bullet constraint frame by several ULP.
__declspec(naked) void __fastcall TransformJointPointOriginalFast(
    float* /*out*/, const float* /*point*/, const float* /*matrix*/) {
    __asm {
        mov eax, [esp+4]

        fld dword ptr [edx]
        fmul dword ptr [eax]
        fld dword ptr [edx+4]
        fmul dword ptr [eax+10h]
        faddp st(1), st
        fld dword ptr [edx+8]
        fmul dword ptr [eax+20h]
        faddp st(1), st
        fadd dword ptr [eax+30h]
        fstp dword ptr [ecx]

        fld dword ptr [edx]
        fmul dword ptr [eax+4]
        fld dword ptr [edx+4]
        fmul dword ptr [eax+14h]
        faddp st(1), st
        fld dword ptr [edx+8]
        fmul dword ptr [eax+24h]
        faddp st(1), st
        fadd dword ptr [eax+34h]
        fstp dword ptr [ecx+4]

        fld dword ptr [edx]
        fmul dword ptr [eax+8]
        fld dword ptr [edx+4]
        fmul dword ptr [eax+18h]
        faddp st(1), st
        fld dword ptr [edx+8]
        fmul dword ptr [eax+28h]
        faddp st(1), st
        fadd dword ptr [eax+38h]
        fstp dword ptr [ecx+8]
        ret 4
    }
}
#endif

void TransformJointPointOriginal(float out[3], const float point[3],
                                 const float matrix[16]) {
#if defined(_M_IX86)
    TransformJointPointOriginalFast(out, point, matrix);
#else
    for (int column = 0; column < 3; ++column) {
        out[column] = point[0] * matrix[column]
                    + point[1] * matrix[4 + column]
                    + point[2] * matrix[8 + column]
                    + matrix[12 + column];
    }
#endif
}

// 关节过拉伸限位半径上界：distA/distB 为关节锚点经两刚体逆变换后的
// 局部距离，限位项按轴取 |lim[axis]| 与 |lim[3+axis]| 的较大者再求
// 欧氏范数。x64 原版在两个加载器里是同一段内联数学——
//   PMD sub_7FF7CB4D2670 @0x7FF7CB4D51B0..0x4D526D（lim 表 +0x48..0x5C，
//   三对比较 0x48/0x54、0x4C/0x58、0x50/0x5C，平方和过 sqrtf 后累加
//   进关节记录 +0x94）；
//   PMX sub_7FF7CB4C9AC0 @0x7FF7CB4D1B8A..0x4D1C4A（指令形态逐条一致）。
// 物理编辑对话框（physics_model_dialog.cpp）的逐轴 maxAbs 写法与此同源。
float JointRadiusBound(const float limits[6], float distA, float distB) {
    float maxAbs[3];
    for (int axis = 0; axis < 3; ++axis) {
        float upper = std::fabs(limits[axis]);
        const float lower = std::fabs(limits[3 + axis]);
        if (lower > upper)
            upper = lower;
        maxAbs[axis] = upper;
    }
    return distA + distB + std::sqrt(maxAbs[0] * maxAbs[0] +
                                     maxAbs[1] * maxAbs[1] +
                                     maxAbs[2] * maxAbs[2]);
}

// VA 0x004064F0 - build one Bullet rigid body from PMD data.
// shape 0 = sphere(sx), 1 = box(sx,sy,sz), 2 = capsule(sx radius, sy height).
// mode != 0 -> dynamic (mass used); mode == 0 -> kinematic bone follower.
// out[0] receives the body id (m_debugBodyId), out[1] the btRigidBody*.
// The scene world is built by SceneConstruct (0x4032B0) during UI creation
// (0x466D20), so it is always live here like in the original.
void CreateRigidBody(PhysicsScene* scene, void** out, int shape, float sx, float sy,
                     float sz, const float* mat16, int mode, float mass,
                     float dampLin, float dampAng, float restitution,
                     float friction, char grp, std::uint16_t mask) {
    btDiscreteDynamicsWorld* world = scene->world;
    btCollisionShape* colShape = nullptr;
    if (shape == 0) {
        colShape = new btSphereShape(sx);                      // 0x406531
    } else if (shape == 1) {
        colShape = new btBoxShape(btVector3(sx, sy, sz));      // 0x40657F
    } else if (shape == 2) {
        colShape = new btCapsuleShape(sx, sy);                 // 0x4065CF
    } else {
        out[0] = reinterpret_cast<void*>(                       // 0x4068A4
            static_cast<std::intptr_t>(-1));
        out[1] = nullptr;
        return;
    }

    // 0x406607..0x40676D: start transform = matrix translation + basis
    // rebuilt through the D3DX quaternion round-trip (sub_401000 =
    // btMatrix3x3::setRotation).
    btTransform startTrans;
    startTrans.setIdentity();
    startTrans.setOrigin(btVector3(mat16[12], mat16[13], mat16[14]));
    float q[4];
    QuatFromMatrix(q, mat16);
    SetRotationOriginal(reinterpret_cast<float*>(&startTrans.getBasis()), q);

    // 0x406777: mass zero for kinematic bodies; inertia from the shape
    // (virtual calculateLocalInertia, vtable slot 7).
    const float useMass = mode ? mass : 0.0f;
    btVector3 inertia(0.0f, 0.0f, 0.0f);
    colShape->calculateLocalInertia(useMass, inertia);

    btDefaultMotionState* motionState =
        new btDefaultMotionState(startTrans);                  // 0x4067B5
    btRigidBody::btRigidBodyConstructionInfo ci(               // 0x4067EF
        useMass, motionState, colShape, inertia);
    btRigidBody* body = new btRigidBody(ci);                   // 0x406803

    world->addRigidBody(body, 1 << grp, mask);                 // 0x406857
    body->setDamping(dampLin, dampAng);                        // 0x40686B
    body->setRestitution(restitution);                         // 0x406875
    body->setFriction(friction);                               // 0x40687E
    if (mode == 0)
        body->setCollisionFlags(                               // 0x406886
            body->getCollisionFlags() |
            btCollisionObject::CF_KINEMATIC_OBJECT);
    body->setActivationState(DISABLE_DEACTIVATION);           // 0x406891

    DumpRigidCreateState(shape, mode, mat16, q, body);

    out[0] = reinterpret_cast<void*>(                           // 0x406899
        static_cast<std::intptr_t>(body->m_debugBodyId));
    out[1] = body;
}

#ifdef MIKUDANCESTUDIO_DIAG
static unsigned JointBits(float value) {
    unsigned bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

static std::string WordsHex(const float* values, int count) {
    std::string out;
    char cell[16];
    for (int i = 0; i < count; ++i) {
        std::snprintf(cell, sizeof cell, "%08X ", JointBits(values[i]));
        out += cell;
    }
    return out;
}
#endif

// VA 0x00406010 - build one 6DOF spring joint between two rigid bodies.
// Returns the constraint id assigned from the scene counter (scene+56);
// the btGeneric6DofSpringConstraint pointer is retained by the world.
int CreatePhysJoint(PhysicsScene* scene, void* rbA, void* rbB,
                    float ax, float ay, float az, float aq0, float aq1,
                    float aq2, float aq3, float bx, float by, float bz,
                    float bq0, float bq1, float bq2, float bq3,
                    float linUpper0, float linUpper1, float linUpper2,
                    float linLower0, float linLower1, float linLower2,
                    float angUpper0, float angUpper1, float angUpper2,
                    float angLower0, float angLower1, float angLower2,
                    float spring0, float spring1, float spring2,
                    float spring3, float spring4, float spring5) {
    btDiscreteDynamicsWorld* world = scene->world;
#ifdef MIKUDANCESTUDIO_DIAG
    // Env-gated (MIKUDANCESTUDIO_DUMP_JOINTARGS=<path>) creation-argument trace -
    // the original receives the very same 32 floats on the stack at
    // 0x406010, so a probe there yields a directly comparable record.
    static FILE* jointArgLog = []() -> FILE* {
        const char* path = std::getenv("MIKUDANCESTUDIO_DUMP_JOINTARGS");
        return path ? std::fopen(path, "w") : nullptr;
    }();
    if (jointArgLog) {
        std::fprintf(jointArgLog,
            "jnt a=%08X %08X %08X qa=%08X %08X %08X %08X "
            "b=%08X %08X %08X qb=%08X %08X %08X %08X "
            "spr=%08X %08X %08X %08X %08X %08X\n",
            JointBits(ax), JointBits(ay), JointBits(az),
            JointBits(aq0), JointBits(aq1), JointBits(aq2), JointBits(aq3),
            JointBits(bx), JointBits(by), JointBits(bz),
            JointBits(bq0), JointBits(bq1), JointBits(bq2), JointBits(bq3),
            JointBits(spring0), JointBits(spring1), JointBits(spring2),
            JointBits(spring3), JointBits(spring4), JointBits(spring5));
        std::fflush(jointArgLog);
    }
#endif
    btTransform frameA, frameB;
    frameA.setIdentity();
    frameA.setOrigin(btVector3(ax, ay, az));
    const float qa[4] = {aq0, aq1, aq2, aq3};
    SetRotationOriginal(reinterpret_cast<float*>(&frameA.getBasis()), qa);
    frameB.setIdentity();
    frameB.setOrigin(btVector3(bx, by, bz));
    const float qb[4] = {bq0, bq1, bq2, bq3};
    SetRotationOriginal(reinterpret_cast<float*>(&frameB.getBasis()), qb);
#ifdef MIKUDANCESTUDIO_DIAG
    if (jointArgLog) {
        std::fprintf(jointArgLog, "frm A=%s B=%s\n",
            WordsHex(reinterpret_cast<const float*>(&frameA), 16).c_str(),
            WordsHex(reinterpret_cast<const float*>(&frameB), 16).c_str());
        std::fflush(jointArgLog);
    }
#endif

    auto* con = new btGeneric6DofSpringConstraint(             // 0x406048
        *static_cast<btRigidBody*>(rbA),
        *static_cast<btRigidBody*>(rbB), frameA, frameB, true);

    con->setLinearLowerLimit(                                  // 0x4062xx
        btVector3(linLower0, linLower1, linLower2));
    con->setLinearUpperLimit(
        btVector3(linUpper0, linUpper1, linUpper2));
    // VC9's __CIfmod path in sub_401260/sub_4012E0 canonicalizes a signed
    // zero result to +0.  The modern fmodf inline preserves -0, which is
    // visible in the rotational motor limit bits.
    const auto canonicalZero = [](float value) {
        return value == 0.0f ? 0.0f : value;
    };
    con->setAngularLowerLimit(                                 // sub_401260
        btVector3(canonicalZero(angLower0), canonicalZero(angLower1),
                  canonicalZero(angLower2)));
    con->setAngularUpperLimit(                                 // sub_4012E0
        btVector3(canonicalZero(angUpper0), canonicalZero(angUpper1),
                  canonicalZero(angUpper2)));

    world->addConstraint(con, false);                          // 0x4064xx

    if (spring0 > 0.0f || spring1 > 0.0f || spring2 > 0.0f ||
        spring3 > 0.0f || spring4 > 0.0f || spring5 > 0.0f) {
        const float springs[6] = {spring0, spring1, spring2,
                                  spring3, spring4, spring5};
        for (int axis = 0; axis < 6; ++axis) {
            if (springs[axis] > 0.0f) {
                con->enableSpring(axis, true);                 // sub_5008D0
                con->setStiffness(axis, springs[axis]);        // sub_500910
            }
        }
        con->setEquilibriumPoint();                            // sub_500930
    }

    int& counter = scene->constraintId;                         // 0x4064xx
    // The original stores this monotonically increasing value at
    // constraint+0x60: distinct solver UID.  m_userConstraintId at +0x18
    // remains -1 in the original; merging the two fields changes the VC9
    // class size and the constraint ordering used by the impulse solver.
    con->setUid(counter);
    DumpConstraintCreateState(counter, con);
    return counter++;
}

// VA 0x004A9220 - select each bone's pose source and propagate transforms.
// For every bone (604-byte stride): bones flagged at +492 take the physics
// result pose (+0x188 pos / +0x194 quat) when physicsMode says so, else the
// kinematic pose (+0x140/+0x14C); the chosen pose lands at +0x16C/+0x178.
// Physics mode 2 additionally applies the accumulated bone-morph offsets.
// The transform updater then runs once for every bone layer through
// ModelRecord::maxBoneLayer.
void SetPhysicsMode(unsigned char* m, int afterPhysics,
                    unsigned char* const* modelSlots, int physicsMode) {
    mdl::ModelRecord& model = *mdl::Mdl(m);
    const int boneCount = model.boneCount;
    mikudancestudio::mdl::BoneRecord* bones = mikudancestudio::mdl::Bones(m);
    for (int i = 0; i < boneCount; ++i) {
        mikudancestudio::mdl::BoneRecord* bone = &bones[i];
        const unsigned char flag = bone->hasRigidBody;
        if (((physicsMode == 1 || (physicsMode >= 2 && bone->physicsDisabled == 0)) && flag) != 0) {
            std::memcpy(bone->physicsOffset, bone->ikBackup, 12);           // pos
            std::memcpy(bone->physicsQuat, bone->ikBackup + 3, 16);           // quat
        } else {
            std::memcpy(bone->physicsOffset, bone->trans, 12);
            std::memcpy(bone->physicsQuat, bone->rotQuat, 16);
        }
    }

    if (model.physicsMode == 2) {
        const int recCount = mdl::BoneMorphOffsetCount(m);
        mdl::BoneMorphOffsetRecord* records = mdl::BoneMorphOffsets(m);
        for (int i = 0; i < recCount; ++i) {
            const mdl::BoneMorphOffsetRecord& record = records[i];
            mikudancestudio::mdl::BoneRecord* bone = &bones[record.boneIndex];
            bone->physicsOffset[0] += record.translation[0];
            bone->physicsOffset[1] += record.translation[1];
            bone->physicsOffset[2] += record.translation[2];
            float out[4];
            QuatMultiply(out,
                         reinterpret_cast<const float*>(bone->physicsQuat),
                         record.rotation);
            std::memcpy(bone->physicsQuat, out, 16);
        }
    }

    const int maxLayer = model.maxBoneLayer;
    for (int i = 0; i <= maxLayer; ++i)
        BoneFrameTransform(m, afterPhysics, i, modelSlots, physicsMode);  // 0x493A60
}

}  // namespace mikudancestudio
