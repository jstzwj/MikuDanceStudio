# Automatic scene effects and cross-effect render products

An ordinary scene/postprocess FX9 effect can now enter the shared DX11/DX12/Vulkan
frame without `MDSRhiProfile`, an alternate shader branch or entry annotations.
The importer reads immutable expanded legacy source for declarations and VS/PS
entries, and exports commands from the existing parsed SAS description. There is
no second scene-script parser and no DX9 draw needed to prepare a supported frame.

Each imported whole-object scene carrier has one selected technique,
multiple Buffer passes and one `ScriptExternal` scene capture. Ordered nesting,
`DefaultEffect` children and offscreen mip timing are described in
[subeffects and mip timing](RHI_MME_SUBEFFECTS.md). Numeric instance,
shared and CONTROLOBJECT parameters use the existing refresh walk. Viewport
parameters use the current RHI extent. Pass dependencies include only samplers
reachable from the selected pixel entry, including calls through helper functions;
unused samplers in other passes do not become false feedback dependencies.
Vertex texture reads and unrepresented render state still select fallback.
Imported Buffer draws preserve the native quad's four input vertices and its
diagonal, including for nonlinear vertex processing. Clip correction is applied
after the effect's own vertex shader; fullscreen VS work is not assumed affine.

Scene target declarations accept viewport-sized, single-level 2D RGBA8/RGBA16F
color targets and D24S8 attachments. The initial shader importer writes one
COLOR0 output; capture and final presentation remain RGBA8. Explicit MRT/HDR
profiles continue to cover their existing scope. Initialization is explicit:
targets used by scene capture and first Buffer writes require a clear. Sharing
does not add previous-frame history or infer clears.

## Shared identity

The native bridge reads each effect's live texture parameter and finds the SAS
resource that published that actual texture. Each allocation/rebuild receives a
fresh monotonic CPU revision token. All aliases export that token, including a
material effect that did not create the resource. Distinct private allocations
with equal names stay distinct. Native handles never enter a portable frame.

The shared pool's type/name/semantic matching still applies. The public pair uses:

```hlsl
// Scene publisher
shared texture SharedMask : RENDERCOLORTARGET < string Format="A8R8G8B8"; >;
// Separate material reader: matching semantic, no allocation annotations
shared texture SharedMask : RENDERCOLORTARGET;
```

`SharedFrameTargets` canonicalizes these revisions with equal dimensions, format,
access, mip/layer/sample counts. Upload keys and target references stay distinct.
Registry epochs reject expired leases; frame/Null validation rejects conflicts,
feedback, reads before initialization and stale handles. Resize, source reload
and device replacement reassemble the frame and rebuild native resources from
the CPU description. Contents do not survive implicitly across frames/devices.

All effects declare their shared products before material draws are assembled.
The scene script can initialize a product before `ScriptExternal`, material
shaders sample it during that capture, and subsequent Buffer passes read the
captured scene. Physical aliases participate in the final frame dependency
validation. Missing bindings, unsupported sources, illegal dependencies or native
pipeline failures cause whole-frame, same-state DX9 fallback before RHI submission.

## Public example and verification

`assets/effects/portable/LegacyScene.fx` clears a shared mask, captures the scene,
grades it and copies it to the original output. `LegacySharedMaterial.fx` reads
that mask through a separate shared-pool effect. Both are original CC0 examples
and execute unchanged through the DX9 effect runtime.

- `mme_legacy_source` checks scene declarations, entries, per-pass sampler
  reachability and unsupported Draw commands.
- `mme_legacy_scene_*` uses the public pair with a real DX9 shared pool and SAS
  execution as oracle. It checks material output, capture timing, two-pass output,
  Alpha/pixel mapping, exact final copy and twelve repeated submissions. Existing
  one-step material/two-step accumulated channel bounds are unchanged. DX12 debug
  and Vulkan validation variants require zero native validation errors.
- `editor_frame_*` assigns the public pair to generated PMD/PMX scenes and keeps
  it across DX9/DX11/DX12/Vulkan switches. It checks alias identity, live mask
  changes, resize, device rebuilding, missing shared-binding fallback/recovery,
  exactly one scene update and zero DX9 scene draws on successful RHI frames.
- `rhi_shared_targets` exercises invalid declarations, feedback, initialization,
  lease invalidation and Null native handles; `rhi_generic_shader_*` samples all
  sixteen independently populated slots, including t15/s15.
- Native Ray requirements reports and hidden editor Ray fallback remain separate
  gates. These establish observed requirements and compatibility recovery, not
  native Ray image parity.

All GPU work is offscreen or uses never-shown HWNDs. No desktop input or visible
application is needed. Runtime shader compilation remains capability-dependent;
unavailable compilation is an explicit direct-test skip and editor DX9 fallback.

Viewport ratios other than one, sampled depth/cube/volume and arbitrary FX9 syntax
remain later work. Unmodified Ray continues to use same-frame DX9 fallback.

## Local acceptance (2026-10-30)

The x64 scoped suite passed all 74 gates, including the five new scene-effect
oracles, sixteen-slot sampling, native validation, six hidden editor/Ray gates
and both native requirements reports. The public shared-material/scene pair
measured zero maximum channel delta against DX9 on DX11, DX12 and Vulkan,
including their debug/validation variants. Final copies and repeat submissions
were byte-exact; no comparison thresholds were widened.

The matching x86 scope passed 53 gates with 21 explicit Vulkan runtime shader
compiler/validation capability skips. Its DX11/DX12 automatic scene oracles and
all six hidden editor/Ray fallback gates passed, as did both native requirements
reports. The Vulkan editor continues to exercise capability fallback and recovery.
After the final private/shared target-namespace assertion, eight scene/editor
gates were rerun on each architecture: x64 passed all eight; x86 passed six with
the same two direct Vulkan capability skips. Both application builds and the
diagnostics-off product were rebuilt successfully after that change.

Diagnostic x64/x86 application builds and the diagnostics-off product build
succeeded. Evidence logs are `out/mme-shared-scenes-x64-acceptance.log` and
`out/mme-shared-scenes-x86-acceptance.log`; native inventories are generated in
the corresponding build directory. External Ray files remain local inputs.
