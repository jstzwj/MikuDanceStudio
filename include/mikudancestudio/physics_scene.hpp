// ===========================================================================
// MikuDanceStudio - the physics scene wrapper ("0x48 object", 0x048)
// ===========================================================================
// Original form (recovered from the x86 AND x64 binaries):
//   InitMainWindowAndD3D performs
//       scene = operator new(SIZE);  // x86 0x48 @ 0x47A727 / x64 0x90
//       memset(scene, 0, SIZE);
//   and stores the pointer at app+650672 (x86) / app+654776 (x64);
//   SceneConstruct (x86 0x4032B0 / x64 0x1400027B0) fills it in during
//   WM_CREATE, right after the D3D bootstrap succeeds.
//
// Layout evidence (x64 0x14000CD11..0x14000CDBD):
//   owner qword @0; 10 gizmo VB/IB slots @+8..+0x50 stride 8; collision
//   config @0x58, dispatcher @0x60, broadphase @0x68, constraint-id counter
//   as the only DWORD @0x70, solver @0x78, world @0x80, ground @0x88.
// The x86 twin packs the same members at 4-byte stride from +4 with the
// counter at 56.  A naturally aligned struct reproduces BOTH layouts with
// no hand padding - offsets pinned by static_assert below.
// ===========================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include <d3d9.h>

class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class bt32BitAxisSweep3;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;
class btRigidBody;

namespace mikudancestudio {

class D3DRenderer;

class PhysicsScene {
public:
    D3DRenderer* owner;              // 0 / 0     set by SceneConstruct

    // Gizmo geometry for the bone/selection manipulators (FVF 0x42,
    // XYZ | DIFFUSE, D3DUSAGE_WRITEONLY, D3DPOOL_DEFAULT; INDEX16).
    IDirect3DVertexBuffer9* gizmoSphereVB;     // 4 / 8
    IDirect3DIndexBuffer9* gizmoSphereIB;      // 8 / 0x10
    IDirect3DVertexBuffer9* gizmoCubeVB;       // 12 / 0x18
    IDirect3DIndexBuffer9* gizmoCubeIB;        // 16 / 0x20
    IDirect3DVertexBuffer9* gizmoSphere33VB;   // 20 / 0x28
    IDirect3DIndexBuffer9* gizmoSphere33IB;    // 24 / 0x30
    IDirect3DVertexBuffer9* gizmoArrowVB;      // 28 / 0x38
    IDirect3DIndexBuffer9* gizmoIdentityIB;    // 32 / 0x40
    IDirect3DVertexBuffer9* gizmoBoxSelVB;     // 36 / 0x48
    IDirect3DIndexBuffer9* gizmoBoxSelIB;      // 40 / 0x50

    // Bullet world stack (allocation sizes are the x86 originals).
    btDefaultCollisionConfiguration* collisionConfig;        // 44 / 0x58
    btCollisionDispatcher* dispatcher;                       // 48 / 0x60
    bt32BitAxisSweep3* broadphase;                           // 52 / 0x68
    std::int32_t constraintId;        // 56 / 0x70  starts 0, bumped per joint
    btSequentialImpulseConstraintSolver* solver;             // 60 / 0x78
    btDiscreteDynamicsWorld* world;                          // 64 / 0x80
    btRigidBody* groundBody;                                 // 68 / 0x88
    // static plane (0,1,0,0), group 0x8000 / mask -1, restitution 0.88
};

static_assert(sizeof(PhysicsScene) == 0x48 || sizeof(PhysicsScene) == 0x90,
              "size must be the x86 or x64 original allocation size");

#define MIKUDANCESTUDIO_SCENE_OFF(f, x86off, x64off)                              \
    static_assert(offsetof(PhysicsScene, f) ==                            \
                      (sizeof(void*) == 8 ? (x64off) : (x86off)),         \
                  #f " offset must match the original binary")

MIKUDANCESTUDIO_SCENE_OFF(owner, 0, 0);
MIKUDANCESTUDIO_SCENE_OFF(gizmoSphereVB, 4, 8);
MIKUDANCESTUDIO_SCENE_OFF(gizmoSphereIB, 8, 16);
MIKUDANCESTUDIO_SCENE_OFF(gizmoCubeVB, 12, 24);
MIKUDANCESTUDIO_SCENE_OFF(gizmoCubeIB, 16, 32);
MIKUDANCESTUDIO_SCENE_OFF(gizmoSphere33VB, 20, 40);
MIKUDANCESTUDIO_SCENE_OFF(gizmoSphere33IB, 24, 48);
MIKUDANCESTUDIO_SCENE_OFF(gizmoArrowVB, 28, 56);
MIKUDANCESTUDIO_SCENE_OFF(gizmoIdentityIB, 32, 64);
MIKUDANCESTUDIO_SCENE_OFF(gizmoBoxSelVB, 36, 72);
MIKUDANCESTUDIO_SCENE_OFF(gizmoBoxSelIB, 40, 80);
MIKUDANCESTUDIO_SCENE_OFF(collisionConfig, 44, 88);
MIKUDANCESTUDIO_SCENE_OFF(dispatcher, 48, 96);
MIKUDANCESTUDIO_SCENE_OFF(broadphase, 52, 104);
MIKUDANCESTUDIO_SCENE_OFF(constraintId, 56, 112);
MIKUDANCESTUDIO_SCENE_OFF(solver, 60, 120);
MIKUDANCESTUDIO_SCENE_OFF(world, 64, 128);
MIKUDANCESTUDIO_SCENE_OFF(groundBody, 68, 136);

#undef MIKUDANCESTUDIO_SCENE_OFF

void DisposePhysicsWorld(PhysicsScene* scene);

}  // namespace mikudancestudio
