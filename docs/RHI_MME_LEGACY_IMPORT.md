# Bounded automatic FX9 material import

Unannotated model material effects can now execute natively on DX11, DX12 and
Vulkan when their complete source and bindings fit the following subset. The
effect needs no `MDSRhiProfile`, alternate shader branch or renamed entry points.
`LegacyMaterial.fx` is an original CC0 example that also executes unchanged on
DX9. This does not enable native Ray or arbitrary FX9 effects.

## Source and shader translation

The importer consumes the loader's immutable, expanded **legacy** source. Its
CPU-only result is shared by assignment clones and cached per loaded revision;
it does not parse source each frame. Includes and the original loader macros
are resolved before import. Device replacement uses the same loaded revision.
Import does not require a successful MDS_RHI expansion or reopen root includes;
only the frozen legacy description supplies shader source.

It reads numeric declarations, material textures, sampler states, structures,
functions, techniques and actual `compile vs_3_0` / `ps_3_0` entries. Shader
functions and parameter names are retained. Supported `tex2D`, `tex2Dlod`,
`tex2Dbias`, `tex2Dgrad` and `tex2Dproj` calls with a named sampler become modern
Texture2D operations. Declarations are emitted into reflected row-major b1
constants and ordered tN/sN resources. Scalar FX bools use uint transport.

The VS reads float3 POSITION/NORMAL and float2 TEXCOORD0 from the existing
32-byte vertex record, either directly or through a structure. It returns a
structure with float4 POSITION and TEXCOORD varyings. The PS takes a structure
with matching fields and returns float4 COLOR0. Generated wrappers map the
outputs to SV_POSITION/SV_Target0 and reproduce DX9 pixel centers with a
viewport-dependent clip-space correction. Pixel POSITION reads, whole-input
aliasing/passing and COLOR varyings are rejected until their legacy behavior
is explicitly represented. Function overloads and compile arguments are also
outside this initial subset.

## Parameters and resources

The existing MME instance and shared pool own live numeric values. Each draw
receives fresh immutable bytes, including supported CONTROLOBJECT values.
Per-draw standard semantics are supplied by the current CPU geometry/material
state, rather than a previous DX9 draw:

| Semantic | Accepted layout |
| --- | --- |
| WORLD, VIEW, PROJECTION, WORLDVIEW, VIEWPROJECTION, WORLDVIEWPROJECTION | float4x4 |
| DIFFUSE, AMBIENT, SPECULAR, EMISSIVE with Object=Geometry | float4 |
| SPECULARPOWER with Object=Geometry | float, native zero-to-0.1 rule |
| VIEWPORTPIXELSIZE | float2 |
| CONTROLOBJECT | Existing resolver, supported numeric layout |
| No semantic | Numeric instance/shared values |

Supported numeric storage is float scalar/vector/float4x4, scalar int/bool and
literal arrays up to 256 elements. Dynamic draw semantics cannot be shared
until their pool-write ordering is modeled. The native MME name-table aliases
such as matWorld, DifColor and use_texture are explicitly rejected; treating
them as ordinary constants would read stale state. Other camera/light/time
semantics and their annotations also retain fallback.

Material base, sphere and toon texture semantics resolve through the draw's
actual resource roles. At most four declared samplers get independent slots,
including multiple samplers of one texture. Each sampler explicitly specifies
point/linear min, mag and mip filtering and Wrap/Clamp U/V. Missing resources,+sRGB sampling, MipFilter=None, anisotropy and unrepresented sampler state
select fallback. The normal model pipeline's portable trilinear default is
unchanged; imported effects retain their supported explicit sampler settings.

## Selection and failure behavior

The first importer accepts PMD/PMX object-class, standard-order effects with
one pass per object/object_ss technique. Native SAS still chooses techniques
using the current subset and texture conditions. Scene scripts, accessories,
offscreen child turns, multiple material passes and other output families
remain outside automatic import. Explicit portable scene/MRT profiles remain
available alongside an imported model effect.

Representable depth, cull and blend state is taken from the pass or inherited
from that draw's explicit CPU state. The pass must explicitly disable alpha
test, stencil, sRGB write and separate-alpha blending, select Solid fill and
Add blending, set both depth biases to zero and enable all color channels.
Unknown or dynamic state fails validation. All assigned effects and used
programs must succeed before RHI submission; otherwise the editor performs
same-frame DX9 fallback with no second animation/geometry update.

The source budget is 16 MiB, one million tokens, 256 numeric declarations,
64 passes and four samplers. Unsupported syntax is rejected, not skipped.

## Regression gates

All GPU tests render offscreen; editor hosts remain hidden and receive no
desktop input.

- `mme_legacy_source`: declaration/entry/sampler translation and rejection of
  unsupported syntax, addressing, filtering and pixel position access.
- `mme_legacy_shader_*`: unchanged FX9 source against real DX9 output, with
  nonidentity matrices, normals, arrays, bools, shared values and transparency.
  A retained legacy expansion remains usable when the modern branch fails
  preprocessing and root includes are no longer available.
- `mme_legacy_samplers_*`: generated variant with distinct base/toon textures,
  reversed declaration/binding order, linear/wrap and point/clamp sampling.
  DX12 debug and Vulkan validation variants exercise the same chain.
- `editor_frame_*`: actual generated PMD/PMX assignments, live parameters,
  DX9/DX11/DX12/Vulkan switching, resize and device reconstruction. Successful
  RHI frames must perform zero DX9 scene draws and exactly one scene update.
- `editor_ray_fallback_*`: an unmodified local Ray effect still rejects
  unsupported import and recovers in the same frame.

Material comparison retains the existing maximum one-UNORM-step bound. The
material-plus-postprocess chain retains its two-step accumulated bound; its
final copy and repeated submissions remain byte-exact. No comparison tolerance
was widened for this importer. Missing runtime Vulkan shader compilation is an
explicit capability skip in direct tests and DX9 fallback in the editor.

Local verification (2026-10-30): the focused x64 suite passed all 32 gates. The
x86 suite passed 25 gates with seven explicit runtime shader/validation
capability skips; all six hidden editor/fallback gates passed. After the final
frozen-source change, the 12 shader/import gates were rerun on each architecture.
The generated unannotated material and postprocess comparisons measured zero
channel delta on the tested x64 backends, including debug variants. Diagnostic
x64/x86 application builds, the diagnostics-off product build and the build
with newer backends disabled all succeeded. These measurements cover generated
models/textures and Ray fallback, not native Ray rendering or arbitrary assets.

Next work is native standard-parameter providers, broader shader signatures,
automatic scene-effect import, shared render-target identity, DefaultEffect
child turns and mip timing, following actual Ray bindings and script order.
