#include "btBulletDynamicsCommon.h"
#include "BulletDynamics/ConstraintSolver/btGeneric6DofSpringConstraint.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::uint64_t Simulate() {
    btDefaultCollisionConfiguration configuration;
    // Stock 2.75's dispatcher has over-aligned members, but no class allocator.
    // Exercise the C++17 allocation/deletion boundary used by the application.
    auto dispatcher = std::make_unique<btCollisionDispatcher>(&configuration);
    Require(reinterpret_cast<std::uintptr_t>(dispatcher.get()) % alignof(btCollisionDispatcher) ==
                0,
            "dispatcher allocation is misaligned");
    btDbvtBroadphase broadphase;
    btSequentialImpulseConstraintSolver solver;
    btDiscreteDynamicsWorld world(dispatcher.get(), &broadphase, &solver, &configuration);
    world.setGravity(btVector3(0, -9.8f, 0));
    world.getSolverInfo().m_solverMode &= ~SOLVER_RANDMIZE_ORDER;

    btSphereShape sphere(0.75f);
    btBoxShape ground(btVector3(10, 0.5f, 10));
    btVector3 inertia;
    sphere.calculateLocalInertia(1, inertia);
    btRigidBody first(btRigidBody::btRigidBodyConstructionInfo(1, nullptr, &sphere, inertia));
    btRigidBody second(btRigidBody::btRigidBodyConstructionInfo(1, nullptr, &sphere, inertia));
    btRigidBody floor(btRigidBody::btRigidBodyConstructionInfo(0, nullptr, &ground));
    btTransform transform = btTransform::getIdentity();
    transform.setOrigin(btVector3(-0.6f, 0.7f, 0));
    first.setCenterOfMassTransform(transform);
    transform.setOrigin(btVector3(0.6f, 0.7f, 0));
    second.setCenterOfMassTransform(transform);
    transform.setOrigin(btVector3(0, -0.5f, 0));
    floor.setCenterOfMassTransform(transform);

    btGeneric6DofSpringConstraint spring(first, second, btTransform::getIdentity(),
                                         btTransform::getIdentity(), true);
    spring.setUserConstraintId(4322);
    Require(spring.getUid() == 4322, "stock constraint ID API disagrees");
    spring.setLinearLowerLimit(btVector3(-2, -2, -2));
    spring.setLinearUpperLimit(btVector3(2, 2, 2));
    spring.setEquilibriumPoint();
    for (int axis = 0; axis < 6; ++axis) {
        spring.enableSpring(axis, true);
        spring.setStiffness(axis, 3.7f + axis);
        spring.setDamping(axis, 0.4f);
    }
    world.addRigidBody(&floor);
    world.addRigidBody(&first);
    world.addRigidBody(&second);
    world.addConstraint(&spring);

    std::uint64_t hash = 14695981039346656037ull;
    const auto record = [&](float value) {
        Require(std::isfinite(value), "simulation produced a nonfinite value");
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        hash ^= bits;
        hash *= 1099511628211ull;
    };
    const auto vector = [&](const btVector3& value) {
        for (int axis = 0; axis < 3; ++axis)
            record(value[axis]);
    };
    bool contacted = false;
    for (int frame = 0; frame < 240; ++frame) {
        world.stepSimulation(1.0f / 60.0f, 0);
        for (const btRigidBody* body : {&first, &second}) {
            const auto& pose = body->getWorldTransform();
            for (int row = 0; row < 3; ++row)
                vector(pose.getBasis()[row]);
            vector(pose.getOrigin());
            vector(body->getLinearVelocity());
            vector(body->getAngularVelocity());
        }
        for (int i = 0; i < dispatcher->getNumManifolds(); ++i)
            contacted |= dispatcher->getManifoldByIndexInternal(i)->getNumContacts() > 0;
        Require(floor.getWorldTransform() == transform, "static ground moved");
    }
    Require(contacted, "collision/contact path was not exercised");
    world.removeConstraint(&spring);
    world.removeRigidBody(&second);
    world.removeRigidBody(&first);
    world.removeRigidBody(&floor);
    Require(world.getNumConstraints() == 0 && world.getNumCollisionObjects() == 0,
            "simulation objects were not removed");
    return hash;
}
} // namespace

int main() {
    try {
        const auto first = Simulate();
        Require(first == Simulate(), "fresh simulation is not deterministic");
        std::puts(
            "Upstream Bullet: aligned allocation, contacts, springs, 240 frames and replay passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Bullet simulation: %s\n", error.what());
        return 1;
    }
}
