# Built-in self-shadow validation

The three review units are attachment/dependency validation, native generated
passes, and the PMD/PMX CPU producer. The first two acceptance gates pass. The
third unit has working production APIs and offscreen acceptance coverage, but
its strict DX9 image gate remains **open**. No tolerance or reference baseline
has been changed to make it pass.

## Producer and rendering scope

`RuntimeCpuSceneProducer::BuildFrame` returns an ordered R32 float shadow-color
pass with its own depth attachment, followed by the RGBA8/D24S8 main pass. The
shadow distance and mode default to the evaluated PMM track; bare models can
choose modes 1 and 2 explicitly. The shadow projection is a shared CPU function
also used by the native DX9 renderer, retaining D3DX arithmetic on Windows.
Toon fallback colors use the same exact native literals as DX9 initialization.

Casters use native slot order independently of display/physics order. The
producer preserves the alpha=.980000019 caster exclusion, PMX cast/receive
flags, per-material culling, and PMX uniform outline colors. Cleared maps with
no casters remain valid inputs. Material/base/sphere sampling, sphere addition,
Toon/specular colors, both shadow comparisons and alpha testing are explicit
shader inputs. Geometry versions are shared between caster and material passes.

Unicode PMX imports also exposed a null renderer dereference in Shift-JIS
fallback. CPU import now owns the same four locale candidates as renderer
initialization, without changing the process locale or creating a graphics
device. Existing renderer conversion behavior is preserved.

This milestone does not implement native MME execution, simultaneous MRT,
explicit-pass HUD, UV1/material morph shading, or accessory self-shadow
composition. Those inputs are rejected where they would require an unsupported
producer. The existing preview host still uses the single-target producer;
`BuildFrame` is exercised directly through `SceneRenderer` and does not change
the editor's rendering backend.

## Offscreen tests

- `rhi_render_pass`, `rhi_frame_passes`: invalid dependencies, feedback,
  discarded/uninitialized reads, attachment sizes and stale resource lifetime.
- `rhi_multipass_*`: exact generated lit/shadow pixels; 100 resident submissions;
  resize, unload, device replacement and isolated DX12 WARP removal/recovery.
- `runtime_cpu_bindings`: PMX cast/receive/outline flag combinations, empty
  caster passes, alpha exclusion, disabled shadow mode, atomic rejection and
  unsupported accessory composition, alongside existing scene ordering tests.
- `self_shadow_producer_*`: generated PMD/PMX files, base/sphere/combined/additive
  materials, both modes, resized main targets, nonempty shadow/color coverage,
  exact repeat submissions, shared geometry, DX12 debug errors and unload.
  DX9 differences are measured and printed; this is a producer/lifecycle gate,
  **not** a claim that its DX9 comparisons pass.
- `self_shadow_dx9_strict_pmd` / `pmx`: optional real-model gates, skipped only
  when the corresponding external path is absent. Every differing RGBA byte or
  R32 float bit fails the gate.

DX11/DX12 tests create no HWND or swapchain. The independent DX9 oracle uses a
message-only `HWND_MESSAGE` window, hardware vertex processing (matching the
native initialization's first successful path), offscreen render surfaces and
the shipped `skin_effect_sm3.fx`, compiled by D3DX for SM3. It never shows a
window or calls Present. No editor, focus change, desktop capture/input or
minimize/restore operation participates.

The oracle consumes the same solved CPU snapshot and declared pipeline state.
It checks GPU/effect behavior, not independently captured original-MMD CPU
inputs or complete PMM accessory/background/MME scenes. Real model bytes remain
external and are not redistributed in this repository.

## Local real-model measurements (2026-10-28)

Both x64 and x86 Release applications build. The final selected offscreen/CPU
suite passes 16/16 on each architecture, and the standalone portable resource
suite passes 15/15. These passing suites exclude the explicitly failing strict
external image gates described below.

Main targets are 128x96 and 160x96; shadow targets are 128x128. The same 128x96
frame is repeated after resizing. Measurements below are from x64 at the rest
pose; they do not imply coverage of every animation, material or GPU.

| External model / backend | Main RGBA differences | Shadow differences, mode 1 / 2 | Strict result |
| --- | --- | --- | --- |
| Miku_Hatsune.pmd / DX11 hardware | 0 bytes, both modes/sizes | 5 / 2 texels; max 2.98023e-8 | Fail |
| Miku_Hatsune.pmd / DX12 hardware | 0 bytes, both modes/sizes | 5 / 2 texels; max 2.98023e-8 | Fail |
| luo.pmx / DX11 hardware | 0 bytes, both modes/sizes | 1 / 4 texels; max 5.96046e-8 | Fail |
| luo.pmx / DX12 hardware | 0 bytes, both modes/sizes | 1 / 4 texels; max 5.96046e-8 | Fail |
| Miku_Hatsune.pmd / DX11 WARP | up to 34 channels; max 1 byte | nonzero, max 1.19209e-7 | Fail |
| luo.pmx / DX11 WARP | up to 572 channels; max 12 bytes | nonzero, max 8.9407e-8 | Fail |

The strict WARP gates were actually run with both external paths configured and
failed; they are not marked as expected failures. Hardware measurements also
fail when checked strictly because their shadow images differ. The inherited
`rhi_d3d9_null_texture_warp` failure remains a separate investigation with its
original comparison standard unchanged. These new differences must not be
folded into that older issue or hidden by widening tolerances.

## Reproduction and next gate

Build `runtime_self_shadow_test`, then run ordinary producer acceptance:

```powershell
ctest --test-dir build-x64/build -C Release -R '^self_shadow_producer_' --output-on-failure
```

For a real model, use the direct executable with a backend, `strict` or
`measure`, and an optional local evidence directory:

```powershell
build-x64/build/Release/runtime_self_shadow_test.exe C:/models/model.pmx res/assets dx12-hardware strict out/shadow-evidence
```

Evidence contains comparison CSV, RGBA previews, and raw float shadow images.
`measure` retains differences in the report while validating producer/lifecycle
invariants; `strict` additionally requires zero differences in both images.
For the named CTest strict gates, configure `MDS_SELF_SHADOW_PMD` and
`MDS_SELF_SHADOW_PMX`; these currently compare DX11 WARP against hardware DX9.

The next acceptance gate is to isolate SM3/SM5 vertex, interpolation, reciprocal
and sampling arithmetic with small frozen cases, close R32 bit differences on
hardware, then address WARP material precision separately. After that, route an
explicit final frame target through presentation, followed by dynamic screen
textures and post-processing. Vulkan should consume these same access/pass and
producer contracts; it must not invent another MMD shading or CPU scene path.
