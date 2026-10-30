# Rendering hardware interface: incremental migration

Current status (2026-10-30): the live editor supports DX11, DX12 and Vulkan
through the **Renderer** menu. The old **Renderer Preview** menu/window has been
removed; shared controllers remain. See [editor integration](RHI_EDITOR_VIEWPORT.md),
[shared controller](RHI_PREVIEW_CONTROLLER.md), [per-frame renderer selection](RHI_FRAME_OWNERSHIP.md), and the
[current code review and priorities](RHI_REVIEW_2026-10-29.md).
Changing geometry now uses completion-protected buffer reuse; DX12 retains its
upload allocations and Vulkan retires native resources at each resource's last
use. See [dynamic buffers and resource retirement](RHI_DYNAMIC_BUFFERS.md).
Generic VS/PS programs, constant layouts and resource bindings now execute on
all three new backends; shared fullscreen postprocessing uses that path.
See [shader contracts](RHI_SHADERS.md). The first explicit material and two-pass
MME profiles also enter the editor's shared frame after whole-frame validation;
other effects retain same-frame DX9 fallback. See [portable MME](RHI_MME_PORTABLE.md).
Ordered MRT attachments and raw RGBA16F intermediate targets now execute on all
three backends. A bounded public MRT/HDR MME profile exports named targets and
ordered input/output dependencies; unsupported effects still use same-frame DX9.
See [MRT/HDR contracts and gates](RHI_MRT_HDR.md).
Numeric shared parameters and the original ordered CONTROLOBJECT resolver now
feed immutable per-draw constants. The scene-v2 profile lowers the existing SAS
command table into capture/clear/draw steps, with bounded loops and successful-frame
parameter writeback. Hidden editor tests switch live effects through DX9, DX11,
DX12 and Vulkan without unloading the effects; supported RHI frames execute zero
DX9 scene draws. This does not yet translate arbitrary FX9 shaders or execute
Ray natively on the newer backends. Expanded include sources are now retained
per loaded revision for stable device reconstruction. See
[MME parameter and script scope](RHI_MME_SEMANTICS.md).
A bounded source importer now translates unannotated FX9 model-material effects
to the shared shader contract, including actual sampler bindings and current
per-draw matrices/material values. Hidden tests retain these effects across
backend switches. See [legacy FX9 import](RHI_MME_LEGACY_IMPORT.md). Postprocess
postprocess scene effects can now import their existing SAS scripts without
proprietary annotations and share frame-local render targets with other effects.
The generic graphics profile has sixteen texture/sampler slots, based on the
native Ray inventory. See [automatic scenes and sharing](RHI_MME_SHARED_SCENES.md),
[shared resource contracts](RHI_SHARED_TARGETS.md) and
[native Ray requirements](RHI_MME_NATIVE_REQUIREMENTS.md). Bounded `DefaultEffect`
child turns, ordered nested scene carriers and explicit RGBA8 render-target mip
generation now enter that shared frame; see [subeffects and mip timing](RHI_MME_SUBEFFECTS.md).
Ordinary FX9 shaders now lower ordered MRT outputs, float scene captures and
viewport-relative intermediate targets without custom profiles; see
[FX9 MRT/HDR and relative sizing](RHI_MME_LEGACY_MRT.md) for supported geometry,
native viewport semantics, comparison gates and remaining Ray blockers.
Unmodified Ray still uses same-frame DX9 fallback. Camera/geometry matrix
variants, camera/light vectors, native draw names and once-per-frame MME clocks
now have CPU providers; see [standard parameters](RHI_MME_STANDARDS.md).
The proposed ownership, lifecycle and call sequence are specified in
`docs/RHI_INTERFACE_CONTRACT.md`; this file tracks the migration and current
implementation limits.

The CPU full-frame diagnostic assembler remains available. Its earlier
resident-window evidence is archived in [CPU frame and preview](RHI_CPU_FRAME_PREVIEW.md)
and [controller history](RHI_PREVIEW_CONTROLLER_HISTORY.md). The live editor
defaults to DX9 and can select the newer backends, subject to the compatibility
fallbacks documented above.

## Historical migration notes

The notes below describe earlier milestones and their acceptance scope;
statements of missing features apply to those milestones.

The earlier accessory depth-history and alpha-discard measurements are recorded
in [accessory depth measurement](RHI_ACCESSORY_DEPTH_MEASUREMENT.md). Later
single-sample full-scene results use explicit measured backend strategies and
DX9-compatible Mip preparation; these results do not establish MSAA, MME, PMX
or other GPU acceptance.

The current DX11/DX12 integration milestones, verified implementation status,
remaining application/pass/MME boundaries and effort assumptions are tracked in
[DX11 / DX12 integration roadmap](RHI_DX11_DX12_ROADMAP.md). The dated results
below describe earlier migration steps; later backend acceptance is recorded
in the linked DX11 documents and that roadmap.

This is a design boundary, not a renderer replacement. The existing Windows
application still renders through Direct3D 9. The portable `rhi.hpp` and
validation-only Null backend have no native window or GPU dependencies. They
establish opaque, generation-checked resource handles, explicit buffer and
texture descriptions, and frame/draw/present ordering. The Null backend does
not render pixels, compile shaders, or exercise device loss.

Unity 2021.3's `Runtime/GfxDevice/GfxDevice.h`, device types, buffer contract,
and D3D11/Metal implementation layout were inspected locally as architectural
prior art: scene/graphics data are distinct from the platform device; backend
capabilities and resource lifetimes need explicit ownership; device clients
must not silently assume a single global instance. This is not a claim to have
audited the whole Unity source tree. This repository does not import Unity
code, types, or licenses; its contract and implementations are independent.

`CaptureSceneSnapshot` currently extracts camera state and optional attachment,
model visibility, vertex/index counts, material-index spans, supported
kinematic bone matrices, and accessory matrices. Unsupported IK/physics bones
are absent, not guessed. PMD texture names are not yet normalized into
bindings. The opt-in world-parity diagnostic compares the snapshot matrices
with the legacy runtime and records material batches; it does not yet compare
shaders, draw submission order, deformed vertex bytes, or rendered pixels.
The synthetic `render_snapshot` CTest runs cross-platform. The scene CLI also
compares the playback snapshot to a same-frame seek after Save As and readback;
the raw PMM initial pose is not the same thing as seeking its keyframes.

`MIKUDANCESTUDIO_RHI_D3D9=ON` builds an isolated D3D9 device adapter and
smoke test. It accepts a caller-created native device (retained with `AddRef`),
owns its own GPU resources, and implements basic indexed triangle draws with
two fixed vertex layouts, plus an explicit untextured screen-space line
pipeline and non-indexed `LineList` draws. Opt-in borrowed-scene adapters now
draw the selection rectangle, HUD sprite, and text overlay through the live
application device. Each saves and restores external D3D9 state and calls the
existing MME draw bridge; this does not migrate MME effects into the RHI.
The adapter owns uploaded RGBA8 textures for the textured batches; persistent
caches retain them until revision replacement, reset or shutdown. The host is responsible for other reset resources; the
adapter releases its own dynamic buffers. The smoke
test skips if Direct3D device creation is unavailable (for example, a remote
desktop without a usable adapter). The independent `rhi_d3d9_parity` test
compares readback from a minimal native DX9 indexed triangle to the adapter
draw using the same device and offscreen target. It does not compare MMD's
model renderer, effects, IK, textures, or full frame. The same test also
compares the eight-vertex pattern from the selection-box line producer; the
optional capture-file argument accepts its actual dumped bytes. A reset smoke
test invalidates dynamic handles across a native DX9 reset and retains managed
buffers. The default build keeps
legacy rendering.
For an isolated Windows validation build, configure with
`cmake -S . -B build-rhi -DMIKUDANCESTUDIO_FORMATS_ONLY=ON
-DMIKUDANCESTUDIO_RHI_D3D9=ON -DBUILD_TESTING=ON`, then run
`cmake --build build-rhi --config Release` and
`ctest --test-dir build-rhi -C Release --output-on-failure`.
This initial adapter reports no advanced capabilities because shader programs,
render-target textures, application-level reset orchestration, and thread-safe command submission are
not implemented through the contract. On September 29, 2026, the x64/x86
diagnostic builds passed, but both real-PMM GUI parity attempts failed before
loading at D3D9 `CreateDevice` (`0x8876086C`); the standalone D3D9 HAL smoke
test skipped for the same environment limitation. Thus real-PMM snapshot
matrix parity had not been re-established at that time. Later on September 29,
the D3D9 device became available: the independent indexed-triangle parity
test passed in x64/x86 with 4,096 identical pixels and a non-clear red center.
The real PMM GUI diagnostics passed on x64 and x86: v1 `sample.pmm` had 31
supported bones and zero matrix mismatches on each architecture, while v2
`test_1.pmm` had eight supported bones and zero matrix mismatches on each.
These diagnostic comparisons
exclude unsupported IK/physics bones and do not submit PMM models via RHI.

On September 30, 2026, real v1 `sample.pmm` GUI tests on x64 and x86 passed
same-frame comparisons for selection, HUD sprite, and text overlay, with and
without the optional adapters. Each compared the old path with the new draw
using the application's current target and MME bridge. These narrow overlay
batches do not establish parity for model rendering, effects, or another API.
The HUD PNG now decodes into portable RGBA8 at UI load, and text atlas creation
retains its RGBA8 output. The optional DX9 RHI overlay cache uploads each atlas
once per revision and keeps separate sprite/text texture handles until device
reset or shutdown. The same-frame real PMM diagnostics compare these CPU atlas
bytes against the old DX9 textures and compare the adapter drawing against
the old batch. The image decoder and its fixture test also run on Linux.
The asset interface now resolves fixed PNG resource IDs through a Win32
callback or a file-directory source, then shares decoding, content revision,
and Null-device upload/reuse/reset tests across platforms. An optional Noto
Sans JP scene-font atlas demonstrates an OFL-licensed alternate source; it
differs from the GDI reference and does not replace default text rendering.
See `docs/RHI_INTERFACE_CONTRACT.md` for measured differences and opt-in gates.

The first independent model batch can now opt into portable Toon resolution,
BMP/PNG decode and an owned persistent stage-0 texture. File and builtin sources
share logical identity, RGBA content revision and cache lifetime. Real PMM x64/x86
same-frame and actual draw comparisons passed; the synthetic lifecycle tests ran
on Windows/Linux/macOS. See [Toon resource validation](RHI_TOON_TEXTURE_VALIDATION.md)
for source rules, device rebuilding, measured evidence and remaining limits.
Base/Sphere resources now use the same decoder/cache with explicit single-level
or generated-linear Mipmap policy. Optional three-stage owned material bindings
and typed coordinate/combiner inputs are validated independently; non-power-of-two
D3DX resizing remains blocked. See [material resource validation](RHI_MATERIAL_TEXTURE_VALIDATION.md).

## Migration order

The current DX11/DX12 resident scene implementation, native submission lifetime
and validation scope are documented in [RHI_RESIDENT_SCENE.md](RHI_RESIDENT_SCENE.md).
It shares model/HUD shaders and scene resource ownership. Live CPU model geometry
now feeds opt-in DX11/DX12 diagnostic batches; see [RHI_CPU_GEOMETRY.md](RHI_CPU_GEOMETRY.md).
CPU material/state production now covers an opt-in ordered subset; full pass
integration remains ahead. The shared measured pixel arithmetic and independent
control gates are documented in
[RHI_FINAL_MODULATE_COMPATIBILITY.md](RHI_FINAL_MODULATE_COMPATIBILITY.md).
CPU producers now cover ground lines, the ground depth plane and projected model
shadows in an audited optional prefix. Model submissions now transport explicit
Mip chains with source/recipe/content versions and an optional independent DX9
compatibility preparation service. Accessories retain captured input.
See [full Mip preparation and material scope](RHI_CPU_MIP_CONTRACT.md) and
[CPU scene pass production](RHI_CPU_SCENE_PASSES.md) for exact scope
and the atomic field/resource replacement gate.

1. Keep PMM loading, resource resolution, and session animation independent
   from rendering. Run their synthetic scene flow on all CI platforms.
2. Extend the snapshot with actual mesh/texture bindings, deform results,
   material state, and draw order; compare each with the legacy D3D9 path
   before replacing it. Leave legacy IK, physics, and MME effects untouched.
3. Extend the RHI with shader/pipeline state, render targets, samplers,
   window-backed swapchains and device-reset resource rebuilding. Grow the
   isolated D3D9 adapter under opt-in selection, then enable one draw family
   at a time only after graphical parity against the existing renderer.
4. Implement D3D11 after D3D9 parity. Introduce D3D12 only with explicit
   command submission, barriers, and descriptor lifetime management rather
   than emulating its semantics inside the D3D9 interface. Metal and Vulkan
   follow the same portable scene/render-snapshot contract independently.

Each backend needs capability negotiation, shader variants, coordinate/depth
convention tests, resize/device-loss handling, GPU resource lifetime tests,
and captured-frame comparison. MME's D3D9-specific effects are a separate
compatibility migration; no existing shader or effect is promised to work on
other backends by the current RHI skeleton.

## ????? CPU ????

????? PMD ????? CPU ?????????????????????
?????????? Mip ????????? [RHI_CPU_ACCESSORY_OUTLINE.md](RHI_CPU_ACCESSORY_OUTLINE.md)?
?? Windows `.x` ??????? D3DX SYSTEMMEM ?????? x86 ???????
x64 ? 66 ????? 4 ????????????
???????????????????
