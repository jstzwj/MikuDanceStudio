# Ray material import and ordered material scripts

This batch imports the local, **unmodified** `Materials/material_2.0.fx` into
the shared shader contract. It does not enable the full Ray scene on RHI.
The first remaining root-source blocker is a computed non-const `static`
initializer (`mSSAOScale`, `ray.fx:61`); execution still requires additional shader, resource
and script semantics. Unsupported frames continue to use the current CPU
state through DX9 before any RHI scene submission.

## Source diagnostics and import scope

The v3 native requirements report distinguishes source import, wrapper export,
and actual VS/PS SM5 compilation. Source failures include the declaration,
technique/pass, token, expanded byte offset and file/line/column from `#line`.
Resource and execution support remain separate from successful compilation.

The source importer now supports literal constant arrays (1..256 elements),
internal structs without stage semantics, bare or bracketed sampler texture
references, explicit anisotropy requests and overloaded helpers. Stage entry
overloads are rejected. It emits a conservative reachable helper graph across
all declared pass entries and all overloads; unused sampler-parameter library
helpers need no invented texture/state binding. Anisotropic declarations must
provide an explicit `MaxAnisotropy` in 1..16; an inherited native value is not
guessed. A reachable untranslatable
sampler argument still fails. Immutable FX9 constants become `static const`
in modern HLSL so they do not create an implicit `$Globals` constant buffer.

The default portable sampling profile converts imported anisotropic filters
to trilinear while preserving explicit point filters, addressing and uploaded
mips. Native mode retains the requested anisotropy. The generic sampler
contract accepts anisotropic mip requests only with anisotropic minification
or magnification, matching the existing backend sampler representation.
The DX9 editor's filtering is unchanged.

## Material script contract

Selected object/object_ss techniques may execute up to eight shader passes.
Empty native techniques suppress their draw, including empty edge/shadow/zplot
techniques. Native SAS, rather than a second string parser, exports the selected
technique's command table. The initial material subset supports only:

- `RenderColorTarget0..3`, with no mip or cube-face selector;
- ordered `Pass` commands, with a maximum of 32 draws.

Clearing slot zero restores the calling draw's primary color attachment;
clearing another slot disables it. Outputs must be dense and unique. Shared
targets retain publisher identity, format and resolved viewport size. Feedback,
unknown targets, output-count mismatches and incompatible extents are rejected.

Frame assembly splits the pending host pass at each scripted object, preserving
object and pass order. It loads the caller's depth and each color attachment,
then resumes the host pass with Load semantics. It never invents a clear.
The surrounding scene script must initialize shared attachments before use.
A single-color scene capture remaps only the calling color attachment within
an existing material MRT group. Nested MRT captures remain unsupported.

Material states may inherit only the explicit supported CPU draw state.
Inherited alpha testing, stencil, partial color writes or unsupported blending
keep DX9 fallback. Explicit unsupported fixed state also fails. Scene Buffer
passes retain the stricter explicit-state requirement. This bounded inheritance
does not assert that an arbitrary Ray editor frame is supported.

## Public fixtures and gates

`LegacyMaterialMap.fx` and `LegacyMaterialMapScene.fx` are original CC0 fixtures
without alternate shader branches or portable annotations. The material uses
two ordered four-output geometry passes. The scene explicitly clears all seven
shared targets, captures the material, and combines the two groups. Signed and
greater-than-one values remain in RGBA16F targets.

`mme_material_script_test` uses generated real PMD/PMX inputs through the CPU
producer and actual texture preparation. It has independent paths:

- `null`: dependency/binding validation, missing initialization, feedback,
  stale handles, extent conflicts and atomic preparation failure.
- `<api>-compile`: compile and execute both material passes, compare all eight
  targets with DX11, verify strict final copy, repeated submission and device
  replacement. This path requires no DX9 device.
- `<api>`: run the native DX9 SAS technique against the same geometry and
  textures, then compare all eight raw targets. The oracle explicitly selects
  the portable trilinear profile for this comparison. It does not claim exact
  native-anisotropic parity.

Each GPU path covers opaque and translucent material values and widths
128 -> 163 -> 128. RGBA8 comparisons permit at most two code values; RGBA16F
comparisons permit at most one half ULP and reject nonfinite values. Copy,
repeat and replacement comparisons require byte equality.

The optional Ray gates load the original material and includes from
`MIKUDANCESTUDIO_RAY_TEST_EFFECT`'s directory. The public publisher initializes
its shared targets. This isolates the real material child; it does not execute
Ray's root, lighting, DefaultEffect chain or final composition. No Ray source,
model or texture bytes are copied into the repository.

The hidden editor gates additionally exercise public scripted materials through
production effect assignment, once-only updates, zero DX9 scene draws on RHI
frames, odd resize, switching, reconstruction and missing-publisher fallback.

## Validation on 2026-10-30

The x64 independent CPU/RHI scope passed all 51 gates, including ten public/Ray
material execution gates and DX12 debug/Vulkan validation variants. For every
one of the 120 material/viewport/opacity cases, all eight material attachments
and the combined FinalColor measured zero delta against DX11, including zero
half ULP on the two RGBA16F targets. Exact final copy, five repeat submissions
and device replacement retained byte equality. Limits were not widened.
Evidence: `out/mme-ray-material-x64-test.log` and `-details.log`.

Both architecture application builds and the diagnostics-off x64 product
build succeeded. All tests are offscreen or use never-shown HWNDs.

The corresponding x86 scope finished with 37 passes, 14 explicit Vulkan
runtime-HLSL/validation-capability skips and zero failures. Its 72 material
cases on DX11/DX12, including DX12 debug, also measured zero difference for
every intermediate and combined output. Evidence:
`out/mme-ray-material-x86-test.log` and `-details.log`.

**The native DX9/editor matrix remains incomplete.** After DX9 recovered, the
x64 `mme_ray_material_dx11` gate passed against the original Ray material child,
including all eight attachments, PMD/PMX, both material opacities, resize,
repeat and replacement. This is the isolated material oracle, not a complete
Ray scene. Evidence: `out/mme-ray-material-native-probe3.log`.

That run exposed two public-fixture issues. Shared MRT clears now use transparent
black on both sides; the old RHI test initialization used the attachment default
Alpha of one while the native side cleared to zero. The public UV clip plane
also moves from .5 to .473 to avoid testing an exact pixel-center `clip(0)`
decision whose interpolated sign can differ between SM3 and SM5. Both ordered
passes and their coverage remain in the fixture; image limits are unchanged.
Native confirmation of the adjusted clip plane is still pending.

After these fixture changes, the focused independent x64 suite passed 11/11
gates. Its 120 material cases produced 1,080 attachment/final-color comparisons
with zero difference, including zero half ULP. The x86 rerun passed seven gates
with four explicit Vulkan runtime-HLSL/validation-capability skips and no failures.
Both architecture test builds succeeded. Evidence:
`out/mme-ray-material-resume-x64-test.log`, `-details.log`, and the corresponding
`out/mme-ray-material-resume-x86-test.log` / `-details.log`.

The hidden editor's earlier DefaultEffect, nested scene and ordinary MRT/HDR
oracle phases passed, but the new scripted-material composite failed with
maximum RGBA delta 120 (limit 2). Its cause is still unresolved. The test now
rejects a native effect with a latched execution failure before accepting its
image as a reference, and reports both native composition inputs alongside
the first differing pixels. Evidence for the original image failure:
`out/mme-ray-material-native-probe2.log` and `-details.log`.

Subsequent x64 and x86 native attempts again failed before executing a frame:
`CreateDevice` returned `0x8876086C`, with adapter display 0x0/unknown format.
These remain failures, not passing tests or capability skips. The remaining
native backend gates, the final hidden editor matrix and native v3 requirement
inventories need another run when DX9 stays available. Evidence:
`out/mme-ray-material-editor-native-probe2.log`,
`out/mme-ray-material-native-x86-probe.log` and
`out/mme-ray-material-native-clip-probe.log`. The final editor attempt with
the rebuilt diagnostics failed at the same device initialization step:
`out/mme-ray-material-resume-native-final.log` and `-details.log`.

The independent RHI comparisons above do not substitute for the native oracle
or establish DX9 image parity. The earlier intermittent Vulkan/Ray crash
observation also remains open; see the preceding subeffect batch's stability
note. This batch makes no full-Ray or general FX9 compatibility claim.
