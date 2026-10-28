# Built-in self-shadow validation

The three review units are attachment/dependency validation, native generated
passes, and the PMD/PMX CPU producer. Real-model image acceptance now uses the
bounded numerical policy below. Exact DX9 comparison remains available as a
diagnostic and still reports the original differences; reference images and
rendering output are unchanged.

## Numerical acceptance policy

The policy applies only to the new real-model built-in self-shadow gate:

| Output | Allowed error |
| --- | --- |
| Main RGB | Absolute difference <= 1 byte per channel, per pixel |
| Main alpha | Exact bytes |
| R32 shadow depth | Finite values; absolute difference <= 1e-6 per texel |
| Main coverage | Exact per-pixel mask: RGB differs from the white clear color |
| Shadow coverage | Exact per-texel mask: depth < the clear value of 1 |
| Image format and dimensions | Exact |

No averaging or percentage of failed pixels can hide an outlier. Coverage
masks are compared at each position, not just by total pixel count. Both masks
describe this test's white/depth=1 clears; they are not general visibility
buffers. The RGB limit covers a single UNORM rounding step. The depth bound
covers the measured SM3/SM5 float differences while remaining below the mode-1
shadow threshold (0.3/1500). Mode 2 still requires its own image check because
its threshold varies with Y. These bounds do not establish universal model or
hardware equivalence.

Generated-scene images, repeat submissions and CPU/lifetime contracts keep
their existing exact checks. The inherited `rhi_d3d9_null_texture_warp` test is
unchanged. WARP material differences above these bounds remain failures when
run with numerical acceptance enabled.

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
- `self_shadow_compare`: CPU rejection cases for excessive RGB/depth error,
  any alpha change, changed raster coverage, nonfinite depth and mismatched
  image shapes. A one-byte RGB or one-ULP depth change can pass bounded
  acceptance while still failing exact comparison.
- `self_shadow_dx9_tolerant_{pmd,pmx}_{dx11,dx12}`: real-model hardware gates
  using the policy above; skipped only when the corresponding external path is
  absent.
- `self_shadow_dx9_strict_pmd` / `pmx`: opt-in diagnostics registered with
  `MIKUDANCESTUDIO_SELF_SHADOW_STRICT_DIAGNOSTICS=ON`. Every differing RGBA byte
  or R32 float bit fails. Direct executable `strict` mode is always available.

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
suite passed 16/16 on each architecture, and the standalone portable resource
suite passed 15/15 before the numerical-policy change. These counts exclude
the explicitly failing strict external image diagnostics described below.

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

After the policy change, all four real-model hardware gates (PMD/PMX on
DX11/DX12) passed on x64 with external paths configured. The CPU comparison
policy passed on x64, x86 and the standalone portable build. An additional
x86 PMD/DX12 hardware run passed bounded acceptance. Exact PMD/DX11 hardware
comparison still failed, and PMX/DX11 WARP still failed bounded acceptance;
both rejection paths were rerun explicitly.

The strict WARP diagnostics were actually run with both external paths
configured and failed; they are not marked as expected failures. Hardware
measurements also fail exact comparison because their shadow images differ.
These historical exact results remain valid after introducing bounded
acceptance. The PMX WARP maximum of 12 bytes exceeds the new one-byte RGB
limit and is still unresolved. The inherited null-texture issue stays separate.

## Reproduction and next gate

Build `runtime_self_shadow_test`, then run ordinary producer acceptance:

```powershell
ctest --test-dir build-x64/build -C Release -R '^self_shadow_producer_' --output-on-failure
```

For a real model, use the direct executable with a backend, comparison mode,
and an optional local evidence directory:

```powershell
build-x64/build/Release/runtime_self_shadow_test.exe C:/models/model.pmx res/assets dx12-hardware tolerant out/shadow-evidence
```

Evidence contains comparison CSV, RGBA previews, and raw float shadow images.
`measure` validates producer/lifecycle invariants and reports numerical errors;
`tolerant` additionally enforces every limit above; `strict` requires zero
differences in both images. The CSV retains exact error counts and adds counts
outside tolerance and differing coverage pixels. No output is quantized or
altered for comparison.

Configure `MDS_SELF_SHADOW_PMD` and `MDS_SELF_SHADOW_PMX`, then run
`ctest -R '^self_shadow_dx9_tolerant_'` for hardware acceptance. The optional
strict CTest diagnostics compare DX11 WARP against hardware DX9 as before.

Next, route the explicit final frame target through presentation, then add
dynamic screen textures and post-processing. WARP material precision and exact
SM3/SM5 differences can be investigated independently of bounded acceptance.
The [Vulkan offscreen adapter](RHI_VULKAN_OFFSCREEN.md) now reuses the same
shadow/main tests, PMD/PMX CPU producer, pass validation and numerical limits.
Generated models and the real PMD pass bounded acceptance. On TITAN Xp the real
PMX shadow map is exact, but its main image has texture-sampling differences
up to 15 RGB bytes and one differing coverage pixel. The external PMX bounded
gate remains a failure; no expected-failure flag or wider tolerance is applied.
This is independent of the existing WARP issue and does not establish Vulkan
presentation or full model parity.
