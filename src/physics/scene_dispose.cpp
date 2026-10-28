#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/physics_world_state.hpp"

namespace mikudancestudio {
namespace {
template <typename T> void ReleaseOwned(T*& value) {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}
} // namespace

void DisposePhysicsWorld(PhysicsScene* scene) {
    PhysicsWorldState physics;
    physics.collisionConfig = scene->collisionConfig;
    physics.dispatcher = scene->dispatcher;
    physics.broadphase = scene->broadphase;
    physics.solver = scene->solver;
    physics.world = scene->world;
    physics.groundBody = scene->groundBody;
    DestroyPhysicsWorld(physics);
    scene->collisionConfig = physics.collisionConfig;
    scene->dispatcher = physics.dispatcher;
    scene->broadphase = physics.broadphase;
    scene->solver = physics.solver;
    scene->world = physics.world;
    scene->groundBody = physics.groundBody;

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

} // namespace mikudancestudio
