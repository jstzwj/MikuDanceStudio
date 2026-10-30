# Ordinary FX9 MRT/HDR and relative targets

Later extension: [ordered material scripts and Ray MaterialMap import](RHI_MME_RAY_MATERIAL.md)
adds two geometry MRT groups and source diagnostics. The validation below
records this earlier batch; it is not evidence for the later material chain.

Unannotated FX9 material and scene shaders now describe their actual ordered
pixel outputs to the shared RHI. `COLOR` means slot zero; `COLOR0` through
`COLOR3` map to `SV_Target0` through `SV_Target3`. Struct results and `out`
parameters can be combined and declared in any order. Duplicate, sparse or
unknown output semantics fail source lowering. Actual component counts are
retained; the graphics format/layout validator still rejects undefined channels.

The scene assembler remaps a compatible scene material pass into all capture
attachments, installs their real RGBA8/RGBA16F formats, and preserves ordered
clear/load/store and sample dependencies. It checks shader output counts before
uploading anything. An ordinary single-output material cannot silently fill an
MRT capture. Builtin model/outline/grid pipelines have no corresponding generic
MRT shader and retain whole-frame, same-state DX9 fallback when captured there.
Smaller `ScriptExternal` scene captures and wrapping an already-MRT capture in
another MRT scene remain unsupported. Ordinary `Draw=Buffer` intermediates can
have their own fixed or relative dimensions.

## Size and viewport rules

`MmePortableTarget` distinguishes paired fixed Width/Height from native
`ViewportRatio`. The exporter retains the publisher's ratio instead of copying
the last DX9 allocation dimensions. Frame preparation resolves positive finite
products using single-precision multiplication and truncation, matching SAS.
For example 55x37 at (.5,.5) produces 27x18; at (.25,.25), 13x9.
The resolved dimensions must be 1 through 4096. Fixed and relative descriptions
cannot be combined. Shared aliases keep the publisher's resolved allocation;
entering a smaller offscreen turn must not apply the ratio a second time.

Zero, negative and positive-but-rounded-to-zero requests keep DX9 fallback.
Native initial creation substitutes the screen extent for nonpositive requests;
resource recreation can instead reach the native 64-pixel allocation default.
This batch does not replace those lifecycle-dependent rules with a minimum-one
clamp. Formats, attachment extents, initialization and shared identity conflicts
remain checked by the existing frame/Null contracts.

`VIEWPORTPIXELSIZE` keeps the native BeginScene size, or current offscreen turn
size. It does **not** change when a script redirects to a smaller intermediate.
The importer wrapper's separate b0 viewport correction uses the actual draw
destination. Confusing these two sizes changes sample offsets in Ray's
downsample and blur shaders.

## Public oracle

The original CC0 fixtures `LegacyMrtMaterial.fx` and `LegacyMrtScene.fx` have no
portable profile annotations or alternate shader branches. They exercise:

```text
MRT material -> CaptureA/B RGBA16F
             -> HalfA/B RGBA16F (ViewportRatio .5)
             -> QuarterHdr RGBA16F (ViewportRatio .25)
             -> Tone RGBA8 -> exact final copy
```

The material deliberately returns struct fields in COLOR1/COLOR0 order. The
first scene pass uses reversed `out` arguments. Native DX9 runs the actual SAS
`ScriptExternal` / `Draw=Buffer` sequence and material draw. The offscreen test
reads every half-float intermediate directly, including negative values,
highlights above one, spatial UV gradients and fractional alpha. Comparison
limits stay at one half ULP and two UNORM8 code values; Tone/final copying and
repeat submission require identical bytes. The test uses 48x32, 55x37, then
48x32 and replaces the RHI device. DX12 debug and Vulkan validation variants
require zero diagnostic errors. Child-turn allocation and atomic extent rejection
are also checked.

Hidden editor gates assign both ordinary FX files through the production MME
exporter. They exercise supported material editing, backend switching, device
recreation and unsupported outline/output-layout fallback before RHI uploads.
The independent editor oracle replaces its private native targets on resize;
it discards the DX9 snapshot family at that replacement boundary. Retaining its
grow-only larger snapshots would add a native StretchRect rescale on shrink,
rather than compare equal logical target allocations. This is test harness
lifecycle setup, not a change to the native renderer or comparison tolerance.
Successful RHI frames must still perform one geometry update and zero DX9 scene
draws. Every test stays offscreen or in a never-shown HWND.

## Ray boundary

Version-two [native requirement reports](RHI_MME_NATIVE_REQUIREMENTS.md) include
numeric viewport ratios and a row for every selected effect/technique/pass.
Source import, wrapper generation and actual HLSL SM5 compilation are distinct
stages. A source failure marks later shader stages blocked, and parameter,
resource and state compatibility unassessed. Shader compilation in this report
does not establish Vulkan execution or whole-frame support.

The local unmodified Ray root currently stops at `MME_legacy_syntax`; the
MaterialMap child stops at `MME_legacy_sampler_texture`. Existing inventories
also require A8, L8 and R32F targets beyond this portable color-target subset.
MRT and relative allocations alone therefore do not enable Ray on the newer
backends. Its existing native DX9 path and same-frame fallback remain active.

## Validation on 2026-10-30

The x64 acceptance scope passed all 85 existing gates, including all six hidden
editor/Ray gates and both native requirement reports. The five new MRT gates
initially failed because the test attempted to use Null through the resident
scene renderer, which Null does not implement. The test now exercises Null's
resource, Pass and binding interfaces directly. All five corrected MRT gates
passed, including DX12 debug and Vulkan validation; product code was unchanged
by this test correction. The initial 90-gate log remains
`out/mme-legacy-mrt-x64-test.log`, with its `-details.log` companion; the final
five-gate evidence is `out/mme-legacy-mrt-x64-final.log` and
`out/mme-legacy-mrt-x64-final-details.log`.

Every x64 new-fixture float comparison measured zero half ULP and every RGBA8
comparison measured zero code-value delta. The 36 new hidden-editor comparisons
also measured zero maximum RGBA delta across PMD/PMX, all three backends, initial
size, odd resize and restore. Exact final copy and repeated submissions retained
byte equality. Comparison limits were not widened.

The x86 90-gate scope finished with 65 passes, 25 explicit Vulkan runtime-HLSL
or validation-capability skips, and no failures. All six hidden editor/Ray gates
and both native requirement reports passed. Native MRT execution was verified
on x86 DX11/DX12; Vulkan shader execution was verified on x64. The x86 editor's
24 new native-oracle comparisons also measured zero maximum RGBA delta.
Evidence is `out/mme-legacy-mrt-x86-test.log` and its `-details.log` companion.

Both x64 and x86 diagnostic applications and the diagnostics-off x64 product
built successfully. This successful run does not close the preceding batch's
[intermittent Vulkan/Ray crash observation](RHI_MME_SUBEFFECTS.md#stability-note).
