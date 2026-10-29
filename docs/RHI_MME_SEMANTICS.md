# MME parameters and scene scripts

The live editor can retain a supported material and scene effect while switching
DX9, DX11, DX12 and Vulkan. Successful RHI frames execute no DX9 scene draws.
The existing DX9 effect instances remain CPU metadata/parameter-pool owners;
new-backend draws receive immutable CPU bytes and resource dependencies.

This is an extension of the explicit portable profiles, not automatic import
of unmodified FX9 effects. Full Ray still uses same-frame DX9 fallback.

## Shared parameters and CONTROLOBJECT

The CPU preparation walk refreshes the original object name/order/placement
table, then calls the existing control resolver in native assignment order.
It includes hidden objects, base assignments and explicit material assignments.
It does not begin a DX9 scene, bind render targets or execute SAS draws.
The resolver retains its existing self/attached-object/bone/morph/missing-object
rules. These rules are reused, rather than reimplemented for each backend.

Numeric `shared` values retain the D3DX pool's identity and ordering. There is
no second RHI pool with a different set of defaults. Snapshots carry float,
int and bool values, their logical row/column/array shape and the shared flag.
The frontend reflects b1..b3 from both modern shader stages and requires matching
declarations. Supported shader members are float scalars/vectors, row-major
float4x4 matrices, scalar ints and scalar uint transport for FX bools. Arrays use
16-byte element alignment, or 64-byte matrix elements. Binding checks validate
the snapshot even when its immutable program was already cached.

Existing b0 material/post ABIs are unchanged. Other standard numeric semantics,
arbitrary structs, int/bool vectors and shared textures are not enabled by this
batch. Unsupported declarations retain whole-frame fallback. CONTROLOBJECT
`Name`/`Item` annotation names are case-insensitive; empty optional Item follows
the native whole-object behavior.

## scene-v2

SceneScript.fx opts into `MDSRhiProfile="scene-v2"`. It uses one scene-class
postprocess technique, annotated VS/PS entries and Draw=Buffer pass scripts.
`MDSRhiInputs` lists the ordered target names bound at t0..t3. Legacy sampler
states must match point/clamp.

The loader snapshots expanded legacy and MDS_RHI sources alongside the native
FX9 load, using the actual _INDEX/MME_MIPMAP defines and include directory.
Assignment clones share these immutable bytes. Device replacement compiles from
the loaded snapshot, including when an included file changed on disk after load.
A preprocessing failure makes the include-dependent RHI effect unsupported;
it does not change native FX9 loading or rendering. The current source limits
are 1 MiB for the root and 16 MiB for each expanded variant.

The exporter consumes the original SAS interpreter's parsed command table.
The CPU planner supports ordered color targets 0..3, depth target restoration,
ClearSetColor/ClearSetDepth, color/depth clears, Pass, one ScriptExternal capture
and nested LoopByCount/LoopGetIndex/LoopEnd. Clear color conversion retains
native clamp-and-truncate to ARGB8 before conversion into the RHI clear value.
Each expanded draw gets its own parameter snapshot. The original CPU parameter
values stay unchanged during support checks and graph construction.

Private LoopGetIndex writes are committed to the matching live effect revision
only after successful rendering/presentation. A failed preparation cannot leave
the index advanced before same-frame DX9 fallback. Shared loop-index writes are
rejected until cross-effect script ordering and pool writeback are represented.

The capture inserts the current scene draws at ScriptExternal, after the script's
clears and before its remaining passes. It preserves the script's clear color
under transparent geometry. It does not copy an already composited main image.
Hidden scene carriers do not apply their postprocess; their control assignments
still participate in the native CPU refresh walk.

Named targets use the viewport extent, RGBA8/RGBA16F colors and D24S8 depth.
Scene capture/final presentation are currently RGBA8. Color/depth capture and
first writes require explicit initialization; previous-frame contents are not
invented. Dense attachments, duplicate targets, feedback, read-before-write,
inconsistent output counts and incomplete scripts fail on the CPU. Limits are
64 expanded steps and 4096 executed commands/loop count magnitude. These are
explicit fallback boundaries, not silent truncation of the native script.

## Remaining compatibility work

| Requirement | Current state |
| --- | --- |
| Explicit portable material/post/MRT profiles | Native DX11/DX12/Vulkan |
| Numeric shared constants and CONTROLOBJECT | CPU resolver/pool reused, native constant binding |
| Bounded single-carrier scene scripts | Native scene-v2 graph |
| Includes in explicit portable profiles | Expanded once per loaded revision |
| General legacy FX9 shader syntax | Not translated |
| General per-draw camera/light/material/time semantics | Not migrated beyond existing profile ABIs |
| Shared render-target identity across effects | Not migrated |
| Multiple nested scene carriers and DefaultEffect child turns | Not migrated |
| Render-target mip generation and viewport-ratio targets | Not migrated |
| Depth/cube/volume sampling and more than four textures | Not in the generic profile |
| Unmodified Ray | DX9 fallback, including with another backend selected |

Normal native switching for arbitrary MME requires completing those missing
contracts and testing real legacy effects. A Ray fallback/recovery pass is not
evidence of native Ray execution. The next importer can use retained preprocessed
source revisions to translate legacy shader I/O/samplers, export actual bindings
and standard semantics, then add shared target/child-turn execution in original
script order. Required formats and mip timing should follow Ray's live resource
records rather than hard-coded effect filenames.

## Verification

All tests are offscreen or use a never-shown parent HWND. No desktop input,
focus changes or visible editor hosts are used.

`mme_scene_plan` runs without a GPU or Windows headers. It checks loop snapshots,
deferred writes, capture initialization, clear quantization, feedback, sparse/MRT
attachment errors and expansion limits. `mme_portable_controls_*` compares the
public material/post effects to real DX9 and rejects invalid snapshots.
`mme_scene_script_*` runs the actual native SAS parser/step/resume/Draw=Buffer
pipeline against the same generated geometry and textures. Material comparison
allows one UNORM8 step and accumulated post arithmetic two; the final copy and
repeated submissions remain byte-exact on every backend.

`editor_frame_*` loads generated PMD and PMX, modifies live controls/pool values
and loop counts, hides/restores the scene carrier, recreates devices and switches
live effects through all available backends. It checks exact recovered images,
once-only geometry updates, zero DX9 scene draws on successful RHI frames and
same-frame fallback for unsupported script expansion. Missing Vulkan runtime DXC
continues to use the tested fallback path. Original Ray script/offscreen gates
and hidden editor Ray fallback/recovery gates remain separate acceptance tests.

Local validation (2026-10-30): the x64 scoped acceptance reached 32 passing
cases, including both native validation variants, parameter/script DX9 oracles,
MRT/HDR, Null contracts, hidden editor switching and real Ray fallback/native
script gates. The first broad run had eight DX9 CreateDevice failures before
any frame ran (including 0x8876086A). The final targeted 18-case run passed,
followed by all nine include-revision/editor/native-Ray gates after source
snapshot integration. No comparison tolerance or availability skip was added.

The x86 parameter/script/editor scope had 17 passes and four explicit Vulkan
runtime-compiler skips; its Vulkan editor fallback and recovery passed. All
seven final x86 planner/include-revision/editor/Ray fallback gates then passed.
Both
architecture GUI builds and the diagnostics-off / newer-backends-disabled
product configurations compiled. These measurements do not establish native
execution of unmodified Ray or arbitrary legacy FX9 shaders.
