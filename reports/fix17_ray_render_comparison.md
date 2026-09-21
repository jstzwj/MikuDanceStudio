# Ray scene comparison, 2026-09-22

## Confirmed and corrected: world-axis vertex data

`MakeLineGeometry` did not match MMD 9.32 in two places:

| Field | Previous port | MMD / corrected port |
| --- | --- | --- |
| Blue axis ARGB | `0xFF000001` (almost black) | `0xFF0000FF` |
| Green axis endpoint | `(0, 0.1, 65)` | `(0, 65, 0)` |

Evidence from the user's original x64 executable, IDA image base
`0x7FF7CB420000`: `0x7FF7CB42F4C8` and `0x7FF7CB42F4F4` write the blue
color; `0x7FF7CB42F57C`, `0x7FF7CB42F584`, and `0x7FF7CB42F590` write
the green endpoint. The x86 `sub_40AF40` agrees.

These explain the nearly black blue axis and absent vertical green axis.
They do **not** establish the cause of the red axis's discontinuity.
No depth-test workaround or changes to Ray's shaders were made.

## Live comparison of the two running applications

Read-only `ReadProcessMemory` inspection used the `ExpGetPmdNum` export's
RIP-relative load to locate each application's own global app pointer.
Neither process was patched, suspended, injected into, or closed.

Both applications loaded the same paths:

- `Downloads/sour miku/miku/miku.pmx` (`Sour_Miku_White`, 25 materials).
- `Documents/github/ray-mmd/ray_controller.pmx`.
- `Documents/github/ray-mmd/Skybox/Helipad GoldenHour/Sky with box.pmx`.

The Miku model has 139,692 working vertices in both processes. Position,
normal, UV, bone indices/weights and material-index fields compared equal
for every vertex. This compares CPU working data, not GPU vertex-buffer
contents. Both texture caches contain all 16 referenced texture paths with
non-null texture objects, including `tex/hair5.png` and `spa/hair.png`.
The material records and texture/sphere/toon morph multiplier/addend values
for body, face and hair agree (neutral tints: multiplier 1, addend 0).
The PMX is UTF-16, and all referenced texture files exist.

Both live applications had edit mode 0, playback inactive, self-shadow
mode 1, and self-shadow enabled. With the recovered frame gate this means
the screenshot's editing state uses the **fixed-function host renderer**.
The next draw-state comparison must include `ConfigureMaterialStages`
and MME's no-assigned-effect fallback, not only `ConfigureEffectMaterial`.
Embedded host shader resources 117 and 118 also hash identically between
the two executable files, but that alone does not validate the live draw.

The local diagnostic output is in ignored `build-x64/live-material-comparison.json`
and the `compare_live_*.py` scratch scripts. The scripts contain addresses
of this particular running session and are not reusable regression tests.

## Verification and remaining work

- x64 Release compilation/link succeeded to `build-x64/ray-render-review/`.
  The normal output EXE was locked by the user's running application;
  the existing process and scene were retained.
- Existing x64 CTest suite: **21/21 passed**, including Ray GPU tests.
- `git diff --check` passed.

The native computer-use pipe was unavailable, so there is no new GUI A/B
capture. Existing Ray tests do not compare the complete Sour Miku scene
against MMD. The washed-out hair/face, the red-line discontinuity, and the
sky/background alignment remain unresolved; this change must not be
reported as full Ray rendering parity. The original Main mapping for Miku
also still needs confirmation before treating the screenshots as an
identical-effect-assignment comparison.
