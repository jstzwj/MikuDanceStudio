# First portable MME profiles

The editor can execute a complete supported MME frame on DX11, DX12 or Vulkan.
These are explicit opt-in profiles. A separate [bounded legacy material importer](RHI_MME_LEGACY_IMPORT.md)
also accepts supported effects without profile annotations. Other effects
continue through same-frame DX9 fallback.

## Transport and selection

The embedded MME loader retains immutable root bytes and expanded legacy/modern
include variants per loaded revision; assignment clones share them. A read-only
CPU bridge exports parameter values,
technique/pass annotations and the selected program entries. Its draw query
uses the existing `SasSelectTechnique` implementation, including actual subset,
UseTexture/UseSpheremap/UseToon and object/object_ss selection. Explicit subset
assignments override the whole-object assignment, including an empty override.

The frontend preprocesses the `MDS_RHI` shader branch, validates the legacy FX
pass state, then creates immutable generic `ShaderProgram` descriptions.
DX11/DX12 use shared HLSL; Vulkan compiles that HLSL to the generic SPIR-V ABI.
Native reflection checks the declared constants and resources. Runtime Vulkan
effects require a matching-architecture DXC DLL; absent that capability, the
editor falls back. Built-in precompiled postprocess remains available.

All assigned effects must pass profile validation. The editor compiles every
used generic program before scene submission. Unknown effects, unsupported
state or compilation failure hide the old RHI image and use this frame's DX9
state, with one animation/physics/geometry update. A failed shader is cached
for that device/program; assigning a valid effect recovers without rebuilding
the device. Resource preparation and execution still have their existing
failure fallback.

Host objects and automatic effect assignments are synchronized before frame
selection, including startup with a saved RHI backend. This synchronization
does not enter a DX9 scene or execute MME render scripts. Native DX9 retains
its existing BeginScene initialization and object synchronization order.
Eligibility examines current base assignments. Derived DX9 offscreen bindings
may remain cached after removing a scene effect; those inactive nodes do not
keep a supported frame in fallback. Active effects with offscreen child turns
remain outside the supported profiles.

## material-v1

The fixed ABI is `b0` (96 bytes shared by VS/PS): row-major float4x4
`worldViewProjection` at 0, float4 `materialDiffuse` at 64 and float4 `tint` at
80. `t0/s0` are the base texture and portable trilinear wrap sampler. POSITION
and TEXCOORD0 reference the existing 32-byte CPU vertex record. Constants are
rebuilt from the same current CPU scene snapshot used by DX9 fallback.

The legacy effect parameters use WORLDVIEWPROJECTION, geometry DIFFUSE,
VIEWPORTPIXELSIZE and MATERIALTEXTURE plus plain tint. Each object/object_ss
technique has one annotated pass. Supported state is explicitly checked:
depth test/write with LessEqual, no culling, source-alpha blending, no alpha
test/stencil/sRGB write and all color channels. The example adjusts DX9's
material pixel centers to the shared shader convention.

## post-v1

One whole-object scene carrier uses one postprocess technique with Grade and
Finish passes. Its accepted script is:

```
RenderColorTarget0=SceneColor;RenderDepthStencilTarget=Depth;
Clear=Color;Clear=Depth;ScriptExternal=Color;
RenderColorTarget0=Ping;Pass=Grade;
RenderColorTarget0=;RenderDepthStencilTarget=;Pass=Finish;
```

The frontend imports those two pass entries, live multiply/add values and the
scene-capture dependency into ordinary shared frame passes. `b0` contains two
float4 values (32 bytes); `t0/s0` sample the preceding output with point/clamp.
The resources are default-size A8R8G8B8 SceneColor/Ping and D24S8 Depth.
Grade writes a separate RGBA8 target; Finish copies it exactly. No frame
history or feedback alias is introduced. Native MME's expanded Draw=Buffer
quad and UV correction are preserved by the DX9 branch of the example.

This profile rejects multiple scene effects, offscreen-owner turns, resource
extent/format overrides, unsupported semantics/annotations, loops, MRT,
floating-point color products and depth sampling. Numeric live parameters and
CONTROLOBJECT are now accepted as described below. Combining this first scene
effect with screen.bmp history is explicitly rejected pending a capture-order
comparison. Original native anisotropic differences remain separately tracked;
these profiles use explicit trilinear or point sampling.
MME object/subset visibility overrides also retain DX9 fallback in this batch.

## mrt-hdr-v1

The CC0 `assets/effects/portable/MrtHdr.fx` adds a bounded four-pass scene profile:
capture SceneColor, Split to RGBA16F LayerA/LayerB, sample both in Combine to an
RGBA16F Hdr target, ToneMap into RGBA8 Tone, then Finish into FinalColor. All
targets use the viewport extent. The final copy is exact. No sRGB conversion,
history, independent blending or depth texture sampling is implicit.

The CPU effect description exports named target formats and ordered per-pass
`sampled`/`colors` vectors, corresponding to tN and SV_TargetN. The frontend builds
ordinary generic shader passes from this graph, including current multiply/add
parameters. The accepted script, pass order, parameter annotations, target
formats and sampler states are checked against the versioned profile; this is
not a general scene-script translator. Changes outside the profile make the
whole frame use DX9 before RHI uploads/submission.

`mme_mrt_hdr_*` executes the public FX on DX9 and each new backend using the
same input bytes. It reads LayerA/LayerB/Hdr as raw binary16 and permits at most
one binary16 ULP against DX9. Tone-mapped RGBA8 allows two channel steps for
the accumulated arithmetic; Finish remains byte-exact on each backend.
Hidden editor PMD/PMX fixtures check real effect assignment, exported graph,
live parameters, resize/recreation, zero DX9 scene draws on supported frames,
and same-frame DX9 fallback for an unsupported target format without RHI work.

## Gates

- `rhi_shader_contract`: Null binding/layout/lifetime/dependency rejection.
- `rhi_generic_shader_*`: native reflection, row-major matrices, constants,
  non-default slots, alpha, repeat and device replacement.
- `rhi_post_process_*`: exact copies, transforms, chaining, extent/orientation.
- `mme_portable_shader_*`: both public FX files against real DX9 rendering,
  including the native MME fullscreen quad. Material allows one UNORM step;
  the combined material/grade comparison allows two accumulated steps. Finish
  must equal Ping byte-for-byte on each backend, including DX9.
- `mme_portable_controls_*`: shared numeric values, CONTROLOBJECT position/world,
  reflected arrays and scalar bools; missing, mistyped or incorrectly sized
  snapshots fail before mutating the draw. The same DX9 comparison limits apply.
- `mme_scene_plan`: pure CPU SAS command lowering, per-iteration snapshots,
  delayed private writes, initialization/feedback checks and expansion bounds.
- `mme_scene_script_*`: the actual native SAS parser, step/resume executor and
  production Draw=Buffer against the lowered scene-v2 graph on each backend.
- `editor_frame_*`: generated PMD/PMX, actual MME assignments and SAS selection,
  material plus scene effect, zero DX9 scene draws on successful RHI frames,
  startup assignment discovery, live parameters, subset overrides, resize,
  repeat, device recreation, unknown-effect fallback, shader compile failure,
  once-only updates and same-device recovery. Every host stays hidden.

Local validation (2026-10-29): the complete x64 hidden/offscreen suite had 79
passes and 24 explicit external-asset skips, with no failures. After final
shader validation changes, all 17 shader/postprocess/MME gates and all 10
editor/Null/native-MME gates passed. The focused x86 suite had 17 passes and
four explicit skips for unavailable runtime DXC/validation-layer capabilities;
its Vulkan editor fallback test passed. Product builds with diagnostics off
and with all newer backends disabled also passed. Skipped external PMD/PMX
comparisons are not claimed as asset parity; generated PMD/PMX editor fixtures
are covered separately.
The local Sour Miku PMX also passed hidden import, live geometry/materials,
both self-shadow modes, exact repeats and backend reconstruction on DX11,
DX12 and Vulkan. Its model and textures remain outside the repository.

Optional real RayMMD coverage, reproduction and the boundary between native
DX9 execution and newer-backend fallback are tracked in
[RayMMD regression](RHI_RAY_REGRESSION.md).

## Live parameters and scene-v2

[MME parameter and script scope](RHI_MME_SEMANTICS.md) describes numeric shared
parameters, CONTROLOBJECT, the reflected b1..b3 constant layout and the scene-v2
SAS command subset. Public CC0 examples are ControlMaterial.fx, ControlPost.fx
and SceneScript.fx. The older profiles keep their versioned script restrictions.

Automatic legacy material import now handles a bounded SM3 source subset and
basic per-draw semantics. The next work is broader standard parameter providers,
automatic scene-effect import, shared target identity, DefaultEffect
child effects and mip generation timing. Texture slots remain t0..t3; expand
them using Ray's actual simultaneous bindings. Full Ray still uses same-frame
DX9 fallback. Passing portable examples does not establish native Ray support.
