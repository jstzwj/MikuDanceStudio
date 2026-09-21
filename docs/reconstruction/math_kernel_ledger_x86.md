# Historical math kernel ledger x86

Archived on 2026-09-22 from `src/math/math_kernel_ledger.cpp`. The original analysis date is not recorded.

These are historical **32-bit x86** reconstruction notes. Their absolute addresses, object offsets, source line numbers, phase labels and coverage verdicts describe the earlier analysis, not a current x64 equivalence proof. The original binary hash was not recorded here. Claims such as "fully covered", "verbatim", and "still to be ported" below are retained as historical quotations and require fresh verification against the current source and target binary. No executable code was present in the source translation unit.

## Archived notes

```text
===========================================================================
Math-helper adjudication ledger for the 0x401xxx / 0x402xxx "kernel gap"
VAs (original MikuMikuDance v932, statically linked with bullet-2.75).
===========================================================================
Every VA assigned to this file was adjudicated against its xrefs in the
original IDB.  NONE of them required a ported body:

  * All the 0x401xxx/0x402xxx math helpers below are bullet-2.75 inline /
    header functions (or the compiler's lazy static-initializer thunks for
    them) that the VC9 linker emitted near their first user OUTSIDE the
    0x4C6000..0x50A000 bullet block.  The port links the real bullet-2.75,
    and the ported app callers of these exact call sites already invoke
    the genuine bullet API, so duplicating them here would create a second
    conflicting implementation of code the port already provides.
  * The five render helpers (0x401A20/0x401470/0x4015A0/0x4016F0/
    0x401B60) are MMD app code but were already ported verbatim into
    src/render/debug_geometry.cpp before this file existed.

Verified with IDA session c6cbbce9 (xrefs_to / decompile) and the
bullet-2.75 sources at repo root.  Detailed evidence per VA below.
===========================================================================

---------------------------------------------------------------------------
VA 0x00401190 - btTransform::setIdentity() (thiscall(this: float[16]))
---------------------------------------------------------------------------
Writes the 64-byte btTransform identity: basis = 3x3 identity rows
(this[0..11]) and origin = (0,0,0,0) (this[12..15], including w = 0 - NOT
a D3DX 4x4 identity, whose [15] would be 1).
bullet-2.75 provided (callers: 0x405F0A inside sub_4032B0 only).
The ported call site initializes the ground body's motion-state transform
with btTransform::getIdentity() instead:
src/physics/scene_create.cpp:157.
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x00401230 - btDefaultCollisionConfiguration::ctor (thiscall(this))
---------------------------------------------------------------------------
Initializes 8 DWORDs: 0,0,0, 4096, 4096, 0, 0, 1 - the two 4096s are
bullet's defaultMaxPersistentManifoldPoolSize /
defaultMaxCollisionAlgorithmPoolSize.
bullet-2.75 provided (callers: 0x405D0B inside sub_4032B0 only).
Ported call site: src/physics/scene_create.cpp:125
(`new btDefaultCollisionConfiguration()`).
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x00401260 - btGeneric6DofConstraint::setAngularLowerLimit (inline)
---------------------------------------------------------------------------
For each of the 3 axes: angle = btNormalizeAngle(src[axis]) stored into
this + 960 + 56*axis (m_angularLimits[axis].m_loLimit).  The wrap is
bullet's btNormalizeAngle (btScalar.h:465): fmod(x, 2*pi); <-pi -> +2pi;
>pi -> -2pi.  Note the VC9 __CIfmod path canonicalizes a signed-zero
fmod result to +0.
bullet-2.75 provided (callers: 0x4062D7 inside sub_406010 only).
Ported call site: src/physics/physics_create.cpp:515
(con->setAngularLowerLimit + canonicalZero workaround).
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x004012E0 - btGeneric6DofConstraint::setAngularUpperLimit (inline)
---------------------------------------------------------------------------
Identical to 0x401260 but writes this + 964 + 56*axis
(m_angularLimits[axis].m_hiLimit).
bullet-2.75 provided (callers: 0x4062FE inside sub_406010 only).
Ported call site: src/physics/physics_create.cpp:518
(con->setAngularUpperLimit).
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x00401470 - DrawSphere: scaled gizmo-sphere debug draw (thiscall)
---------------------------------------------------------------------------
D3DXMatrixScaling(s) -> *a3 -> GetTransform(D3DTS_WORLD) -> SetTransform,
SetRenderState(66), SetStreamSource(this[1]), SetIndices(this[2]),
DrawIndexedPrimitive(LINELIST, 0,0,58,0,120), restore world.
Already ported: src/render/debug_geometry.cpp:81 (DrawSphere), called from
the ports of its original callers 0x406950 (DrawPhysicsCollisionDebug) and
0x420F30 (DrawAccessoryDebug).
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x004015A0 - DrawBox: scaled gizmo-cube debug draw (thiscall)
---------------------------------------------------------------------------
D3DXMatrixScaling(2x, 2y, 2z) -> ... DrawIndexedPrimitive(LINELIST,
0,0,8,0,12).
Already ported: src/render/debug_geometry.cpp:100 (DrawBox); original
callers 0x406950 / 0x420F30 (see above).
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x004016F0 - DrawCapsule: three-piece capsule debug draw (thiscall)
---------------------------------------------------------------------------
Two hemispheres: Scaling(-r,-r,-r) * Translation(0,-h,0) / Scaling(r,r,r)
* Translation(0,+h,0), each DrawIndexedPrimitive(LINELIST,0,0,33,0,64);
middle: Scaling(r, 2h, r), DrawIndexedPrimitive(LINELIST,0,0,16,0,8).
Already ported: src/render/debug_geometry.cpp:121 (DrawCapsule).
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x00401A20 - SetDebugColor: gizmo vertex-buffer diffuse rewrite
---------------------------------------------------------------------------
Locks the 4 debug vertex buffers at this[1]/this[3]/this[5]/this[7]
(lengths 928/128/528/256), writes the ARGB color dword built from
(a2|0xFFFFFF00)<<8 | a3 <<8 | a4 into every +12 slot of the 16-byte
XYZ|DIFFUSE stride, unlocks.
Already ported: src/render/debug_geometry.cpp:55 (SetDebugColor).
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x00401B60 - DrawSelectionBox (thiscall)
---------------------------------------------------------------------------
SetRenderState(66), SetStreamSource(this[9]), SetIndices(this[10]),
DrawIndexedPrimitive(TRIANGLELIST, 0,0,72,0,24).
Already ported: src/render/debug_geometry.cpp:162 (DrawSelectionBox);
original caller 0x421445 inside sub_420F30.
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x00401BE0 - btMatrix3x3::getIdentity() lazy static initializer
---------------------------------------------------------------------------
MSVC lazy-init thunk: guard dword 0x5458D0, then writes the 48-byte
identity basis at 0x5458A0..0x5458CC; returns &0x5458A0.
bullet-2.75 provided (callers: 0x401C81 inside sub_401C50 only - i.e.
only btTransform::getIdentity's static initializer consumes it).
See btTransform.h:193 in the bullet-2.75 tree.
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x00401C50 - btTransform::getIdentity() lazy static initializer
---------------------------------------------------------------------------
Guard dword 0x545920; copies the 12 basis floats produced by 0x401BE0
into the 64-byte static btTransform at 0x5458E0 and zeroes its origin
(last 4 dwords); returns &0x5458E0.
bullet-2.75 provided (callers: 0x4032B0 at 0x4067C3 / 0x405F40,
0x406010 at 0x406048 / 0x40612C, 0x4064F0 at 0x4067C3 - all app-range,
but every one of those ports already uses the real
btTransform::getIdentity(): src/physics/scene_create.cpp:157-158,
src/physics/physics_create.cpp:442-455).
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x00401D20 - btBoxShape::getHalfExtentsWithMargin() (inline)
---------------------------------------------------------------------------
Copies this[8..11] (m_implicitShapeDimensions, offset 0x20) into a2[0..3],
then calls the virtual getMargin() (vtable slot +0x28) three times, adding
each x87 float result to a2[0], a2[1], a2[2] respectively (original
instruction order: x += 3rd call, y += 2nd call, z += 1st call).
bullet-2.75 provided (callers: 0x4D107D inside sub_4D1070 =
btBoxShape::calculateLocalInertia - the mass/12 box inertia formula - plus
0x406950 at 0x4069E1/0x406B19 and 0x420F30 at 0x4211DA, whose ports query
the real shape: src/render/debug_geometry.cpp:313
getHalfExtentsWithMargin()).
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x00401DA0 - btBoxShape::btBoxShape(halfExtents) (inline ctor)
---------------------------------------------------------------------------
Calls sub_4D0AB0 (btPolyhedralConvexShape ctor chain, 0x4D0AB0), stores
the btBoxShape vftable 0x529584, this[1] = 0, then m_implicitShapeDimensions
(this[8..10]) = halfExtents[0..2] * this[4..6] - margin (this[12]),
this[11] = 0.
bullet-2.75 provided (callers: 0x4065BC inside sub_4064F0 only - the app
rigid-body factory, ported at src/physics/physics_create.cpp:428
`new btBoxShape(btVector3(sx, sy, sz))`).
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x004028A0 - btDefaultMotionState::ctor (inline)
---------------------------------------------------------------------------
Stores vftable 0x5295F4; 64-byte-copies a2 into this[4..19]
(m_graphicsWorldTrans) and this[36..51] (m_startWorldTrans), a3 into
this[20..35] (m_centerOfMassOffset), nulls this[52] (m_userPointer).
bullet-2.75 provided (callers: 0x405F4D inside sub_4032B0 and 0x4067D0
inside sub_4064F0; ported at src/physics/scene_create.cpp:156-158 and
src/physics/physics_create.cpp:454-455 via `new btDefaultMotionState`).
---------------------------------------------------------------------------

---------------------------------------------------------------------------
VA 0x00402A20 - btTransform inverse -> 4x4 float matrix (inline kernel)
---------------------------------------------------------------------------
Transposes the source 4x4's rotation part into a2 (D3DX layout, zero 4th
column) and computes a2[12..14] = -R^T * t, i.e. the matrix of
transform.inverse().
bullet-2.75 provided (callers: 0x4D84D4 / 0x4D8990 in sub_4D8420,
0x4DBDE7 in sub_4DBDB0, 0x4DC54B in sub_4DC4C0, 0x4DEED9 in sub_4DED30,
0x4E0D6D / 0x4E11B1 in sub_4E0CF0, 0x523B60 / 0x5240B8 in sub_5239A0 -
all bullet GJK/EPA/collision code in 0x4C6000..0x530000 - plus 0x402B84
inside sub_402B70 = btDefaultMotionState::getWorldTransform, itself only
reachable through the vtable pointer at 0x5295F8, i.e. pure bullet glue).
---------------------------------------------------------------------------
```

