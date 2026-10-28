# RHI feature support

The baseline includes main v10.11 (`4648314`), including restoration of omitted
EMM assignments. Support means implemented behavior with a named acceptance
test; an API's theoretical capabilities do not imply adapter support.

| Contract / feature | Null | DX9 editor | DX11 | DX12 | Vulkan |
| --- | --- | --- | --- | --- | --- |
| CPU PMD/PMX scene, IK, Bullet and PMM evaluation | CPU validation | Native | Independent CPU producer | Independent CPU producer | No backend |
| Resident immutable geometry/material versions | Validation | Native | Implemented | Implemented | No backend |
| Single RGBA8 + D24S8 model frame | Validation | Native | Implemented | Implemented | No backend |
| Explicit sequential passes and sampled attachment dependencies | `rhi_render_pass` / `rhi_frame_passes` | Native implicit state | Generated-scene gate | Generated-scene gate | No backend |
| R32 float shadow color + independent D24S8 attachment | Clear/read validation | Built-in effect | Generated-scene gate | Generated-scene gate | No backend |
| Built-in self-shadow generation and composition | No rasterization | Native | CPU PMD/PMX producer; strict DX9 gate open | CPU PMD/PMX producer; strict DX9 gate open | No backend |
| Multiple simultaneous color attachments (MRT) | Rejected | Native API only | Not exposed | Not exposed | No backend |
| Mips / layers / MSAA on explicit render textures | Rejected | Separate legacy paths | Rejected | Rejected | No backend |
| Native MME `.fx` execution | Unsupported | Implemented | Unsupported | Unsupported | No backend |
| Dynamic screen texture / post-processing | Dependency foundation | Native | Not yet integrated | Not yet integrated | No backend |

## Explicit pass rules

`rhi_render_pass.hpp` describes render textures separately from immutable uploaded
material textures. Access is declared at creation: color attachment, depth/stencil
attachment, sampled, and copy source. The current subset is one mip, one layer,
one sample, at most one color attachment and one D24S8 attachment per pass.
RGBA8 and R32Float color targets may be sampled; D24S8 sampling is not advertised.
No implicit resolve, format substitution or depth/color history transfer occurs.

Passes execute in declaration order on one graphics queue. Each pass names all
sampled render textures, and specifies load/clear/discard and store/discard for
its attachments. Load and sampling require initialized contents. A clear defines
the complete attachment; discarded loads cannot promise complete writes and must
also discard their result in this subset. Simultaneous attachment write and
sampling is rejected. Matching dimensions are required for color/depth pairs.

`RenderPassState` validates the same rules in Null and native adapters. Rejected
passes do not commit content state. Native barriers and view unbinding follow
the accepted accesses. Bindings are reset at pass and pipeline boundaries.
Creation, destruction, resize/reset, and readback cannot occur within a frame.
Readback requires initialized contents and CopySource access. Reset invalidates
contents; device identity, handle generation and submission epoch prevent stale
work from being reused. Texture content is never treated as preserved just
because an allocation survived a resize.

## Acceptance and review units

1. Portable attachment/dependency/lifetime contract and Null negative tests.
2. DX11/DX12 generated shadow map, composition, readback, resize/unload/recovery.
3. Real PMD/PMX CPU scene producer, built-in self-shadow, DX9 comparisons.

`RenderFrame` adds ordered passes to the resident `SceneRenderer`. Immutable
geometry/material uploads remain shared across passes. Mutable targets have a
separate scene ID namespace, are retired with GPU completion tokens on resize or
unload, and are rebuilt from CPU descriptions after device replacement. Whole
frame validation precedes allocations and submissions. The earlier single-target
`ModelReplayStream` remains supported and cannot silently execute shadow draws.

`CreateD3D11OffscreenDevice` and `CreateD3D12OffscreenDevice` need no HWND or
swapchain; their returned bundle has a device and a null swapchain. The generated
`rhi_multipass_*` tests check actual R32 shadow coverage, lit and shadowed pixels,
100 resident submissions, target resizing, stale commands/prepared frames,
unload, replacement and (DX12 WARP only) isolated native device removal/recovery.
The test colors avoid UNORM half-byte rounding boundaries; no image tolerance is
introduced. All five x64 generated-scene variants passed on this workstation,
including hardware, WARP and the DX12 debug layer. Portable resource tests: 15/15.

The built-in shadow shader is a separate program from fixed-function model
emulation. Its contract follows the stock effect's depth plot and shadow
comparison (1500 / 8000, perspective Y factor, 0.3 threshold), material/toon and
specular inputs. This is not a native MME effect interpreter. Real material
coverage and the remaining strict DX9 differences are recorded in
[self-shadow validation](RHI_SELF_SHADOW_VALIDATION.md). Explicit-pass
HUD/fixed pipelines, D24S8 readback on DX12, UV1 and material morph shading are
not accepted by this milestone's native program.

Tests for this work must be offscreen: no visible window, focus changes, desktop
input, minimize/restore calls, or editor automation. GPU failures and unavailable
devices must be reported separately. The inherited `rhi_d3d9_null_texture_warp`
one-channel precision failure remains independent: do not alter its tolerance or
baseline to accept this work. Image comparisons continue to require exact bytes
where the corresponding gate promises exact output.

`RuntimeCpuSceneProducer::BuildFrame` produces the explicit shadow/main frame.
It shares the native DX9 CPU shadow projection, preserves PMX caster/receiver
flags and outline colors, and reuses geometry versions between passes. An empty
caster pass clears and stores a white map. Unsupported self-shadow accessories,
UV1 and material morph shading fail explicitly. The legacy preview controller
still consumes its single-target stream; presenting the new frame through that
controller is a separate integration step, not an implicit fallback to DX9.
