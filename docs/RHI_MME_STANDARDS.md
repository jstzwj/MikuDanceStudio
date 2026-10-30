# MME standard parameter providers and shader signatures

Unannotated PMD/PMX material effects now use CPU providers for the standard
parameters below on DX11, DX12 and Vulkan. `LegacyStandard.fx` is an original
CC0 FX9 fixture; it has no portable profile or alternate shader branch.

## Supported providers

| Family | Layout and behavior |
| --- | --- |
| WORLD / VIEW / PROJECTION / WORLDVIEW / VIEWPROJECTION / WORLDVIEWPROJECTION | float4x4; plain, INVERSE, TRANSPOSE and INVERSETRANSPOSE; camera/geometry family |
| POSITION / DIRECTION, Object=Camera | float4; inverse camera view translation or normalized forward; w=1 / 0 |
| POSITION / DIRECTION, Object=Light or Light0 | float4; light direction transformed by inverse BeginScene world; position is its negative, w=0; direction normalized, w=0 |
| DIFFUSE / AMBIENT / SPECULAR, Object=Light or Light0 | float4, native alpha=1 |
| DIFFUSE / AMBIENT / SPECULAR / EMISSIVE, Object=Geometry | float4 from current material |
| SPECULARPOWER, Object=Geometry | float, zero becomes 0.1 |
| VIEWPORTPIXELSIZE | float2, scene viewport dimensions |
| TIME / ELAPSEDTIME / TIME2 / ELAPSEDTIME2 | float, original MME clock machine |
| matWorld / matWorldViewProj / LightDir | native case-insensitive names, current matrices/light direction |
| use_texture / use_spheremap / use_subtexture / use_toon / transp | scalar bool from current texture roles and host transparency |
| VertexCount / SubsetCount | scalar int from current model record |

Recognized semantics take precedence over names. Type/shape mismatches reject
the whole frame. Dynamic standard parameters cannot be shared until native pool
write ordering is represented. Ordinary numeric/shared/CONTROLOBJECT values
retain the existing instance resolver. `Place` has no native setter and keeps
its instance value.

For PMD/PMX effect draws, object WORLD is identity. Camera VIEW is the native
BeginScene world multiplied by camera view; removing the BeginScene world from
VIEW would change effects. Matrix multiplication/inversion and vector
normalization use the existing DX9-compatible CPU math. Singular or nonfinite
matrix results reject preparation atomically, preserving same-frame fallback.
The DX9 binder's INVERSETRANSPOSE suffix length was corrected from 15 to 16;
previously these parameters were silently left unchanged. All 24 variants now
participate in the native binder regression oracle.

## One frame clock across both paths

`RenderFrameScene` advances the existing MME clock once after CPU frame updates
and before attempting a supported RHI frame. Its scope freezes further native
clock updates until that frame finishes. Every material receives the same clock
snapshot; if preparation/submission fails, DX9 consumes that snapshot too.
Scope destruction unfreezes the clock on every return. A frame selected directly
for DX9 still uses the original OnBeginScene update.

Edit-mode TIME follows the wall clock and restarts its epoch when returning to
edit mode. Playback TIME follows the raw timeline. ELAPSEDTIME retains the
original transition behavior. TIME2 is raw timeline time and ELAPSEDTIME2 its
signed delta, including backwards seeks. Backend/device replacement does not
restart the effect clock. Pure tests cover first update, edit/play transitions,
pause, seek and unsigned tick rollover; hidden frame tests count actual updates
and require exactly one update, including a forced RHI failure.

## Shader entry signatures

The physical vertex record remains XYZ, normal and UV. float4 POSITION receives
w=1. POSITION0/NORMAL0 aliases, direct/struct parameters, VS return/out/inout and
PS return/out (direct or a one-color struct) lower to deterministic modern stage
wrappers. The generated varying layout still maps clip position to SV_POSITION
and COLOR0 to SV_Target0 and applies the existing DX9 pixel-center correction.

Duplicate VS outputs, PS inout, unmatched varyings, pixel POSITION reads and
whole-input escape when clip position is present reject preparation. General
function overloads, custom vertex streams and additional output families remain
outside automatic import.

## Validation and remaining scope

`mme_standard_shader_*` compares reflected logical constant components against
the original native binder and compares offscreen pixels against actual DX9.
The fixture uses noncommuting matrices, perspective, all 24 matrix variants,
camera/light/name/clock providers, normals, sampling and alpha blending.
`mme_standard_struct_*` and `mme_standard_inout_*` execute generated signature
variants on all three backends. DX12 debug and Vulkan validation variants run
the standard fixture. Singular-matrix preparation must fail without replacing
the draw. Material tolerance remains one UNORM8 step, accumulated post arithmetic
two; final copy and repeated submissions remain byte-exact.

`editor_frame_*` covers generated PMD/PMX with the actual frame owner, switching,
seek and device failure/rebuild, once-only geometry and clock updates, and zero
DX9 scene draws on successful RHI frames. Optional
`editor_mme_standard_pmd_*` / `editor_mme_standard_pmx_*` use
`MDS_SELF_SHADOW_PMD` / `MDS_SELF_SHADOW_PMX` to run the same material on local
real assets without copying them into the repository. Every window stays hidden.

Light-family matrices, remaining native draw names, automatic scene scripts,
shared render-target identity, DefaultEffect child turns and mip timing still
need their own contracts. Ray remains a same-frame DX9 fallback. These changes
do not establish native support for arbitrary FX9 effects or Ray.

## Local batch status (2026-10-30)

After DX9 became available, all 11 direct/struct/inout standard-parameter image
gates passed against the actual native binder, including DX12 debug and Vulkan
validation. This covers the expanded 24-matrix oracle on all three APIs.

The hidden-editor rerun corrected two fixture errors: resizing only the viewport
without its hidden parent client, and querying a lazily created device before
its first frame. Real PMD switching then exposed a production Vulkan shader
build omission that dropped ordinary material specular highlights. The shared
SPIR-V build now enables the same lit pixel variant as DX11/DX12; see the public
[specular regression](RHI_MATERIAL_SAMPLING.md#lit-material-specular-regression).

The x64 56-gate MME/material/editor scope is now accepted: the initial run passed
37 gates, and all 19 previously blocked gates passed after DX9 recovered. No
implementation or comparison bounds changed between those runs. These include
the 720-case sampling matrix, the four exact specular cases, effect image
oracles on all three APIs and the following hidden-editor entries:

- `editor_frame_{dx11,dx12,vulkan}`: generated PMD/PMX, resize, unload, frame
  driver, switching, same-frame fallback and device recovery.
- `editor_mme_standard_{pmd,pmx}_{dx11,dx12,vulkan}`: local original
  Miku PMD and Sour Miku PMX. Each entry switches the retained effect through
  DX9/DX11/DX12/Vulkan, requires exact output equality between supported RHI
  backends, tests recreation and seek, and counts once-only geometry/clock
  updates and zero DX9 scene draws on RHI frames.
- `editor_ray_fallback_{dx11,dx12,vulkan}`: generated PMD/PMX with the local Ray
  effect, retaining same-frame DX9 fallback across backend switches.

The initial interruption was a native DX9 initialization failure
(CheckDeviceType=0x8876086A; CreateDevice also returned 0x8876086C). The resumed
19-gate run executed the full image/frame checks, including real-model and Ray
entries starting from DX12/Vulkan, and passed with zero failures or skips.

Independent final checks passed another 22 x64 gates covering CPU/Null contracts,
generic shaders and MRT/multipass, including Vulkan validation. This makes 78
distinct passing x64 cases across the completed runs, with zero skips.

The resumed x86 DX9-dependent scope passed 32 gates with 11 explicit Vulkan
runtime-DXC/validation capability skips. All 12 hidden editor entries passed,
including generated PMD/PMX, both real models and Ray fallback starting from each
backend. These tests exercise same-frame DX9 fallback where native Vulkan effect
compilation is unavailable; they do not establish native x86 Vulkan MME support.
The independent x86 suite passed 27 gates with eight capability skips; its
720-case sampling and four specular cases ran on all three APIs. Together these
are 78 distinct x86 cases: 59 passed, 19 capability skips, zero failures.

x64/x86 applications and the diagnostics-off product built successfully; the
newer-backends-disabled product had already passed in the preceding batch. No
device-availability skip, tolerance relaxation or desktop action was added.
All test windows stayed hidden. Ray still uses same-frame DX9 fallback on both
architectures; these results cover the documented supported effect scope.
