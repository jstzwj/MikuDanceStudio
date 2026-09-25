// ===========================================================================
// VA 0x004032B0 - SceneConstruct  (original: sub_4032B0, 0x2D5A bytes)
// ===========================================================================
// Builds the physics scene wrapper stored at app+650672 (restored as
// mikudancestudio::PhysicsScene in include/mikudancestudio/physics_scene.hpp; the original
// field offsets are pinned there by static_assert on both architectures):
//   10 gizmo vertex/index buffers, FVF 0x42 (XYZ | DIFFUSE, 16 B stride),
//   D3DUSAGE_WRITEONLY, D3DPOOL_MANAGED, INDEX16, then the Bullet world
//   stack and the static ground btRigidBody: plane (0,1,0,0) + default
//   motion state, added with group 0x8000 / mask -1, restitution 0.88.
//
// World tuning copied from the original tail (0x405E6B..0x405FEA):
//   * world+160 &= ~1 - that dword is btDynamicsWorld::m_solverInfo
//     .m_solverMode (verified: ctor 0x4F14F0 writes the 68-byte solver info
//     at +88 with m_solverMode=260 at +160), i.e. clear SOLVER_RANDMIZE_ORDER
//     - a defensive no-op on the stock default 0x104.
//   * setGravity(0, -98, 0).
//   * sub_403030 is the stock Bullet 2.75 construction-info constructor:
//     damping 0/0, friction 0.5, restitution 0, sleeping 0.8/1.0,
//     additional damping disabled with the stock 0.005/0.01 factors.
//
// Allocation note: the original allocates the broadphase and the ground body
// through btAlignedAlloc + placement construction and every other Bullet
// object through operator new; the port uses C++ new throughout (deleting
// dtors are identical, docs/ARCHITECTURE.md §8).
//
// Called once from UI creation (0x466D78, right after the D3D bootstrap
// 0x408020 succeeds).  Any buffer creation or lock failure returns false,
// which fails WM_CREATE and terminates the process like the original.
// =========================================================================//

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstring>

#include "btBulletDynamicsCommon.h"
#include "BulletCollision/BroadphaseCollision/btAxisSweep3.h"  // bt32BitAxisSweep3

#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/physics_world.hpp"

#include "scene_gizmo_data.inc"

namespace mikudancestudio {
namespace {

// One gizmo vertex buffer: create, lock(0, len), copy, unlock.
bool PutVB(IDirect3DDevice9* dev, IDirect3DVertexBuffer9** slot,
           unsigned len, const void* data) {
    IDirect3DVertexBuffer9* vb = nullptr;
    if (FAILED(dev->CreateVertexBuffer(
            len, 0x8 /*D3DUSAGE_WRITEONLY*/, 0x42 /*XYZ|DIFFUSE*/,
            D3DPOOL_MANAGED /*1*/, &vb, nullptr)))
        return false;
    *slot = vb;
    void* p = nullptr;
    if (FAILED(vb->Lock(0, len, &p, 0)))
        return false;
    std::memcpy(p, data, len);
    vb->Unlock();
    return true;
}

// One gizmo index buffer.  The original locks with size 0 (= whole buffer).
bool PutIB(IDirect3DDevice9* dev, IDirect3DIndexBuffer9** slot,
           unsigned len, const void* data) {
    IDirect3DIndexBuffer9* ib = nullptr;
    if (FAILED(dev->CreateIndexBuffer(
            len, 0, D3DFMT_INDEX16 /*101*/, D3DPOOL_MANAGED /*1*/, &ib,
            nullptr)))
        return false;
    *slot = ib;
    void* p = nullptr;
    if (FAILED(ib->Lock(0, 0, &p, 0)))
        return false;
    std::memcpy(p, data, len);
    ib->Unlock();
    return true;
}

}  // namespace

bool SceneConstruct(PhysicsScene* scene, D3DRenderer* d3dSub) {
    scene->owner = d3dSub;                                   // 0x4032F3
    IDirect3DDevice9* dev = d3dSub->device;                  // +120032

    // ---- gizmo buffers ----------------------------------------------------
    if (!PutVB(dev, &scene->gizmoSphereVB, 928, kGizmoSphere58))    // 0x403308
        return false;
    if (!PutIB(dev, &scene->gizmoSphereIB, 480, kGizmoSphereIdx))   // 0x403BD7
        return false;
    if (!PutVB(dev, &scene->gizmoCubeVB, 128, kGizmoCube8))         // 0x4047C0
        return false;
    if (!PutIB(dev, &scene->gizmoCubeIB, 48, kGizmoCubeIdx))        // 0x4048E3
        return false;
    if (!PutVB(dev, &scene->gizmoSphere33VB, 528, kGizmoSphere33))  // 0x404A22
        return false;
    if (!PutIB(dev, &scene->gizmoSphere33IB, 256, kGizmoSphere33Idx))  // 0x404F0E
        return false;
    if (!PutVB(dev, &scene->gizmoArrowVB, 256, kGizmoArrow16))      // 0x405A22
        return false;
    if (!PutIB(dev, &scene->gizmoIdentityIB, 32, kGizmoIdentityIdx))  // 0x4057DD
        return false;
    if (!PutVB(dev, &scene->gizmoBoxSelVB, 256, kGizmoBoxSel16))    // 0x405526
        return false;
    if (!PutIB(dev, &scene->gizmoBoxSelIB, 144, kGizmoBoxSelIdx))   // 0x405A0D
        return false;

    // ---- Bullet world stack -------------------------------------------------
    scene->collisionConfig = new btDefaultCollisionConfiguration();  // 0x405D0B (0x58)
    scene->dispatcher =
        new btCollisionDispatcher(scene->collisionConfig);           // 0x405D4F (0x12F0)
    scene->broadphase = new bt32BitAxisSweep3(                       // 0x405DE3
        btVector3(-10000.0f, -10000.0f, -10000.0f),
        btVector3(10000.0f, 10000.0f, 10000.0f), 1500000);
    scene->solver = new btSequentialImpulseConstraintSolver();       // 0x405E18
    scene->world = new PhysicsWorld(                                // 0x405E5D (0x110)
        scene->dispatcher, scene->broadphase, scene->solver,
        scene->collisionConfig);
    btDiscreteDynamicsWorld* world = scene->world;

    world->getSolverInfo().m_solverMode &= ~1;             // 0x405E6B
    // m_localTime keeps the constructor's 1/60 (orig ctor 0x4FAC30 stores
    // flt_52EA00 there and nothing zeroes it); the load-pass main step
    // consumes it for exactly one substep (orig maindt probe: first call
    // runs with m_localTime=1/60, dt=0).
    // x64 original 0x7FF7CB4257CF loads dword_7FF7CB552CA8 =
    // 0xC2C40000 = -98.0f exactly (x and z come from the zeroed xmm6).
    // The x86 build's 0x405EAB instead stores C2C3FFFF, one ULP below;
    // this port follows the x64 baseline, which is the behavioral
    // reference for both build flavors here.
    world->setGravity(btVector3(
        0.0f, -98.0f, 0.0f));                              // x64 0x7FF7CB4257CF (x86 0x405EAB)

    // ---- static ground body -------------------------------------------------
    auto* plane = new btStaticPlaneShape(                  // 0x405EF8 (0x60)
        btVector3(0.0f, 1.0f, 0.0f), 0.0f);
    btDefaultMotionState* motionState =                    // 0x405F4D (0xE0)
        new btDefaultMotionState(btTransform::getIdentity(),
                                 btTransform::getIdentity());
    btVector3 zeroInertia(0.0f, 0.0f, 0.0f);
    btRigidBody::btRigidBodyConstructionInfo ci(           // 0x405F6D
        0.0f, motionState, plane, zeroInertia);

    if (scene->groundBody != nullptr) {                    // 0x405F77
        delete scene->groundBody;
        scene->groundBody = nullptr;
    }
    scene->groundBody = new btRigidBody(ci);               // 0x405FB5
    world->addRigidBody(scene->groundBody,                 // 0x405FDF
                        static_cast<short>(0x8000), static_cast<short>(-1));
    scene->groundBody->setRestitution(0.88f);              // 0x405FEA
    return true;
}

}  // namespace mikudancestudio
