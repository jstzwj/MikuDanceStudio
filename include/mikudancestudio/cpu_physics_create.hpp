#pragma once
#include <cstdint>
#include "btBulletDynamicsCommon.h"
#include "BulletDynamics/ConstraintSolver/btGeneric6DofSpringConstraint.h"
namespace mikudancestudio::cpu_physics {
template <class Scene, class Result, class Ops> struct Creation {
    static void CreateRigidBody(Scene* scene, Result& out, int shape, float sx, float sy, float sz,
                                const float* mat16, int mode, float mass, float dampLin,
                                float dampAng, float restitution, float friction, char grp,
                                std::uint16_t mask) {
        btDiscreteDynamicsWorld* world = scene->world;
        btCollisionShape* colShape = nullptr;
        if (shape == 0) {
            colShape = new btSphereShape(sx); // 0x406531
        } else if (shape == 1) {
            colShape = new btBoxShape(btVector3(sx, sy, sz)); // 0x40657F
        } else if (shape == 2) {
            colShape = new btCapsuleShape(sx, sy); // 0x4065CF
        } else {
            out.id = -1; // 0x4068A4
            out.body = nullptr;
            return;
        }

        // 0x406607..0x40676D: start transform = matrix translation + basis
        // rebuilt through the D3DX quaternion round-trip (sub_401000 =
        // btMatrix3x3::setRotation).
        btTransform startTrans;
        startTrans.setIdentity();
        startTrans.setOrigin(btVector3(mat16[12], mat16[13], mat16[14]));
        float q[4];
        Ops{}.QuatFromMatrix(q, mat16);
        Ops{}.SetRotation(reinterpret_cast<float*>(&startTrans.getBasis()), q);

        // 0x406777: mass zero for kinematic bodies; inertia from the shape
        // (virtual calculateLocalInertia, vtable slot 7).
        const float useMass = mode ? mass : 0.0f;
        btVector3 inertia(0.0f, 0.0f, 0.0f);
        colShape->calculateLocalInertia(useMass, inertia);

        btDefaultMotionState* motionState = new btDefaultMotionState(startTrans); // 0x4067B5
        btRigidBody::btRigidBodyConstructionInfo ci(                              // 0x4067EF
            useMass, motionState, colShape, inertia);
        btRigidBody* body = new btRigidBody(ci); // 0x406803

        world->addRigidBody(body, 1 << grp, mask); // 0x406857
        body->setDamping(dampLin, dampAng);        // 0x40686B
        body->setRestitution(restitution);         // 0x406875
        body->setFriction(friction);               // 0x40687E
        if (mode == 0)
            body->setCollisionFlags( // 0x406886
                body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        body->setActivationState(DISABLE_DEACTIVATION); // 0x406891

        Ops{}.DumpRigid(shape, mode, mat16, q, body);

        out.id = body->m_debugBodyId; // 0x406899
        out.body = body;
    }
    static int CreatePhysJoint(Scene* scene, btRigidBody* rbA, btRigidBody* rbB, float ax, float ay,
                               float az, float aq0, float aq1, float aq2, float aq3, float bx,
                               float by, float bz, float bq0, float bq1, float bq2, float bq3,
                               float linUpper0, float linUpper1, float linUpper2, float linLower0,
                               float linLower1, float linLower2, float angUpper0, float angUpper1,
                               float angUpper2, float angLower0, float angLower1, float angLower2,
                               float spring0, float spring1, float spring2, float spring3,
                               float spring4, float spring5) {
        btDiscreteDynamicsWorld* world = scene->world;
        btTransform frameA, frameB;
        frameA.setIdentity();
        frameA.setOrigin(btVector3(ax, ay, az));
        const float qa[4] = {aq0, aq1, aq2, aq3};
        Ops{}.SetRotation(reinterpret_cast<float*>(&frameA.getBasis()), qa);
        frameB.setIdentity();
        frameB.setOrigin(btVector3(bx, by, bz));
        const float qb[4] = {bq0, bq1, bq2, bq3};
        Ops{}.SetRotation(reinterpret_cast<float*>(&frameB.getBasis()), qb);

        Ops{}.DumpJointFrames(frameA, frameB);
        auto* con = new btGeneric6DofSpringConstraint( // 0x406048
            *rbA, *rbB, frameA, frameB, true);

        con->setLinearLowerLimit( // 0x4062xx
            btVector3(linLower0, linLower1, linLower2));
        con->setLinearUpperLimit(btVector3(linUpper0, linUpper1, linUpper2));
        // VC9's __CIfmod path in sub_401260/sub_4012E0 canonicalizes a signed
        // zero result to +0.  The modern fmodf inline preserves -0, which is
        // visible in the rotational motor limit bits.
        const auto canonicalZero = [](float value) { return value == 0.0f ? 0.0f : value; };
        con->setAngularLowerLimit( // sub_401260
            btVector3(canonicalZero(angLower0), canonicalZero(angLower1),
                      canonicalZero(angLower2)));
        con->setAngularUpperLimit( // sub_4012E0
            btVector3(canonicalZero(angUpper0), canonicalZero(angUpper1),
                      canonicalZero(angUpper2)));

        world->addConstraint(con, false); // 0x4064xx

        if (spring0 > 0.0f || spring1 > 0.0f || spring2 > 0.0f || spring3 > 0.0f ||
            spring4 > 0.0f || spring5 > 0.0f) {
            const float springs[6] = {spring0, spring1, spring2, spring3, spring4, spring5};
            for (int axis = 0; axis < 6; ++axis) {
                if (springs[axis] > 0.0f) {
                    con->enableSpring(axis, true);          // sub_5008D0
                    con->setStiffness(axis, springs[axis]); // sub_500910
                }
            }
            con->setEquilibriumPoint(); // sub_500930
        }

        int& counter = scene->constraintId; // 0x4064xx
        // Stock Bullet 2.75 exposes the solver UID through its user constraint ID.
        // Keep the application's monotonically increasing ID through that API.
        con->setUserConstraintId(counter);
        Ops{}.DumpConstraint(counter, con);
        return counter++;
    }
};
}
