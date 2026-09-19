#include "mikudancestudio/physics_scene.hpp"

#include "btBulletDynamicsCommon.h"
#include "BulletCollision/BroadphaseCollision/btAxisSweep3.h"

namespace mikudancestudio {
namespace {
template <typename T>
void DeleteOwned(T*& value) {
    delete value;
    value = nullptr;
}

template <typename T>
void ReleaseOwned(T*& value) {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}
}  // namespace

// Reference: MMD x64 sub_7FF7CB422580. Preserve its reverse traversal and
// destruction order, using Bullet interfaces rather than x86 field offsets.
void DisposePhysicsWorld(PhysicsScene* scene) {
    btDiscreteDynamicsWorld* world = scene->world;
    if (world != nullptr) {
        for (int i = world->getNumConstraints() - 1; i >= 0; --i) {
            btTypedConstraint* constraint = world->getConstraint(i);
            world->removeConstraint(constraint);
            delete constraint;
        }
        for (int i = world->getNumCollisionObjects() - 1; i >= 0; --i) {
            btCollisionObject* object = world->getCollisionObjectArray()[i];
            if (btRigidBody* body = btRigidBody::upcast(object)) {
                delete body->getMotionState();
                delete body->getCollisionShape();
            }
            world->removeCollisionObject(object);
            delete object;
        }
    }
    scene->groundBody = nullptr;  // non-owning alias into the world
    DeleteOwned(scene->world);
    DeleteOwned(scene->solver);
    DeleteOwned(scene->broadphase);
    DeleteOwned(scene->dispatcher);
    DeleteOwned(scene->collisionConfig);

    ReleaseOwned(scene->gizmoBoxSelVB);
    ReleaseOwned(scene->gizmoSphereVB);
    ReleaseOwned(scene->gizmoCubeVB);
    ReleaseOwned(scene->gizmoSphere33VB);
    ReleaseOwned(scene->gizmoArrowVB);
    ReleaseOwned(scene->gizmoSphereIB);
    ReleaseOwned(scene->gizmoCubeIB);
    ReleaseOwned(scene->gizmoSphere33IB);
    ReleaseOwned(scene->gizmoIdentityIB);
    ReleaseOwned(scene->gizmoBoxSelIB);
}

}  // namespace mikudancestudio
