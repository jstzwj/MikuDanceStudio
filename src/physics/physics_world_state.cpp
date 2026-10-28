#include "mikudancestudio/physics_world_state.hpp"
#include "mikudancestudio/physics_world.hpp"

#include "btBulletDynamicsCommon.h"
#include "BulletCollision/BroadphaseCollision/btAxisSweep3.h"

namespace mikudancestudio {
namespace {

template <typename T> void DeleteOwned(T*& value) {
    delete value;
    value = nullptr;
}

} // namespace

void CreatePhysicsWorld(PhysicsWorldState& state) {
    state.collisionConfig = new btDefaultCollisionConfiguration();
    state.dispatcher = new btCollisionDispatcher(state.collisionConfig);
    state.broadphase = new bt32BitAxisSweep3(btVector3(-10000.0f, -10000.0f, -10000.0f),
                                             btVector3(10000.0f, 10000.0f, 10000.0f), 1500000);
    state.solver = new btSequentialImpulseConstraintSolver();
    state.world =
        new PhysicsWorld(state.dispatcher, state.broadphase, state.solver, state.collisionConfig);
    auto* world = state.world;

    world->getSolverInfo().m_solverMode &= ~1;
    // Match the x64 reference gravity and leave Bullet's initial local time intact.
    world->setGravity(btVector3(0.0f, -98.0f, 0.0f));

    auto* plane = new btStaticPlaneShape(btVector3(0.0f, 1.0f, 0.0f), 0.0f);
    auto* motionState =
        new btDefaultMotionState(btTransform::getIdentity(), btTransform::getIdentity());
    btVector3 zeroInertia(0.0f, 0.0f, 0.0f);
    btRigidBody::btRigidBodyConstructionInfo ci(0.0f, motionState, plane, zeroInertia);
    state.groundBody = new btRigidBody(ci);
    world->addRigidBody(state.groundBody, static_cast<short>(0x8000), static_cast<short>(-1));
    state.groundBody->setRestitution(0.88f);
}

// Reference: MMD x64 sub_7FF7CB422580. Keep reverse traversal and
// destruction order while using Bullet interfaces rather than byte offsets.
void DestroyPhysicsWorld(PhysicsWorldState& state) {
    if (state.world != nullptr) {
        for (int i = state.world->getNumConstraints() - 1; i >= 0; --i) {
            btTypedConstraint* constraint = state.world->getConstraint(i);
            state.world->removeConstraint(constraint);
            delete constraint;
        }
        for (int i = state.world->getNumCollisionObjects() - 1; i >= 0; --i) {
            btCollisionObject* object = state.world->getCollisionObjectArray()[i];
            if (btRigidBody* body = btRigidBody::upcast(object)) {
                delete body->getMotionState();
                delete body->getCollisionShape();
            }
            state.world->removeCollisionObject(object);
            delete object;
        }
    }
    state.groundBody = nullptr;
    DeleteOwned(state.world);
    DeleteOwned(state.solver);
    DeleteOwned(state.broadphase);
    DeleteOwned(state.dispatcher);
    DeleteOwned(state.collisionConfig);
}

} // namespace mikudancestudio
