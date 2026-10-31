#pragma once
#include "mikudancestudio/cpu_bone_types.hpp"
#include "btBulletDynamicsCommon.h"
#include <cmath>
#include <cstring>
namespace mikudancestudio::cpu_physics {
// Shared legacy bone/body synchronization and readback; traits provide storage and math.
template <class Traits> struct Kernel {

    using Model = typename Traits::Model;
    using Bone = typename Traits::Bone;
    using Rigid = typename Traits::Rigid;
    using Joint = typename Traits::Joint;
    using Matrix = typename Traits::Matrix;
    using Math = typename Traits::Math;
    static void D3dxRotX(Matrix* m, float a) { Math{}.rotX(m, a); }
    static void D3dxRotY(Matrix* m, float a) { Math{}.rotY(m, a); }
    static void D3dxRotZ(Matrix* m, float a) { Math{}.rotZ(m, a); }
    static void D3dxMul(Matrix* o, const Matrix* a, const Matrix* b) { Math{}.mul(o, a, b); }
    static void D3dxTranslation(Matrix* o, float x, float y, float z) {
        Math{}.translation(o, x, y, z);
    }
    static bool D3dxInverse(Matrix* o, const Matrix* m) { return Math{}.inverse(o, m); }
    static void D3dxQuatFromMatrix(float* q, const Matrix* m) { Math{}.quatFromMatrix(q, m); }
    static void D3dxQuatMul(float* o, const float* a, const float* b) {
        Math{}.quatMultiply(o, a, b);
    }
    static void TransformToMatrix(float out[16], const btTransform& t) {
        const btMatrix3x3& b = t.getBasis();
        const btVector3& o = t.getOrigin();
        out[0] = b[0].x();
        out[1] = b[1].x();
        out[2] = b[2].x();
        out[3] = 0.0f;
        out[4] = b[0].y();
        out[5] = b[1].y();
        out[6] = b[2].y();
        out[7] = 0.0f;
        out[8] = b[0].z();
        out[9] = b[1].z();
        out[10] = b[2].z();
        out[11] = 0.0f;
        out[12] = o.x();
        out[13] = o.y();
        out[14] = o.z();
        out[15] = 1.0f;
    }

    static void MatrixToBasis(btTransform* t, const float* m) {
        t->getBasis().setValue(m[0], m[4], m[8], m[1], m[5], m[9], m[2], m[6], m[10]);
        t->setOrigin(btVector3(m[12], m[13], m[14]));
    }

    static void BuildFollowMatrix(float out[16], const Model* m, const Rigid& rigid,
                                  int boneOverride) {
        Bone* bones = m->boneTable;
        Matrix t;
        auto* o = reinterpret_cast<Matrix*>(out);
        D3dxRotZ(o, rigid.rotation[2]);
        D3dxRotX(&t, rigid.rotation[0]);
        D3dxMul(o, o, &t);
        D3dxRotY(&t, rigid.rotation[1]);
        D3dxMul(o, o, &t);
        D3dxTranslation(&t, rigid.position[0], rigid.position[1], rigid.position[2]);
        D3dxMul(o, o, &t);
        const int bone = boneOverride >= 0 ? boneOverride : rigid.boneIndex;
        if (bone >= 0) {
            const Bone& bn = bones[bone];
            D3dxTranslation(&t, bn.position[0], bn.position[1], bn.position[2]);
            D3dxMul(o, o, &t);
            D3dxMul(o, o, reinterpret_cast<const Matrix*>(bn.matInit));
        }
    }

    static void ReseatRigidBody(btRigidBody* body, const btTransform& transform) {
        const btVector3 zero(0.0f, 0.0f, 0.0f);
        body->setWorldTransform(transform);
        body->setLinearVelocity(zero);
        body->setAngularVelocity(zero);
        body->setInterpolationLinearVelocity(zero);
        body->setInterpolationAngularVelocity(zero);
    }

    static void StopBodyAt(btRigidBody* body, const btTransform& transform) {
        const btVector3 zero(0.0f, 0.0f, 0.0f);
        body->setWorldTransform(transform);
        body->setLinearVelocity(zero);
        body->setAngularVelocity(zero);
    }

    static void ModelKinematicSync(Model* m) {
        const int count = m->rigidCount;
        if (count <= 0)
            return;
        Rigid* rigids = m->rigidTable;
        for (int i = 0; i < count; ++i) {
            const Rigid& rec = rigids[i];
            if (rec.mode != 0)
                continue;
            btRigidBody* body = rec.body;
            if (body == nullptr)
                continue; // port guard
            float mat[16];
            BuildFollowMatrix(mat, m, rec, -1);
            btTransform tr;
            tr.setIdentity();
            MatrixToBasis(&tr, mat);
            if (btMotionState* ms = body->getMotionState())
                ms->setWorldTransform(tr);
        }
    }

    static void ModelDynamicReseat(Model* m) {
        const int count = m->rigidCount;
        if (count <= 0)
            return;
        Rigid* rigids = m->rigidTable;
        const int centerBone = m->centerBone; // 0x4B3460
        for (int i = 0; i < count; ++i) {
            const Rigid& rec = rigids[i];
            if (static_cast<signed char>(rec.mode) <= 0)
                continue;
            btRigidBody* body = rec.body;
            if (body == nullptr)
                continue; // port guard
            float mat[16];
            const int link = rec.boneIndex;
            BuildFollowMatrix(mat, m, rec, link < 0 ? centerBone : -1);
            btTransform tr;
            tr.setIdentity();
            MatrixToBasis(&tr, mat);
            ReseatRigidBody(body, tr);
        }
    }

    static void ModelPhysicsReadback(Model* m) {
        Bone* bones = m->boneTable;
        Rigid* rigids = m->rigidTable;
        const int rcount = m->rigidCount;
        Joint* joints = m->jointTable;
        const int jcount = m->jointCount;

        // ---- part 1: joint overstretch clamp ------------------------------
        for (int j = 0; j < jcount; ++j) {
            const Joint& joint = joints[j];
            const int ra = joint.rigidA;
            const int rb = joint.rigidB;
            if (ra < 0 || ra >= rcount || rb < 0 || rb >= rcount)
                continue; // port guard
            btRigidBody* bodyA = rigids[ra].body;
            btRigidBody* bodyB = rigids[rb].body;
            if (bodyA == nullptr || bodyB == nullptr)
                continue; // port guard
            float ma[16], mb[16];
            TransformToMatrix(ma, bodyA->getWorldTransform());
            TransformToMatrix(mb, bodyB->getWorldTransform());
            // Original 0x4B26B5 / x64 0x7FF7CB4E3660..0x7FF7CB4E36D9: d2 =
            // ((dy*dy + dx*dx) + dz*dz) and everything downstream runs single
            // precision - sqrtf, the (dist - limit) comiss, divss limit/dist
            // for k, and the (mb-ma)*k products as mulss into the translation.
            const float dy = ma[13] - mb[13], dx = ma[12] - mb[12], dz = ma[14] - mb[14];
            const float d2 = dy * dy + dx * dx + dz * dz;
            const float dist = std::sqrt(d2);
            const float limit = joint.radiusBound;
            if (dist - limit < 2.0f)
                continue;
            const float k = limit / dist;
            float t[16];
            D3dxTranslation(reinterpret_cast<Matrix*>(t), (mb[12] - ma[12]) * k,
                            (mb[13] - ma[13]) * k, (mb[14] - ma[14]) * k);
            D3dxMul(reinterpret_cast<Matrix*>(ma), reinterpret_cast<const Matrix*>(ma),
                    reinterpret_cast<const Matrix*>(t));
            btTransform tr = bodyB->getWorldTransform();
            tr.setOrigin(btVector3(ma[12], ma[13], ma[14]));
            StopBodyAt(bodyB, tr); // x64 inline reset, see StopBodyAt
        }

        // ---- part 2: dynamic bodies drive the bone world matrices ---------
        for (int i = 0; i < rcount; ++i) {
            const Rigid& rec = rigids[i];
            if (static_cast<signed char>(rec.mode) <= 0)
                continue;
            const int bone = rec.boneIndex;
            if (bone < 0)
                continue;
            btRigidBody* body = rec.body;
            if (body == nullptr)
                continue; // port guard
            float w[16];
            TransformToMatrix(w, body->getWorldTransform());
            // Original 0x4B29CE routes this through the real D3DXMatrixMultiply
            // (x87, one rounding per element); an SSE float chain drifts 1-2 ULP
            // into every physics bone.
            D3dxMul(reinterpret_cast<Matrix*>(m->boneTable[bone].matInit),
                    reinterpret_cast<const Matrix*>(rec.invTransform),
                    reinterpret_cast<const Matrix*>(w));
        }

        // ---- part 3: derive the per-bone physics pose ----------------------
        for (int i = 0; i < rcount; ++i) {
            const Rigid& rigid = rigids[i];
            if (static_cast<signed char>(rigid.mode) <= 0)
                continue;
            const int bi = rigid.boneIndex;
            if (bi < 0)
                continue;
            Bone& bone = m->boneTable[bi];
            float* world = bone.matInit;
            float* local = bone.matLocal;
            const int parent = bone.parent;
            float inv[16];
            bool haveLocal;
            if (parent < 0) {
                std::memcpy(local, world, 16 * sizeof(float));
                haveLocal = true;
            } else if (D3dxInverse(reinterpret_cast<Matrix*>(inv),
                                   reinterpret_cast<const Matrix*>(m->boneTable[parent].matInit))) {
                D3dxMul(reinterpret_cast<Matrix*>(local), reinterpret_cast<const Matrix*>(world),
                        reinterpret_cast<const Matrix*>(inv));
                haveLocal = true;
            } else {
                haveLocal = false;
            }
            if (!haveLocal) {
                // singular parent world matrix: original falls straight through
                // to the +244 copy below.
                std::memcpy(bone.matExtra, bone.matInit,
                            sizeof(bone.matExtra)); // 0x4B419x
                continue;
            }

            float* q = bone.ikBackup + 3; // +0x194
            D3dxQuatFromMatrix(q, reinterpret_cast<const Matrix*>(local));

            // +0x188 position = translation of T(p) * local * T(-p); the
            // T(p) * local product is kept for the mode==2 branch below.
            float work[16], pos[16], t[16];
            D3dxTranslation(reinterpret_cast<Matrix*>(t), bone.position[0], bone.position[1],
                            bone.position[2]);
            D3dxMul(reinterpret_cast<Matrix*>(work), reinterpret_cast<const Matrix*>(t),
                    reinterpret_cast<const Matrix*>(local));
            D3dxTranslation(reinterpret_cast<Matrix*>(t), -bone.position[0], -bone.position[1],
                            -bone.position[2]);
            D3dxMul(reinterpret_cast<Matrix*>(pos), reinterpret_cast<const Matrix*>(work),
                    reinterpret_cast<const Matrix*>(t));
            std::memcpy(bone.ikBackup, pos + 12, 3 * sizeof(float));

            const int target = bone.tailIdx;
            Bone& tb = target >= 0 ? m->boneTable[target] : bone;

            if (bone.type == cpu_bone::BoneType::RotateGrant) { // tail type
                float conj[4] = {-tb.physicsQuat[0], -tb.physicsQuat[1], -tb.physicsQuat[2],
                                 tb.physicsQuat[3]};
                float out[4];
                D3dxQuatMul(out, conj, q);
                std::memcpy(q, out, sizeof out);
            }
            if ((bone.flags & cpu_bone::kBoneFlagRotInherit) != 0) {
                float limit[4];
                D3dxQuatFromMatrix(limit, reinterpret_cast<const Matrix*>(tb.matLocal));
                if (limit[3] > 1.0f)
                    limit[3] = 1.0f;
                if (limit[3] < -1.0f)
                    limit[3] = -1.0f;
                // x64 0x7FF7CB4E3C85: acosf (float CRT), then subss with the
                // truncated-pi float 0x4048F5C3.
                float angle = std::acos(limit[3]);
                if (limit[3] < 0.0f)
                    angle -= 3.140000104904175f; // 0x529xxx
                // Original 0x4B2CED / x64 0x7FF7CB4E3CD6: ((y*y + x*x) + z*z)
                // through sqrtf.
                const float axisLen =
                    std::sqrt(limit[1] * limit[1] + limit[0] * limit[0] + limit[2] * limit[2]);
                if (angle != 0.0f && axisLen >= 0.00000011920929f) {
                    const float a = angle * bone.inheritRatio;
                    // x64 0x7FF7CB4E3D0E/0x7FF7CB4E3D5F: sinf/cosf and a
                    // divss sin/axisLen.
                    const float s = std::sin(a) / axisLen;
                    const float rot[4] = {-limit[0] * s, -limit[1] * s, -limit[2] * s, std::cos(a)};
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
            if ((bone.flags & cpu_bone::kBoneFlagTransInherit) != 0) {
                for (int c = 0; c < 3; ++c)
                    bone.physicsOffset[c] -= tb.physicsOffset[c] * bone.inheritRatio;
            }
            if (rigid.mode == 2) {
                // mode 2: collapse the physics pose onto the bone position and
                // rebuild the local/world matrices around it.
                std::memset(bone.trans, 0, sizeof(bone.trans));
                std::memset(bone.ikBackup, 0, 3 * sizeof(float));
                work[12] = work[13] = work[14] = 0.0f;
                D3dxTranslation(reinterpret_cast<Matrix*>(t), -bone.position[0], -bone.position[1],
                                -bone.position[2]);
                D3dxMul(reinterpret_cast<Matrix*>(local), reinterpret_cast<const Matrix*>(t),
                        reinterpret_cast<const Matrix*>(work));
                D3dxTranslation(reinterpret_cast<Matrix*>(t), bone.position[0], bone.position[1],
                                bone.position[2]);
                D3dxMul(reinterpret_cast<Matrix*>(local), reinterpret_cast<const Matrix*>(local),
                        reinterpret_cast<const Matrix*>(t));
                if (parent >= 0)
                    D3dxMul(reinterpret_cast<Matrix*>(world),
                            reinterpret_cast<const Matrix*>(local),
                            reinterpret_cast<const Matrix*>(m->boneTable[parent].matInit));
            }
            std::memcpy(bone.matExtra, bone.matInit, sizeof(bone.matExtra));
        }
    }
};
}
