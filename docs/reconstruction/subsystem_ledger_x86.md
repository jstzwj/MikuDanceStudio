# Historical subsystem ledger x86

Archived on 2026-09-22 from `src/features/subsystem_ledger.cpp`. The original analysis date is not recorded.

These are historical **32-bit x86** reconstruction notes. Their absolute addresses, object offsets, source line numbers, phase labels and coverage verdicts describe the earlier analysis, not a current x64 equivalence proof. The original binary hash was not recorded here. Claims such as "fully covered", "verbatim", and "still to be ported" below are retained as historical quotations and require fresh verification against the current source and target binary. No executable code was present in the source translation unit.

## Archived notes

```text
===========================================================================
0x004C5xxx "remaining subsystems" - classification ledger
===========================================================================
Scope of this file: the 11 functions 0x4C5150, 0x4C5260, 0x4C5E50,
0x4C5E60, 0x4C5E70, 0x4C5E80, 0x4C5E90, 0x4C5EA0, 0x4C5EB0, 0x4C5ED0,
0x4C5EE0.  Each was decompiled from the original MikuMikuDance v932
(32-bit x86 VC9, IDA Hex-Rays) and checked against the existing MikuDanceStudio
tree.  VERDICT: every one of the 11 is already fully covered - either as a
complete port or inlined into a port of its only caller(s).  No new bodies
are needed; this file records the evidence per VA so the coverage ledger
can cite it.

Object layout shared by the whole family (the accessory object, slot array
app+0x9DD70 [255 slots]; note the axis-mesh owner object at app
kPtrSub04b0 uses the same +0/+4/+0x4A4 material-array shape):
  +0x0000  ID3DXMesh* (vtable slot 3 = DrawSubset)
  +0x0004  material records base pointer, 68-byte (17-float) stride
  +0x0214  X translation float        (this[133])
  +0x0218  Y translation float        (this[134])
  +0x021C  Z translation float        (this[135])
  +0x0220  Rx float, radians          (this[136])
  +0x0224  Ry float, radians          (this[137])
  +0x0228  Rz float, radians          (this[138])
  +0x022C  size (Si) float            (this[139])
  +0x04A0  transparency (Tr) float    (this[296])
  +0x04A4  material count dword       (this[297])

Per-function verdicts:

0x004C5150 - COVERED-INLINE (complete port under another name).
  Original: __thiscall char sub_4C5150(this = axis-mesh owner, a2 = 0x1D574
  render-wrapper sub-object).  Releases a pre-existing mesh (vtable+8),
  FindResourceA(0, 0x73, "XFILE") / SizeofResource / LoadResource /
  LockResource, D3DXLoadMeshFromXInMemory(data, size, 544 /*D3DXMESH_...*/,
  device = *(a2+120032), ..., &matBuf, ..., count = this+297, &mesh),
  MessageBoxA("failed load axis.x from memory!") on failure, then
  operator new(68 * count) into this[1], copies each 72-byte source record
  (ID3DXBuffer via vtable+3 GetBufferPointer) down to 68 bytes, releases
  the buffer, returns 1.
  Port: mikudancestudio::InitAxisMesh at
  src/render/debug_geometry.cpp:253-290 (line 253: `bool InitAxisMesh`) -
  step-for-step identical, including the 544 mesh flags, 72->68 stride
  copy, the null-new early-out, and ReleaseCom of the material buffer.
  Only caller in the original: 0x466D20 (0x46761c); it is outside this
  scope, but the port is already reachable from MMDApp.

0x004C5260 - COVERED-INLINE (inlined into both of its callers).
  Original: __thiscall sub_4C5260(this = axis-mesh owner, a2 = 0x1D574
  render-wrapper).  For i in [0, this[297] /*count at +0x4A4*/):
    device = *(a2 + 120032)
    (*(device->vt + 196))(device, this[1] + 68*i)   // SetMaterial
    (*(device->vt + 260))(device, 0, 0)             // SetTexture(0, null)
    (*(mesh->vt + 12))(mesh = *this, i)             // DrawSubset(i)
  Port: mikudancestudio::(anon)::DrawAxisMesh at
  src/render/debug_geometry.cpp:192-203, called from the two ported
  callers at src/render/debug_geometry.cpp:400 (DrawAccessoryDebug =
  original 0x420F30, call site 0x4213FF) and :486 (DrawBoneOperationAxis
  = original 0x42DB10, call site 0x42DFD9).  Field offsets (count 1188 =
  0x4A4, materials at +4, mesh at +0, 68-byte stride, SetTexture(0,
  nullptr), DrawSubset via vtable slot 3) all match.  Nothing to port.

0x004C5E50 - COVERED-INLINE.  7-byte getter: return this[133] (float at
  +0x214, accessory X translation).  Sole caller 0x42A8F0 (occupied-slot
  walk of the 255-entry accessory array, returns 0.0 when index never
  matches), which backs export ExpGetAcsX (0x4C3800).
  Port: ExpGetAcsX at src/exports/effect_api.cpp:439-442
  (`Fld<float>(acc, 0x214)`, AcsByIndex == the occupied-slot walk).

0x004C5E60 - COVERED-INLINE.  return this[134] (+0x218, Y).
  Chain 0x4C3820 -> 0x42A940 -> 0x4C5E60.  Port: ExpGetAcsY at
  src/exports/effect_api.cpp:444-447.

0x004C5E70 - COVERED-INLINE.  return this[135] (+0x21C, Z).
  Chain 0x4C3840 -> 0x42A990 -> 0x4C5E70.  Port: ExpGetAcsZ at
  src/exports/effect_api.cpp:449-452.

0x004C5E80 - COVERED-INLINE.  return this[136] (+0x220, Rx).
  Chain 0x4C3860 -> 0x42A9E0 -> 0x4C5E80.  Port: ExpGetAcsRx at
  src/exports/effect_api.cpp:454-457.

0x004C5E90 - COVERED-INLINE.  return this[137] (+0x224, Ry).
  Chain 0x4C3880 -> 0x42AA30 -> 0x4C5E90.  Port: ExpGetAcsRy at
  src/exports/effect_api.cpp:459-462.

0x004C5EA0 - COVERED-INLINE.  return this[138] (+0x228, Rz).
  Chain 0x4C38A0 -> 0x42AA80 -> 0x4C5EA0.  Port: ExpGetAcsRz at
  src/exports/effect_api.cpp:464-467.

0x004C5EB0 - COVERED-INLINE.  21-byte getter: return (float)(this[139] *
  10.0) (+0x22C, size, scaled by the double 10.0 at 0x52C170).
  Chain 0x4C38C0 -> 0x42AAD0 -> 0x4C5EB0.  Port: ExpGetAcsSi at
  src/exports/effect_api.cpp:469-474 - `static_cast<float>(
  Fld<float>(acc, 0x22C) * 10.0)`, i.e. the same double-multiply-then-
  narrow-to-float arithmetic.

0x004C5ED0 - COVERED-INLINE.  return this[296] (float at +0x4A0,
  transparency).  Chain 0x4C38E0 -> 0x42AB20 -> 0x4C5ED0.
  Port: ExpGetAcsTr at src/exports/effect_api.cpp:476-479.

0x004C5EE0 - COVERED-INLINE.  __thiscall sub_4C5EE0(this, void* out, int
  mat): if (mat < this[297]) memcpy(out, this[1] + 68*mat, 0x44) else
  zero-fill out with 68 bytes.  (Strict unsigned `<` on the bounds check.)
  Sole caller 0x42AC50, which backs export ExpGetAcsMaterial (0x4C3960).
  Port: ExpGetAcsMaterial at src/exports/effect_api.cpp:505-518 -
  memset(out, 0, 0x44) first, then the strict-< guarded 17-float copy
  from Fld<float*>(acc, 4) + 17*mat.  The wrapper's "null accessory ->
  zero-filled out" behavior is also reproduced.

Conclusion: 0 of 11 functions require a new body.  No code is emitted by
this translation unit on purpose; keeping the evidence next to the
0x4C5xxx family is the entire deliverable.
=========================================================================//

Intentionally no definitions.  See the ledger above.
namespace mikudancestudio {
All 11 in-scope VAs are covered by existing ports (see ledger above);
nothing to define.
}  // namespace mikudancestudio
```

