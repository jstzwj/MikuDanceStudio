# Vulkan offscreen RHI

The optional Vulkan adapter consumes the existing `Device`, `RenderPassDesc`,
`RenderFrame` and resident `SceneRenderer` contracts. Its offscreen factory
creates no HWND, surface or swapchain. An independent Win32 factory now supports
presentation. The CPU scene producer remains shared with DX11/DX12, including
PMD/PMX geometry, material versions and self-shadow
matrices. Vulkan does not call DX9 to produce its input or render its output.
Only the separate comparison oracle uses hardware DX9 and a message-only HWND.

## Build and device requirements

Enable `MIKUDANCESTUDIO_RHI_VULKAN=ON`. The build requires Vulkan SDK headers,
DXC with SPIR-V support, `spirv-val` and Python. DXC compiles the existing model
and built-in shadow HLSL; every module is checked by `spirv-val` before being
embedded in the library. No shader compiler or SDK import library is required
at runtime. Vulkan entry points are loaded from the system Vulkan loader;
this also permits an x86 build without an SDK `Lib32` directory.

The selected graphics device must support Vulkan 1.3, dynamic rendering,
Synchronization2, timeline semaphores, shader demote-to-helper, FP32 signed
zero/Inf/NaN preservation and sampler anisotropy. RGBA8, R32Float and D24S8
format usage is checked before allocating images. Buffers currently require
host-visible coherent memory. Hardware requests exclude CPU devices; software
requests require a CPU Vulkan implementation and never fall back to hardware.

`CreateVulkanOffscreenDevice` provides the native factory.
`CreateOffscreenBackendDevice` selects DX11, DX12 or Vulkan through
`BackendOptions`. `BackendAvailable` indicates compiled adapter availability,
not successful runtime probing. Unsupported creation returns a reason and
native error. On Win32, `CreateBackendDevice` selects the presentation factory;
other window systems still return `presentation_not_supported`. `Dx9Measured` arithmetic and its diagnostics are
unsupported and rejected.

## Implemented behavior

- Uploaded vertex/index/uniform buffers, RGBA8/sRGB textures and explicit mip
  contents. Automatic mip generation is rejected.
- One color attachment (RGBA8 or R32Float), optional D24S8, sampled attachment
  dependencies, explicit load/store operations and copy-source readback.
- PositionColor / PositionNormalUv model programs, indexed/non-indexed and line
  draws, raster/depth/stencil/blend state, built-in shadow generation,
  composition and outlines. Binding layouts separate VS/PS constants and use
  explicit image/sampler arrays; inactive descriptors use a valid white image.
- Shared pass validation rejects read-before-write, attachment feedback,
  uninitialized loads, nesting and undeclared sampling before native recording.
  Synchronization2 image barriers cover attachment writes, fragment reads and
  staging copies; readback includes transfer-to-host visibility.
- Three reusable frame command/descriptor/uniform allocations, bounded draw
  counts and timeline completion tokens. Frames wait before reusing their
  allocations; destruction waits for queued uses. Transfer timeouts retain
  queued staging allocations and require device replacement.
- Resize and logical reset invalidate initialized attachment contents and
  recorded submission epochs while retaining immutable uploads. Resident
  resources rebuild from CPU descriptions on replacement. Native device loss
  requires a new device; forced native Vulkan removal has not been tested.

Upload and readback currently wait for completion. GPU frames are queued with
timeline completion, but asynchronous streaming/copy queues are future work.
D24S8 sampling/readback, MRT, explicit attachment mips/layers/MSAA, fixed HUD
pipelines, UV1, material morph shading, compute and MME remain unsupported. Win32 WSI uses the common final RGBA8 output;
see [sampling and final output](RHI_MATERIAL_SAMPLING.md) for its contracts and
always-hidden acceptance tests.

## Offscreen acceptance (2026-10-29)

Measured with SDK 1.4.341.1 and NVIDIA TITAN Xp on Windows, Release builds.
`rhi_vulkan_resources` covers uploads, minified mip selection, sRGB decoding,
both vertex layouts, indexed material drawing,
readback, stale/cross-device handles, invalid dependencies, discard and reset.
`rhi_multipass_vulkan_*` reuses the generated shadow/main scene, exact pixel
colors, 100 resident submissions, resize, unload, stale frames and replacement.
`self_shadow_producer_vulkan_*` reuses all five generated PMD/PMX fixtures and
the independent stock DX9 effect oracle. Repeated frames remain byte-exact.

Validation variants enable both Khronos core and synchronization validation.
Missing validation layers/extensions produce an explicit CTest skip (77);
rendering cases still run without that optional layer. This workstation has
an x64 validation layer and no x86 validation layer. Validation errors are
counted and cause test failure, never a skip.

The x64 Vulkan cases and DX11/DX12 offscreen regression group passed 13/13.
The x86 Vulkan group passed three rendering cases and explicitly skipped three
validation cases because the 32-bit layer is unavailable. The Vulkan-only x64
resource/multipass build passed 4/4. Portable resource tests passed 16/16.

The real-model gates retain RGB <= 1 byte, exact alpha/coverage, and R32 absolute
error <= 1e-6. Configure `MDS_SELF_SHADOW_PMD` / `MDS_SELF_SHADOW_PMX` to run
the external assets without adding them to the repository.

| External model, x64 | Main image | Shadow map | Bounded result |
| --- | --- | --- | --- |
| Miku_Hatsune.pmd | Within limits | Within limits | Pass |
| luo.pmx, mode 1 | 474 differing channels, maximum 15 bytes; 269 outside tolerance; one coverage pixel | Exact | Fail |
| luo.pmx, mode 2 | 471 differing channels, maximum 15 bytes; 267 outside tolerance; one coverage pixel | Exact | Fail |

These PMX counts repeat at widths 128/160/128 and height 96. Removing material
textures in an isolation experiment made the main image exact; removing
specular did not improve it. The earlier linear-filter experiment changed only the new backend and thus
compared different sampler contracts. The explicit portable profile now applies
matching trilinear samplers to the new backend and the DX9 oracle; it produces
exact PMX comparisons on this workstation. The DX9 editor and native profile
retain their original filtering. Derivative and nearest-mip heuristics are not
enabled; the shared shaders are preserved. The remaining
material-sampling parity issue is not attributed to a particular driver
algorithm without further evidence. The PMX gate stays enabled and failing
when its asset is supplied; it is not marked as an expected failure.

Run only these offscreen groups while working at the desktop:

```powershell
cmake --build build-x64/build --config Release --target rhi_vulkan_test rhi_multipass_gpu_test runtime_self_shadow_test
ctest --test-dir build-x64/build -C Release -R '^(rhi_vulkan_|rhi_multipass_vulkan_|self_shadow_producer_vulkan)' --output-on-failure
ctest --test-dir build-x64/build -C Release -R '^self_shadow_dx9_tolerant_(pmd|pmx)_vulkan$' --output-on-failure
```

A Vulkan-only build is also supported with `MIKUDANCESTUDIO_FORMATS_ONLY=ON`,
all three `MIKUDANCESTUDIO_RHI_D3D*` options OFF and Vulkan ON. Its resource and
generated multipass tests require neither Conan/Bullet nor a Direct3D adapter.
Win32 presentation now consumes the explicit final target. Dynamic screen
textures, post-processing and native anisotropic parity remain future work.
The new portable material gates and native diagnostics are documented in
[RHI_MATERIAL_SAMPLING.md](RHI_MATERIAL_SAMPLING.md).
