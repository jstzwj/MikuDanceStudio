# Material sampling and final frame output

## Explicit sampling profiles

`MaterialSamplingPolicy::Native` preserves the pipeline's sampler declarations.
`PortableTrilinear` explicitly uses linear minification, magnification and mip
interpolation with anisotropy 1 for active material stages. Addressing, UV
transforms, sRGB decoding, mip bytes and alpha-test/blend declarations are
preserved. Filtered alpha and therefore edge coverage can change with the chosen
filter; the profile is a rendering choice, not a numerical tolerance adjustment.

New `ScenePreviewController` instances default to the portable profile through
`BackendOptions`. They apply it to a private copy of either replay-stream or
explicit-frame pipelines. Raw devices retain their declared samplers; direct
offscreen consumers call `ApplyMaterialSampling` explicitly. The DX9 editor's
native filtering is unchanged.

The DX9 self-shadow oracle now consumes the same declared sampler profile as
the tested frame, while still executing the independent shipped SM3 effect.
Portable comparisons do **not** establish equivalence to the editor's native
anisotropic image. Native tests retain their separate names and acceptance.

## Public reproducer

`tests/material_sampling_fixture.hpp` generates all geometry, texture bytes and
explicit mip levels. No model or texture download is required. Three patterns
isolate spatial filtering, mip interpolation (solid color per level), and
alpha-tested/blended edges. The 720-case matrix covers:

- Magnification and two degrees of minification, rotated triangles and varying
  perspective W.
- No mip chain versus a complete explicit chain; wrap and clamp addressing.
- Point, linear, and requested anisotropy 1/4/16.
- Both the model material and built-in shadow composition programs.

Each case renders on hardware DX11, DX12 and Vulkan. The ordinary gate keeps
point/linear cases and explicitly converts requested anisotropic cases to the
portable profile. Cross-API acceptance is RGB <= 1 byte with exact alpha and
per-pixel coverage. Every case also checks byte-exact repeated submissions and
byte-exact GPU final-target copy/readback. No image postprocessing is used to
accept a comparison.

```powershell
cmake --build build-x64/build --config Release --target rhi_material_sampling_test
ctest --test-dir build-x64/build -C Release -R '^rhi_material_sampling$' --output-on-failure
build-x64/build/Release/rhi_material_sampling_test.exe native-measure
build-x64/build/Release/rhi_material_sampling_test.exe native-gate
```

`native-measure` reports every case and exits successfully if measurement
completed, even when image acceptance fails. `native-gate` runs the same native
samplers and returns failure on excessive differences. It is a diagnostic,
not an expected-failure CTest or a substitute for the portable gate.

On TITAN Xp / SDK 1.4.341.1, 120 of 720 native cases fail acceptance, all with
anisotropic filtering and multiple mips. Native point/linear and single-level anisotropic
sampling agree; native anisotropy with multiple mip levels diverges between
Direct3D and Vulkan. Solid-color mip levels reproduce the difference without
spatial texture details. Perspective gradients and alpha edges also diverge.
Changing Vulkan to nearest-mip anisotropy only helped some orthographic cases
and worsened the real PMX image. Derivative experiments did not resolve the
native difference. No such heuristics or shader changes are enabled.

This isolates the remaining issue to native anisotropic filtering / mip
selection; it does not prove a particular driver's internal kernel or override.
The explicit portable profile avoids depending on those implementation choices.
Real-model gates retain their original one-byte/depth bounds:

```powershell
# Optional external files; these assets are never redistributed.
$env:MDS_SELF_SHADOW_PMD = 'C:/models/model.pmd'
$env:MDS_SELF_SHADOW_PMX = 'C:/models/model.pmx'
ctest --test-dir build-x64/build -C Release -R '^self_shadow_portable_' --output-on-failure
ctest --test-dir build-x64/build -C Release -R '^self_shadow_dx9_tolerant_(pmd|pmx)_vulkan$' --output-on-failure
```

The second command preserves the native profile, including its unresolved real
PMX failure. The portable Vulkan PMX comparison is exact on this workstation
for both shadow modes and widths 128/160/128; native mode remains a different
image and is still tracked separately.

## Common final color and presentation

`RenderFrame::finalColor` selects a stored, initialized RGBA8 target with
CopySource access. Whole-frame validation rejects unknown, depth/R32,
discarded or unreadable choices. After all passes, `SceneRenderer` copies it
on the GPU to the selected output; its dimensions determine the output extent,
independently of a larger shadow map. No CPU readback or implicit resize is
part of ordinary submission. A zero selector retains the earlier offscreen
multi-target behavior; explicit-frame presentation requires a selector.

DX11, DX12 and Vulkan implement the same copy contract. The preview controller
accepts an explicit frame whose final extent matches the host's client extent.
The host rebuilds that frame on resize, retaining CPU camera/shadow decisions.
This provides the output boundary for later screen textures and postprocessing;
neither feature is implemented by this change.

Win32 Vulkan presentation uses a graphics/present queue, acquire binary
semaphores per frame, present binary semaphores per swapchain image, and timeline
tokens for resource retirement. The common RGBA8 output is blitted to an UNORM
RGBA8/BGRA8 swapchain image. FIFO and supported immediate presentation are
explicit choices. Out-of-date/suboptimal surfaces rebuild; native device loss
requires replacement. Swapchain rebuild invalidates prepared-frame epochs while
retaining immutable CPU-backed uploads. No fullscreen or MSAA substitution is
performed. Other window systems remain unsupported.

`rhi_hidden_presentation_{dx11,dx12,vulkan,vulkan_validation}` uses only an
always-hidden, nonactivating tool popup. It compares **actual swapchain-image**
readback with the same backend's offscreen image, then checks repeated resize,
resident reuse, replacement, unload and immediate present. Vulkan additionally
checks busy/unsupported/retry-later results, temporary-loss rebuild and stale
submission rejection. Validation tests count errors and fail on them; a missing
optional validation layer is an explicit skip. No ShowWindow, desktop capture,
focus/input operation or minimize/restore is used.

The independent preview host now accepts:

```powershell
mds_rhi_preview vulkan C:/models/model.pmx --sampling portable --self-shadow 1
```

This is an opt-in visible preview command for manual use. Automated acceptance
uses the offscreen and hidden tests above, never this visible mode or the older
desktop lifecycle tests. Native MME execution and complete editor scene parity
remain separate work.

## Verified on 2026-10-29

Release x64/x86 applications and independent preview hosts build successfully.
All automated runs used offscreen or always-hidden windows.

| Group | Result |
| --- | --- |
| x64 generated sampling/output, Vulkan resource/pass validation, hidden presentation, CPU bindings | 20 passed |
| x64 generated and real PMD/PMX self-shadow; portable profiles plus retained native D3D gates | 20 passed |
| x86 selected sampling/output/presentation/CPU and Vulkan PMD/PMX groups | 14 passed, 4 validation skips (32-bit layer unavailable) |
| Standalone portable CPU/resource suite | 16 passed |
| Vulkan-only offscreen build | 4 passed |
| Explicit native 720-case sampling diagnostic | Reproduces failure in 120 cases |
| Retained Vulkan native real-PMX bounded gate | Reproduces max 15 RGB bytes and one differing coverage pixel; shadow exact |

The last two rows remain failures, with no expected-failure flag or relaxed
tolerance. They are reported independently of the portable-profile gates.
