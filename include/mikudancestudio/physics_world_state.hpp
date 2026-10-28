#pragma once

class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class bt32BitAxisSweep3;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;
class btRigidBody;

namespace mikudancestudio {

// Bullet ownership independent of the legacy Win32/D3D9 scene record.
// The Windows adapter copies these fields into its ABI-pinned PhysicsScene.
struct PhysicsWorldState {
    btDefaultCollisionConfiguration* collisionConfig = nullptr;
    btCollisionDispatcher* dispatcher = nullptr;
    bt32BitAxisSweep3* broadphase = nullptr;
    btSequentialImpulseConstraintSolver* solver = nullptr;
    btDiscreteDynamicsWorld* world = nullptr;
    btRigidBody* groundBody = nullptr; // non-owning alias into world
};

// Create requires an empty state; destroy accepts empty or partially built state.
void CreatePhysicsWorld(PhysicsWorldState& state);
void DestroyPhysicsWorld(PhysicsWorldState& state);

} // namespace mikudancestudio
