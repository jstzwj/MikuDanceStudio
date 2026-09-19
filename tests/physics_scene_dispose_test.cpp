#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/physics_world.hpp"
#include "btBulletDynamicsCommon.h"
#include "BulletCollision/BroadphaseCollision/btAxisSweep3.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
std::vector<std::string> events;
void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
struct Shape : btSphereShape {
    std::string name;
    explicit Shape(const char* n) : btSphereShape(1), name(n) {}
    ~Shape() override { events.push_back(name + ":shape"); }
};
struct Motion : btDefaultMotionState {
    std::string name;
    explicit Motion(const char* n) : name(n) {}
    ~Motion() override { events.push_back(name + ":motion"); }
};
struct Body : btRigidBody {
    std::string name;
    explicit Body(const char* n)
        : btRigidBody(btRigidBodyConstructionInfo(0, new Motion(n), new Shape(n))), name(n) {}
    ~Body() override { events.push_back(name + ":body"); }
};
struct Constraint : btPoint2PointConstraint {
    explicit Constraint(btRigidBody& body) : btPoint2PointConstraint(body, btVector3(0, 0, 0)) {}
    ~Constraint() override { events.push_back("constraint"); }
};
}

int main() {
    using namespace mikudancestudio;
    PhysicsScene scene{};
    DisposePhysicsWorld(&scene);  // partially initialized startup cleanup
    scene.collisionConfig = new btDefaultCollisionConfiguration;
    scene.dispatcher = new btCollisionDispatcher(scene.collisionConfig);
    scene.broadphase = new bt32BitAxisSweep3(btVector3(-100, -100, -100), btVector3(100, 100, 100));
    scene.solver = new btSequentialImpulseConstraintSolver;
    scene.world = new PhysicsWorld(scene.dispatcher, scene.broadphase, scene.solver, scene.collisionConfig);
    auto* first = new Body("first");
    auto* last = new Body("last");
    scene.world->addRigidBody(first);
    scene.world->addRigidBody(last);
    scene.groundBody = first;
    scene.world->addConstraint(new Constraint(*first));

#ifdef MIKUDANCESTUDIO_DIAG
    auto* world = static_cast<PhysicsWorld*>(scene.world);
    world->stepSimulation(btScalar(1) / 120, 10, btScalar(1) / 60);
    Require(world->SubstepRemainder() > 0, "substep remainder must be observable");
    world->ClearSubstepRemainder();
    Require(world->SubstepRemainder() == 0, "typed diagnostic reset failed");
#endif

    DisposePhysicsWorld(&scene);
    const std::vector<std::string> expected{
        "constraint", "last:motion", "last:shape", "last:body",
        "first:motion", "first:shape", "first:body"};
    Require(events == expected, "Bullet destruction order or ownership changed");
    Require(!scene.world && !scene.solver && !scene.broadphase && !scene.dispatcher &&
            !scene.collisionConfig && !scene.groundBody, "disposed scene retains owned pointers");
    DisposePhysicsWorld(&scene);
    Require(events == expected, "second disposal deleted resources twice");
    std::puts("physics scene cleanup: order, ownership, empty and repeated cleanup passed");
}
