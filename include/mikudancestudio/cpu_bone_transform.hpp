#pragma once
// Shared five-phase MMD bone/CCD algorithm, extracted from bone_transform.cpp.
// Traits select semantic state and math; no original record layout is assumed.
#include "mikudancestudio/cpu_bone_types.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
namespace mikudancestudio::cpu_bone {
template <class Traits> struct TransformKernel {
    using Bone = typename Traits::Bone;
    using Model = typename Traits::Model;
    using IkChain = typename Traits::IkChain;
    using Binding = typename Traits::Binding;
    using Matrix = typename Traits::Matrix;
    using D3 = typename Traits::Math;
    static constexpr float kPiF = 3.140000104904175f;
    static constexpr float kEps = 1.1920929e-7f;         // 0x493BF7 FLT_EPSILON
    static constexpr float kHalfPi = 1.570796012878418f; // 0x495947 (float pi/2)
    static constexpr float kKneeClamp = 1.535889f;       // 0x4959CC

    // Left knee / right knee (SJIS), 6-byte compares at 0x4955F1/0x495620 and
    // 0x496468/0x496483; string constants 0x530F60 / 0x530F58.
    static constexpr char kNameKneeLJp[7] = "\x8d\xb6\x82\xd0\x82\xb4";
    static constexpr char kNameKneeRJp[7] = "\x89\x45\x82\xd0\x82\xb4";

    static bool IsKnee(const Bone& bone) {
        return std::strncmp(bone.name, kNameKneeLJp, 6) == 0 ||
               std::strncmp(bone.name, kNameKneeRJp, 6) == 0;
    }

    // D3DX imports preserve the runtime's floating-point behavior.

    static void MatRotQuat(Matrix* out, const float q[4]) { D3{}.matrixRotationQuaternion(out, q); }

    static void Vec3Norm(float out[3], const float in[3]) { D3{}.normalize(out, in); }

    // D3DXQuaternionMultiply semantics: out = q2 * q1.
    static void QuatMul(float out[4], const float a[4], const float b[4]) {
        D3{}.quatMultiply(out, a, b);
    }

    // ---- shared pieces --------------------------------------------------------

    // quat of the source bone's +116 matrix scaled about its own axis by
    // `rate`, post-multiplied onto q (0x493B12..0x493CA4 pattern).
    // x64 (0x7FF7CB4DA373..0x7FF7CB4DA476): acosf/sqrtf/sinf/cosf and a
    // divss sin/len - single precision end to end; kPiF is the float image
    // 0x4048F5C3 (dword_7FF7CB552B90).
    static void InheritRotQuat(float q[4], const Bone* bones, int srcIdx, float rate) {
        D3 d3;
        Matrix rot;
        std::memcpy(&rot, bones[srcIdx].matLocal, sizeof(rot));
        float sq[4];
        d3.quatFromMatrix(sq, &rot);
        if (sq[3] <= 1.0f) {
            if (sq[3] < -1.0f)
                sq[3] = -1.0f;
        } else {
            sq[3] = 1.0f;
        }
        float angle = std::acos(sq[3]);
        if (sq[3] < 0.0f)
            angle -= kPiF;
        const float len = std::sqrt(sq[0] * sq[0] + sq[1] * sq[1] + sq[2] * sq[2]);
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
    static void WorldFromParent(Bone& bone, const Bone& parent, D3* d3) {
        Matrix local, pw, t;
        std::memcpy(&local, bone.matLocal, sizeof(local));
        std::memcpy(&pw, parent.matInit, sizeof(pw));
        d3->mul(reinterpret_cast<Matrix*>(bone.matInit), &local, &pw);
        std::memcpy(&t, parent.matInit, sizeof(t));
        t.m[3][0] = t.m[3][1] = t.m[3][2] = 0.0f;
        t.m[3][3] = 1.0f;
        std::memcpy(bone.matWorld, &t, sizeof(t));
    }

    // World position of `bone`: the matrix at +52 applied to the rest position
    // at +308.  The original v63[13]/[17]/[21]/[25] indices were relative to
    // the bone record; `mw` is already rebased to +52, so they become 0/4/8/12.
    static void WorldPos(float dst[3], const Bone& bone) {
        const float* mw = bone.matInit;
        const float* rest = bone.position;
        // 0x494B5D..0x494BD1 and 0x494D3A..0x494E4C use x87 and keep
        // each multiply/add chain in extended precision until the final fstp.
        // The isolated numeric helper retains that evaluation model; a
        // few ULP here become visible after many CCD iterations.
#if defined(_M_IX86)
        D3::transformPoint(dst, rest, mw);
#else
        // x64 original: same source compiled for SSE2 - every multiply/add
        // rounds to float, which the plain float expression reproduces.
        dst[0] = mw[0] * rest[0] + mw[4] * rest[1] + mw[8] * rest[2] + mw[12];
        dst[1] = mw[1] * rest[0] + mw[5] * rest[1] + mw[9] * rest[2] + mw[13];
        dst[2] = mw[2] * rest[0] + mw[6] * rest[1] + mw[10] * rest[2] + mw[14];
#endif
    }

    static float Dot3(const float a[3], const float b[3]) {
#if defined(_M_IX86)
        return D3::dot3(a, b);
#else
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
#endif
    }

    // x64 0x7FF7CB4DB862..0x7FF7CB4DB88A: the CCD alignment test subtracts,
    // squares and accumulates in SINGLE precision (subss/mulss/addss, the
    // y term first, then x, then z) and compares against comiss 1e-7f
    // (dword_7FF7CB552C48).
    static float SquaredDistance3(const float a[3], const float b[3]) {
#if defined(_M_IX86)
        return D3::squaredDistance3(a, b);
#else
        const float dx = a[0] - b[0];
        const float dy = a[1] - b[1];
        const float dz = a[2] - b[2];
        return dy * dy + dx * dx + dz * dz;
#endif
    }

    static void Cross3(float dst[3], const float a[3], const float b[3]) {
#if defined(_M_IX86)
        D3::cross3(dst, a, b);
#else
        dst[0] = a[1] * b[2] - a[2] * b[1];
        dst[1] = a[2] * b[0] - a[0] * b[2];
        dst[2] = a[0] * b[1] - a[1] * b[0];
#endif
    }

    // axis = transpose(bone+180 rotation) * axis, normalized (LABEL_135)
    static void ProjectAxisOnLocal(float axis[3], const Bone& bone) {
        const float* r = bone.matWorld;
        float out[3];
        out[0] = Dot3(r, axis);
        out[1] = Dot3(r + 4, axis);
        out[2] = Dot3(r + 8, axis);
        Vec3Norm(axis, out);
    }

    // reflect-clamp one euler angle into [lo, hi]; `twist` disables the
    // reflection and pins to the bound (0x495A91 pattern).
    static void ClampEuler(float& a, float lo, float hi, bool twist) {
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
    static bool ChannelPass(const Bone& bone, unsigned char afterPhysics) {
        const bool has = (bone.flags & cpu_bone::kBoneFlagAfterPhysics) != 0;
        return afterPhysics ? has : !has;
    }

    // +244 copy gate (LABEL_83): ((physicsMode==1 || (physicsMode>=2 &&
    // bone+493==0)) & bone+492) == 0.
    static bool CopyGate(const Bone& bone, int physicsMode) {
        return ((physicsMode == 1 || (physicsMode >= 2 && bone.physicsDisabled == 0)) &
                bone.hasRigidBody) == 0;
    }

    // phase A (0x493A85): local matrix for physics-swept bones (+596 flag
    // set): inherit 0x100 from bone+488's +116, own quat +376, 0x200
    // translation inherit -> bone+116 = T(-rest308) * R * T(off364) *
    // T(rest308).
    static void BoneTransform_PhysicsSweptLocals(const Model* model, D3& d3, Bone* boneRecords,
                                                 int boneCount, unsigned char afterPhysics,
                                                 int layer) {
        for (int i = 0; i < boneCount; ++i) {
            Bone& bone = boneRecords[Traits::Index(*model, i)];
            if (!ChannelPass(bone, afterPhysics))
                continue;
            if (bone.layer != layer || bone.hasFlag == 0)
                continue;
            float q[4] = {bone.physicsQuat[0], bone.physicsQuat[1], bone.physicsQuat[2],
                          bone.physicsQuat[3]};
            if (bone.flags & cpu_bone::kBoneFlagRotInherit)
                InheritRotQuat(q, boneRecords, bone.tailIdx, bone.inheritRatio);
            MatRotQuat(reinterpret_cast<Matrix*>(bone.matLocal), q);
            if (bone.flags & cpu_bone::kBoneFlagTransInherit) {
                Bone& src = boneRecords[bone.tailIdx];
                const float r = bone.inheritRatio;
                Matrix tmp, cur;
                d3.translation(&tmp, src.physicsOffset[0] * r, src.physicsOffset[1] * r,
                               src.physicsOffset[2] * r);
                std::memcpy(&cur, bone.matLocal, sizeof(cur));
                d3.mul(reinterpret_cast<Matrix*>(bone.matLocal), &cur, &tmp);
            }
            Matrix tmp, cur;
            d3.translation(&tmp, -bone.position[0], -bone.position[1], -bone.position[2]);
            std::memcpy(&cur, bone.matLocal, sizeof(cur));
            d3.mul(&cur, &tmp, &cur);
            d3.translation(&tmp, bone.physicsOffset[0], bone.physicsOffset[1],
                           bone.physicsOffset[2]);
            d3.mul(reinterpret_cast<Matrix*>(bone.matLocal), &cur, &tmp);
            std::memcpy(&cur, bone.matLocal, sizeof(cur));
            d3.translation(&tmp, bone.position[0], bone.position[1], bone.position[2]);
            d3.mul(reinterpret_cast<Matrix*>(bone.matLocal), &cur, &tmp);
        }
    }

    // phase B (0x493E71): local matrix for the rest: type 4 resets scale
    // +348..360, type 9 axis-angle from tail(+460) quat x rate(int)(+488)
    // /100, type 5 quat = parent(+488)quat * own, 0x100 inherit; after the
    // sandwich, 0x200 bones add the inherited translation to +364.
    static void BoneTransform_StandardLocals(const Model* model, D3& d3, Bone* boneRecords,
                                             int boneCount, unsigned char afterPhysics, int layer) {
        for (int i = 0; i < boneCount; ++i) {
            Bone& bone = boneRecords[Traits::Index(*model, i)];
            if (!ChannelPass(bone, afterPhysics))
                continue;
            if (bone.layer != layer || bone.hasFlag != 0)
                continue;
            if (bone.type == cpu_bone::BoneType::UnderIk) { // 0x493EDC
                bone.rotQuat2[0] = bone.rotQuat2[1] = bone.rotQuat2[2] = 0.0f;
                bone.rotQuat2[3] = 1.0f;
            }
            if (bone.type == cpu_bone::BoneType::CoRotate) { // 0x493F1F
                // x64 0x7FF7CB4DD069..0x7FF7CB4DD18A: acosf/sqrtf/sinf/cosf,
                // ((float)rate * angle) / 100.0f (divss), single precision.
                const Bone& tail = boneRecords[bone.tailBone];
                float sq[4] = {tail.physicsQuat[0], tail.physicsQuat[1], tail.physicsQuat[2],
                               tail.physicsQuat[3]};
                if (sq[3] <= 1.0f) {
                    if (sq[3] < -1.0f)
                        sq[3] = -1.0f;
                } else {
                    sq[3] = 1.0f;
                }
                float angle = std::acos(sq[3]);
                if (sq[3] < 0.0f)
                    angle -= kPiF;
                const float len = std::sqrt(sq[0] * sq[0] + sq[1] * sq[1] + sq[2] * sq[2]);
                if (angle == 0.0f || len < kEps) {
                    bone.physicsQuat[0] = bone.physicsQuat[1] = bone.physicsQuat[2] = 0.0f;
                    bone.physicsQuat[3] = 1.0f;
                } else {
                    const float scaled = (static_cast<float>(bone.tailIdx) * angle) / 100.0f;
                    const float s = std::sin(scaled) / len;
                    bone.physicsQuat[0] = s * sq[0];
                    bone.physicsQuat[1] = s * sq[1];
                    bone.physicsQuat[2] = s * sq[2];
                    bone.physicsQuat[3] = std::cos(scaled);
                }
            }
            float q[4] = {bone.physicsQuat[0], bone.physicsQuat[1], bone.physicsQuat[2],
                          bone.physicsQuat[3]};
            if (bone.type == cpu_bone::BoneType::RotateGrant) { // 0x49412F
                const Bone& src = boneRecords[bone.tailIdx];
                float sq[4] = {src.physicsQuat[0], src.physicsQuat[1], src.physicsQuat[2],
                               src.physicsQuat[3]};
                float out[4];
                QuatMul(out, sq, q);
                q[0] = out[0];
                q[1] = out[1];
                q[2] = out[2];
                q[3] = out[3];
            }
            if (bone.flags & cpu_bone::kBoneFlagRotInherit)
                InheritRotQuat(q, boneRecords, bone.tailIdx, bone.inheritRatio);
            MatRotQuat(reinterpret_cast<Matrix*>(bone.matLocal), q);
            float inhX = 0.0f, inhY = 0.0f, inhZ = 0.0f;
            if (bone.flags & cpu_bone::kBoneFlagTransInherit) {
                const Bone& src = boneRecords[bone.tailIdx];
                const float r = bone.inheritRatio;
                inhX = src.physicsOffset[0] * r;
                inhY = src.physicsOffset[1] * r;
                inhZ = src.physicsOffset[2] * r;
                Matrix tmp, cur;
                d3.translation(&tmp, inhX, inhY, inhZ);
                std::memcpy(&cur, bone.matLocal, sizeof(cur));
                d3.mul(reinterpret_cast<Matrix*>(bone.matLocal), &cur, &tmp);
            }
            Matrix tmp, cur;
            d3.translation(&tmp, -bone.position[0], -bone.position[1], -bone.position[2]);
            std::memcpy(&cur, bone.matLocal, sizeof(cur));
            d3.mul(&cur, &tmp, &cur);
            d3.translation(&tmp, bone.physicsOffset[0], bone.physicsOffset[1],
                           bone.physicsOffset[2]);
            d3.mul(reinterpret_cast<Matrix*>(bone.matLocal), &cur, &tmp);
            std::memcpy(&cur, bone.matLocal, sizeof(cur));
            d3.translation(&tmp, bone.position[0], bone.position[1], bone.position[2]);
            d3.mul(reinterpret_cast<Matrix*>(bone.matLocal), &cur, &tmp);
            if (bone.flags & cpu_bone::kBoneFlagTransInherit) { // 0x4944F0
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
    static void BoneTransform_WorldPass(const Model* model, D3& d3, Bone* boneRecords,
                                        int boneCount, Binding* extTable, Model* const* modelSlots,
                                        unsigned char afterPhysics, int layer, int physicsMode) {
        for (int i = 0; i < boneCount; ++i) {
            Bone& bone = boneRecords[Traits::Index(*model, i)];
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
                if (slot >= 0) { // 0x4945FE
                    const Model* model2 = modelSlots[slot];
                    Bone* bones2 = model2->boneTable;
                    const std::int32_t extBone = extTable[extIdx].linkedBone;
                    const Bone& src = bones2[extBone];
                    Matrix ext, tmp, local;
                    std::memcpy(&ext, src.matExtra, sizeof(ext));
                    d3.translation(&tmp, src.position[0], src.position[1], src.position[2]);
                    d3.mul(&ext, &tmp, &ext);
                    d3.translation(&tmp, -bone.position[0], -bone.position[1], -bone.position[2]);
                    d3.mul(&ext, &tmp, &ext);
                    std::memcpy(&local, bone.matLocal, sizeof(local));
                    d3.mul(reinterpret_cast<Matrix*>(bone.matInit), &local, &ext);
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
                    const Model* model2 = modelSlots[extTable[0].linkedModel];
                    Bone* bones2 = model2->boneTable;
                    const Bone& src = bones2[extTable[0].linkedBone];
                    Matrix ext, tmp, local;
                    std::memcpy(&ext, src.matExtra, sizeof(ext));
                    d3.translation(&tmp, src.position[0], src.position[1], src.position[2]);
                    d3.mul(&ext, &tmp, &ext);
                    std::memcpy(&local, bone.matLocal, sizeof(local));
                    d3.mul(reinterpret_cast<Matrix*>(bone.matInit), &local, &ext);
                    ext.m[3][0] = ext.m[3][1] = ext.m[3][2] = 0.0f;
                    ext.m[3][3] = 1.0f;
                    std::memcpy(bone.matWorld, &ext, sizeof(ext));
                    handled = true;
                }
            }
            if (!handled) { // 0x494999 root
                std::memcpy(bone.matInit, bone.matLocal, sizeof(bone.matInit));
                Matrix id;
                std::memset(&id, 0, sizeof(id));
                id.m[0][0] = id.m[1][1] = id.m[2][2] = id.m[3][3] = 1.0f;
                std::memcpy(bone.matWorld, &id, sizeof(id));
            }
            if (CopyGate(bone, physicsMode)) // LABEL_83
                std::memcpy(bone.matExtra, bone.matInit, sizeof(bone.matExtra));
        }
    }

    // phase D (0x494A56): CCD IK per 24-byte chain at model+9920 {+0
    // effector, +4 root, +8 links, +12 child*, +16 loops, +18 enabled,
    // +20 angle}; PMX limit planes, PMD axis projection and the knee hinge
    // flip per the file header.  The floating-point sequence is ported
    // verbatim (x87 fidelity notes included) - do not reorder.
    static void BoneTransform_CcdIk(D3& d3, const Model* m, Bone* boneRecords, IkChain* iks,
                                    int ikCount, unsigned char afterPhysics, int layer,
                                    int physicsMode, bool pmx2) {
        for (int ci = 0; ci < ikCount; ++ci) {
            IkChain& ik = iks[ci];
            Bone& target = boneRecords[ik.boneIndex];
            if (!ChannelPass(target, afterPhysics))
                continue;
            if (target.layer != layer || ik.enabled == 0)
                continue;
            Bone& ikRoot = boneRecords[ik.targetBone];
            const Bone& rootParent = Traits::Parent(boneRecords, ikRoot.parent);
            if (((physicsMode == 1 || (physicsMode >= 2 && rootParent.physicsDisabled == 0)) &
                 rootParent.hasRigidBody) != 0)
                continue;
            float eff[3];
            WorldPos(eff, target); // 0x494B7F
            // rebuild the chain root local matrix (0x494BE5)
            {
                Bone& root = ikRoot;
                float q[4] = {root.physicsQuat[0], root.physicsQuat[1], root.physicsQuat[2],
                              root.physicsQuat[3]};
                MatRotQuat(reinterpret_cast<Matrix*>(root.matLocal), q);
                Matrix tmp, cur;
                d3.translation(&tmp, -root.position[0], -root.position[1], -root.position[2]);
                std::memcpy(&cur, root.matLocal, sizeof(cur));
                d3.mul(&cur, &tmp, &cur);
                d3.translation(&tmp, root.physicsOffset[0], root.physicsOffset[1],
                               root.physicsOffset[2]);
                d3.mul(reinterpret_cast<Matrix*>(root.matLocal), &cur, &tmp);
                std::memcpy(&cur, root.matLocal, sizeof(cur));
                d3.translation(&tmp, root.position[0], root.position[1], root.position[2]);
                d3.mul(reinterpret_cast<Matrix*>(root.matLocal), &cur, &tmp);
            }
            const std::uint16_t* child = ik.links;
            const int loops = ik.iterations;
            const float chainAngle = ik.maxAngle;
            if (ik.linkCount == 0)
                continue;
            // 0x494CEE..0x494D08: the original halves the IK iteration count
            // at +16, not the child-link count at +8.  This threshold selects
            // the relaxed PMX-limit pass before the constrained iterations.
            const int twist = loops >> 1; // v341
            int iter = 0;
            while (iter < loops) {
                if (ik.linkCount == 0)
                    break; // LABEL_264 advance
                bool exitAll = false;
                for (int li = 0; li < ik.linkCount; ++li) {
                    Bone& root = ikRoot;
                    float rootPos[3];
                    WorldPos(rootPos, root); // v391..393
                    Bone& link = boneRecords[child[li]];
                    float linkPos[3];
                    WorldPos(linkPos, link); // v394..396
                    float d1[3] = {linkPos[0] - rootPos[0], linkPos[1] - rootPos[1],
                                   linkPos[2] - rootPos[2]};
                    float d2[3] = {linkPos[0] - eff[0], linkPos[1] - eff[1], linkPos[2] - eff[2]};
                    Vec3Norm(d1, d1);
                    Vec3Norm(d2, d2);
                    const float diff2 = SquaredDistance3(d1, d2);
#ifdef MIKUDANCESTUDIO_DIAG
                    Traits::Precheck(m, ci, iter, li, d1, d2, diff2, target, root, link);
#endif
                    if (diff2 < 1e-7f) { // 0x494F37 / x64 comiss
                        iter = loops;    // skip all iterations
                        exitAll = true;
                        break;
                    }
                    float axis[3];
                    Cross3(axis, d1, d2);
                    float align = 1.0f; // v331
                    const float* r180 = link.matWorld;
                    if (!pmx2) { // 0x4955F7 PMD path
                        if (IsKnee(link)) {
                            const float dot1 =
                                r180[0] * axis[0] + r180[1] * axis[1] + r180[2] * axis[2];
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
                        if (limYlo != 0.0f || limYhi != 0.0f || limZlo != 0.0f || limZhi != 0.0f) {
                            if (limXlo == 0.0f && limXhi == 0.0f && limZlo == 0.0f &&
                                limZhi == 0.0f) {
                                // X-plane: axis = +-Y by row-2 dot (0x4950E0)
                                const float dot2 =
                                    r180[4] * axis[0] + r180[5] * axis[1] + r180[6] * axis[2];
                                axis[0] = 0.0f;
                                axis[1] = dot2 < 0.0f ? -1.0f : 1.0f;
                                axis[2] = 0.0f;
                            } else if (limXlo == 0.0f && limXhi == 0.0f && limYlo == 0.0f &&
                                       limYhi == 0.0f) {
                                // Y-plane: axis = +-Z by row-3 dot (0x495187)
                                const float dot3 =
                                    r180[8] * axis[0] + r180[9] * axis[1] + r180[10] * axis[2];
                                axis[0] = 0.0f;
                                axis[1] = 0.0f;
                                axis[2] = dot3 < 0.0f ? -1.0f : 1.0f;
                            } else if (link.flags & cpu_bone::kBoneFlagFixedAxis) {
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
                            const float dot1 =
                                r180[0] * axis[0] + r180[1] * axis[1] + r180[2] * axis[2];
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
                        if ((link.flags & cpu_bone::kBoneFlagFixedAxis) == 0) {
                            ProjectAxisOnLocal(axis, link); // 0x495544 block
                        } else {                            // 0x4953DF
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
                    float dot = Dot3(d2, d1);
                    if (dot > 1.0f)
                        dot = 1.0f;
                    else if (dot < -1.0f)
                        dot = -1.0f;
                    float half = (std::acos(dot) * 0.5f) * align;
                    const float wlim = (static_cast<float>(li + 1) * chainAngle) * 2.0f;
                    if (half >= 0.0f) {
                        if (wlim < half)
                            half = wlim;
                    } else {
                        if (-wlim > half)
                            half = -wlim;
                    }
                    const float sinHalf = std::sin(half);
                    float rotq[4] = {sinHalf * axis[0], sinHalf * axis[1], sinHalf * axis[2],
                                     std::cos(half)};
                    float acc[4] = {link.rotQuat2[0], link.rotQuat2[1], link.rotQuat2[2],
                                    link.rotQuat2[3]};
#ifdef MIKUDANCESTUDIO_DIAG
                    Traits::Probe(m, ci, iter, li, loops, ik.boneIndex, ik.targetBone, child[li],
                                  d1, d2, axis, dot, static_cast<float>(half), acc, rotq);
#endif
                    float outq[4];
                    QuatMul(outq, acc, rotq); // acc = rot * acc
                    link.rotQuat2[0] = outq[0];
                    link.rotQuat2[1] = outq[1];
                    link.rotQuat2[2] = outq[2];
                    link.rotQuat2[3] = outq[3];
                    if (iter == 0) { // 0x49489A
                        float q0[4] = {link.physicsQuat[0], link.physicsQuat[1],
                                       link.physicsQuat[2], link.physicsQuat[3]};
                        float linkQuat[4] = {link.rotQuat2[0], link.rotQuat2[1], link.rotQuat2[2],
                                             link.rotQuat2[3]};
                        QuatMul(outq, q0, linkQuat); // acc = acc * q376
                        link.rotQuat2[0] = outq[0];
                        link.rotQuat2[1] = outq[1];
                        link.rotQuat2[2] = outq[2];
                        link.rotQuat2[3] = outq[3];
                    }
                    float lq[4] = {link.rotQuat2[0], link.rotQuat2[1], link.rotQuat2[2],
                                   link.rotQuat2[3]};
                    Matrix rotM;
                    MatRotQuat(&rotM, lq);
                    if (pmx2 && link.twistEnable != 0) {
                        // euler decomposition + limit clamp + rebuild
                        // (0x495921..0x496429; x64 0x7FF7CB4DC2D3..DC845: the
                        // off-axis angles come from atan2f(elem/c, elem/c))
                        float eX = 0.0f, eY = 0.0f, eZ = 0.0f;
                        Matrix final;
                        if (link.ikLimitMin[0] > -kHalfPi && link.ikLimitMax[0] < kHalfPi) {
                            float a = std::asin(-rotM.m[2][1]);
                            float c = std::cos(a);
                            if (std::fabs(a) > kKneeClamp) {
                                a = a > 0.0f ? kKneeClamp : -kKneeClamp;
                                c = std::cos(a);
                            }
                            eX = a;
                            eY = std::atan2(rotM.m[2][0] / c, rotM.m[2][2] / c);
                            eZ = std::atan2(rotM.m[0][1] / c, rotM.m[1][1] / c);
                            ClampEuler(eX, link.ikLimitMin[0], link.ikLimitMax[0], iter >= twist);
                            ClampEuler(eY, link.ikLimitMin[1], link.ikLimitMax[1], iter >= twist);
                            ClampEuler(eZ, link.ikLimitMin[2], link.ikLimitMax[2], iter >= twist);
                            Matrix mx, my, mz, t1;
                            d3.rotX(&mx, eX);
                            d3.rotY(&my, eY);
                            d3.rotZ(&mz, eZ);
                            d3.mul(&t1, &mz, &mx);    // RotZ*RotX
                            d3.mul(&final, &t1, &my); // RotZ*RotX*RotY
                        } else if (link.ikLimitMin[1] > -kHalfPi && link.ikLimitMax[1] < kHalfPi) {
                            float a = std::asin(-rotM.m[0][2]);
                            float c = std::cos(a);
                            if (std::fabs(a) > kKneeClamp) {
                                a = a > 0.0f ? kKneeClamp : -kKneeClamp;
                                c = std::cos(a);
                            }
                            eY = a;
                            eX = std::atan2(rotM.m[1][2] / c, rotM.m[2][2] / c);
                            eZ = std::atan2(rotM.m[0][1] / c, rotM.m[0][0] / c);
                            ClampEuler(eX, link.ikLimitMin[0], link.ikLimitMax[0], iter >= twist);
                            ClampEuler(eY, link.ikLimitMin[1], link.ikLimitMax[1], iter >= twist);
                            ClampEuler(eZ, link.ikLimitMin[2], link.ikLimitMax[2], iter >= twist);
                            Matrix mx, my, mz, t1;
                            d3.rotX(&mx, eX);
                            d3.rotY(&my, eY);
                            d3.rotZ(&mz, eZ);
                            d3.mul(&t1, &mx, &my);    // RotX*RotY
                            d3.mul(&final, &t1, &mz); // RotX*RotY*RotZ
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
                            ClampEuler(eX, link.ikLimitMin[0], link.ikLimitMax[0], iter >= twist);
                            ClampEuler(eY, link.ikLimitMin[1], link.ikLimitMax[1], iter >= twist);
                            ClampEuler(eZ, link.ikLimitMin[2], link.ikLimitMax[2], iter >= twist);
                            Matrix mx, my, mz, t1;
                            d3.rotX(&mx, eX);
                            d3.rotY(&my, eY);
                            d3.rotZ(&mz, eZ);
                            d3.mul(&t1, &my, &mz);    // RotY*RotZ
                            d3.mul(&final, &t1, &mx); // RotY*RotZ*RotX
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
                        Matrix tmp, cur;
                        d3.translation(&tmp, -link.position[0], -link.position[1],
                                       -link.position[2]);
                        std::memcpy(&cur, link.matLocal, sizeof(cur));
                        d3.mul(&cur, &tmp, &cur);
                        d3.translation(&tmp, link.physicsOffset[0], link.physicsOffset[1],
                                       link.physicsOffset[2]);
                        d3.mul(reinterpret_cast<Matrix*>(link.matLocal), &cur, &tmp);
                        std::memcpy(&cur, link.matLocal, sizeof(cur));
                        d3.translation(&tmp, link.position[0], link.position[1], link.position[2]);
                        d3.mul(reinterpret_cast<Matrix*>(link.matLocal), &cur, &tmp);
                    }
                    // upward world recompute li..0 (0x496620); the +52 gate
                    // uses the a5==3 form, the +244 copy the LABEL_83 form
                    for (int lj = li; lj >= 0; --lj) {
                        Bone& linkedBone = boneRecords[child[lj]];
                        const bool j3 = (physicsMode == 3);
                        if ((linkedBone.hasRigidBody & (linkedBone.physicsDisabled == 0 ? 1 : 0) &
                             (j3 ? 1 : 0)) == 0) {
                            WorldFromParent(linkedBone,
                                            Traits::Parent(boneRecords, linkedBone.parent), &d3);
                            if (((physicsMode == 1 ||
                                  (physicsMode >= 2 && linkedBone.physicsDisabled == 0)) &
                                 linkedBone.hasRigidBody) == 0)
                                std::memcpy(linkedBone.matExtra, linkedBone.matInit,
                                            sizeof(linkedBone.matExtra));
                        }
                    }
                    // chain root world recompute (0x496748)
                    {
                        Bone& root = ikRoot;
                        WorldFromParent(root, Traits::Parent(boneRecords, root.parent), &d3);
                        if (((physicsMode == 1 || (physicsMode >= 2 && root.physicsDisabled == 0)) &
                             root.hasRigidBody) == 0)
                            std::memcpy(root.matExtra, root.matInit, sizeof(root.matExtra));
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
    static void BoneTransform_PostIkPass(const Model* model, D3& d3, Bone* boneRecords,
                                         int boneCount, unsigned char afterPhysics, int layer,
                                         int physicsMode) {
        for (int i = 0; i < boneCount; ++i) {
            Bone& bone = boneRecords[Traits::Index(*model, i)];
            if (!ChannelPass(bone, afterPhysics))
                continue;
            if (bone.layer != layer || bone.type == cpu_bone::BoneType::UnderIk ||
                bone.hasFlag == 0)
                continue;
            if (bone.type == cpu_bone::BoneType::CoRotate) { // 0x4968F3
                // x64 twin of the phase-B block (acosf/sqrtf/sinf/cosf).
                const Bone& tail = boneRecords[bone.tailBone];
                const float* sourceQuat =
                    tail.type == cpu_bone::BoneType::UnderIk ? tail.rotQuat2 : tail.physicsQuat;
                float sq[4] = {sourceQuat[0], sourceQuat[1], sourceQuat[2], sourceQuat[3]};
                if (sq[3] <= 1.0f) {
                    if (sq[3] < -1.0f)
                        sq[3] = -1.0f;
                } else {
                    sq[3] = 1.0f;
                }
                float angle = std::acos(sq[3]);
                if (sq[3] < 0.0f)
                    angle -= kPiF;
                const float len = std::sqrt(sq[0] * sq[0] + sq[1] * sq[1] + sq[2] * sq[2]);
                if (angle == 0.0f || len < kEps) {
                    bone.physicsQuat[0] = bone.physicsQuat[1] = bone.physicsQuat[2] = 0.0f;
                    bone.physicsQuat[3] = 1.0f;
                } else {
                    const float scaled = (static_cast<float>(bone.tailIdx) * angle) / 100.0f;
                    const float s = std::sin(scaled) / len;
                    bone.physicsQuat[0] = s * sq[0];
                    bone.physicsQuat[1] = s * sq[1];
                    bone.physicsQuat[2] = s * sq[2];
                    bone.physicsQuat[3] = std::cos(scaled);
                }
            }
            float q[4] = {bone.physicsQuat[0], bone.physicsQuat[1], bone.physicsQuat[2],
                          bone.physicsQuat[3]};
            if (bone.type == cpu_bone::BoneType::RotateGrant) { // 0x496B53
                const Bone& src = boneRecords[bone.tailIdx];
                const float* sourceQuat =
                    src.type == cpu_bone::BoneType::UnderIk ? src.rotQuat2 : src.physicsQuat;
                float sq[4] = {sourceQuat[0], sourceQuat[1], sourceQuat[2], sourceQuat[3]};
                float out[4];
                QuatMul(out, sq, q);
                q[0] = out[0];
                q[1] = out[1];
                q[2] = out[2];
                q[3] = out[3];
            }
            if (bone.flags & cpu_bone::kBoneFlagRotInherit)
                InheritRotQuat(q, boneRecords, bone.tailIdx, bone.inheritRatio);
            MatRotQuat(reinterpret_cast<Matrix*>(bone.matLocal), q);
            float inhX = 0.0f, inhY = 0.0f, inhZ = 0.0f;
            if (bone.flags & cpu_bone::kBoneFlagTransInherit) {
                const Bone& src = boneRecords[bone.tailIdx];
                const float r = bone.inheritRatio;
                inhX = src.physicsOffset[0] * r;
                inhY = src.physicsOffset[1] * r;
                inhZ = src.physicsOffset[2] * r;
                Matrix tmp, cur;
                d3.translation(&tmp, inhX, inhY, inhZ);
                std::memcpy(&cur, bone.matLocal, sizeof(cur));
                d3.mul(reinterpret_cast<Matrix*>(bone.matLocal), &cur, &tmp);
            }
            Matrix tmp, cur;
            d3.translation(&tmp, -bone.position[0], -bone.position[1], -bone.position[2]);
            std::memcpy(&cur, bone.matLocal, sizeof(cur));
            d3.mul(&cur, &tmp, &cur);
            d3.translation(&tmp, bone.physicsOffset[0], bone.physicsOffset[1],
                           bone.physicsOffset[2]);
            d3.mul(reinterpret_cast<Matrix*>(bone.matLocal), &cur, &tmp);
            std::memcpy(&cur, bone.matLocal, sizeof(cur));
            d3.translation(&tmp, bone.position[0], bone.position[1], bone.position[2]);
            d3.mul(reinterpret_cast<Matrix*>(bone.matLocal), &cur, &tmp);
            if (bone.flags & cpu_bone::kBoneFlagTransInherit) {
                bone.physicsOffset[0] += inhX;
                bone.physicsOffset[1] += inhY;
                bone.physicsOffset[2] += inhZ;
            }
            WorldFromParent(bone, Traits::Parent(boneRecords, bone.parent), &d3);
            if (CopyGate(bone, physicsMode)) // 0x49706F
                std::memcpy(bone.matExtra, bone.matInit, sizeof(bone.matExtra));
        }
    }

    static void Run(Model* m, unsigned char afterPhysics, int layer, Model* const* modelSlots,
                    int physicsMode) { // 0x493A60
        D3 d3;
        Model& model = *m;
        const int boneCount = static_cast<int>(model.boneCount);
        Bone* boneRecords = model.boneTable;
        IkChain* iks = m->ikChains;
        const int ikCount = static_cast<int>(model.ikChainCount);
        const bool pmx2 = model.physicsMode == 2;
        Binding* extTable = m->boneOrderTable;

        // five sequential passes at the original phase boundaries (header)
        BoneTransform_PhysicsSweptLocals(m, d3, boneRecords, boneCount, afterPhysics,
                                         layer); // A 0x493A85
        BoneTransform_StandardLocals(m, d3, boneRecords, boneCount, afterPhysics,
                                     layer); // B 0x493E71
        BoneTransform_WorldPass(m, d3, boneRecords, boneCount, extTable, modelSlots, afterPhysics,
                                layer,
                                physicsMode); // C 0x49455F
        BoneTransform_CcdIk(d3, m, boneRecords, iks, ikCount, afterPhysics, layer, physicsMode,
                            pmx2); // D 0x494A56
        BoneTransform_PostIkPass(m, d3, boneRecords, boneCount, afterPhysics, layer,
                                 physicsMode); // E 0x496881
    }
};
}
