#include "btBulletDynamicsCommon.h"
#include "BulletDynamics/ConstraintSolver/btGeneric6DofSpringConstraint.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
std::uint32_t Bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void PrintVector(const btVector3& value) {
    std::printf(" %08X %08X %08X", Bits(value.x()), Bits(value.y()), Bits(value.z()));
}

struct SpringProbe : btGeneric6DofSpringConstraint {
    SpringProbe(btRigidBody& a, btRigidBody& b)
        : btGeneric6DofSpringConstraint(a, b, btTransform::getIdentity(),
                                       btTransform::getIdentity(), true) {}

    bool CheckReferenceMotors() {
        // Captured by calling the unmodified x64 MMD spring refresh, RVA
        // 0x111500, with these inputs. Reference EXE SHA-256:
        // 07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1.
        const float equilibrium[] = {0.13f, -0.27f, 0.73f, -0.37f, 0.63f, -0.87f};
        const float stiffness[] = {2.3f, 4.7f, 8.9f, 16.1f, 32.3f, 64.7f};
        const float damping[] = {0.51f, 0.23f, 0.77f, 0.61f, 0.39f, 0.83f};
        for (int axis = 0; axis < 6; ++axis) {
            enableSpring(axis, true);
            m_equilibriumPoint[axis] = equilibrium[axis];
            setStiffness(axis, stiffness[axis]);
            setDamping(axis, damping[axis]);
        }
        m_calculatedLinearDiff.setValue(0.7f, -1.3f, 2.1f);
        m_calculatedAxisAngleDiff.setValue(-0.6f, 0.9f, -1.7f);

        std::printf("expected_vel vel uid=%d ba=%d bb=%d Al=", getUid(),
                    m_rbA.m_debugBodyId, m_rbB.m_debugBodyId);
        const btVector3* velocities[] = {&m_rbA.getLinearVelocity(),
            &m_rbA.getAngularVelocity(), &m_rbB.getLinearVelocity(),
            &m_rbB.getAngularVelocity()};
        const char* separators[] = {"", " Aa=", " Bl=", " Bb="};
        for (int v = 0; v < 4; ++v) {
            const btVector3& value = *velocities[v];
            std::printf("%s%08X %08X %08X", separators[v], Bits(value.x()),
                        Bits(value.y()), Bits(value.z()));
        }
        std::printf("\n");

        btConstraintInfo2 info{};
        info.fps = 59.94f;
        info.m_numIterations = 7;
        internalUpdateSprings(&info);
        const std::uint32_t reference[6][2] = {
            {0x40B734EF, 0x3CB32CA4}, {0xC1188BD7, 0x3DA567A5},
            {0x42A0C960, 0x3E504D5A}, {0x419ABC80, 0x3D7D0B67},
            {0xC1E8FDBE, 0x3E14FCC4}, {0x43BED4C3, 0x3F655A87},
        };
        for (int axis = 0; axis < 6; ++axis) {
            const float velocity = axis < 3 ? m_linearLimits.m_targetVelocity[axis]
                : m_angularLimits[axis - 3].m_targetVelocity;
            const float force = axis < 3 ? m_linearLimits.m_maxMotorForce[axis]
                : m_angularLimits[axis - 3].m_maxMotorForce;
            std::printf("motor %d %08X %08X\n", axis, Bits(velocity), Bits(force));
#if defined(_M_X64)
            if (Bits(velocity) != reference[axis][0] || Bits(force) != reference[axis][1]) {
                std::fprintf(stderr, "Spring axis %d differs from x64 MMD\n", axis);
                return false;
            }
#endif
        }
        return true;
    }
};

void PrintBody(btRigidBody& body) {
    const btTransform& transform = body.getCenterOfMassTransform();
    for (int row = 0; row < 3; ++row) PrintVector(transform.getBasis()[row]);
    PrintVector(transform.getOrigin());
    PrintVector(body.getLinearVelocity());
    PrintVector(body.getAngularVelocity());
    PrintVector(body.getTotalForce());
    PrintVector(body.getTotalTorque());
}
}

int main() {
    btDefaultCollisionConfiguration config;
    btCollisionDispatcher dispatcher(&config);
    btDbvtBroadphase broadphase;
    btSequentialImpulseConstraintSolver solver;
    btDiscreteDynamicsWorld world(&dispatcher, &broadphase, &solver, &config);
    world.setGravity(btVector3(0, -9.8f, 0));
    world.getSolverInfo().m_numIterations = 7;

    btSphereShape sphere(0.75f);
    btBoxShape ground(btVector3(10, 0.5f, 10));
    btVector3 inertia;
    sphere.calculateLocalInertia(1, inertia);
    btRigidBody a(btRigidBody::btRigidBodyConstructionInfo(1, nullptr, &sphere, inertia));
    btRigidBody b(btRigidBody::btRigidBodyConstructionInfo(1, nullptr, &sphere, inertia));
    btRigidBody floor(btRigidBody::btRigidBodyConstructionInfo(0, nullptr, &ground));
    a.m_debugBodyId = 1234567;
    b.m_debugBodyId = 2345678;
    floor.m_debugBodyId = 3456789;
    a.setLinearVelocity(btVector3(-0.25f, 0.5f, -0.75f));
    a.setAngularVelocity(btVector3(0.125f, -0.375f, 0.625f));
    b.setLinearVelocity(btVector3(0.875f, -1.125f, 1.375f));
    b.setAngularVelocity(btVector3(-1.625f, 1.875f, -2.125f));
    {
        SpringProbe probe(a, b);
        probe.setUid(4321);
        if (!probe.CheckReferenceMotors()) return 1;
    }

    btTransform transform = btTransform::getIdentity();
    transform.setOrigin(btVector3(-0.6f, 0.7f, 0)); a.setCenterOfMassTransform(transform);
    transform.setOrigin(btVector3(0.6f, 0.7f, 0)); b.setCenterOfMassTransform(transform);
    transform.setOrigin(btVector3(0, -0.5f, 0)); floor.setCenterOfMassTransform(transform);
    btGeneric6DofSpringConstraint spring(a, b, btTransform::getIdentity(),
                                        btTransform::getIdentity(), true);
    spring.setUid(4322);
    spring.setLinearLowerLimit(btVector3(-2, -2, -2));
    spring.setLinearUpperLimit(btVector3(2, 2, 2));
    spring.setEquilibriumPoint();
    for (int axis = 0; axis < 6; ++axis) {
        spring.enableSpring(axis, true);
        spring.setStiffness(axis, 3.7f + axis);
        spring.setDamping(axis, 0.4f);
    }
    world.addRigidBody(&floor);
    world.addRigidBody(&a);
    world.addRigidBody(&b);
    world.addConstraint(&spring);
    for (int frame = 0; frame < 32; ++frame) {
        world.stepSimulation(1.0f / 60.0f, 0);
        std::printf("state %d", frame);
        PrintBody(a); PrintBody(b); PrintBody(floor);
        std::printf("\n");
    }
    world.removeConstraint(&spring);
    world.removeRigidBody(&b);
    world.removeRigidBody(&a);
    world.removeRigidBody(&floor);
}
