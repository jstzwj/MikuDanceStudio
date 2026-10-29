# First portable MME profiles

The editor can execute a complete supported MME frame on DX11, DX12 or Vulkan.
These are explicit opt-in profiles, not a general FX9 translator. Existing
effects without the profile continue through same-frame DX9 fallback.

## Transport and selection

The embedded MME loader retains immutable source bytes per loaded revision;
assignment clones share them. A read-only CPU bridge exports parameter values,
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
extent/format overrides, extra semantics/annotations, includes, loops, MRT,
floating-point color products and depth sampling. Combining this first scene
effect with screen.bmp history is explicitly rejected pending a capture-order
comparison. Original native anisotropic differences remain separately tracked;
these profiles use explicit trilinear or point sampling.
MME object/subset visibility overrides also retain DX9 fallback in this batch.

## Gates

- `rhi_shader_contract`: Null binding/layout/lifetime/dependency rejection.
- `rhi_generic_shader_*`: native reflection, row-major matrices, constants,
  non-default slots, alpha, repeat and device replacement.
- `rhi_post_process_*`: exact copies, transforms, chaining, extent/orientation.
- `mme_portable_shader_*`: both public FX files against real DX9 rendering,
  including the native MME fullscreen quad. Material allows one UNORM step;
  the combined material/grade comparison allows two accumulated steps. Finish
  must equal Ping byte-for-byte on each backend, including DX9.
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

Next extensions should follow actual effects: general parameter semantics and
bindings, ordered scene scripts/targets, then MRT/float/depth capabilities.
