# Screen textures and postprocessing

## 1. General material target references

`ModelReplayDraw::targetTextures` names mutable RGBA8 render
products, separately from immutable `textures` upload keys. An active stage
selects exactly one source. A target reference includes its expected dimensions;
resize requires rebuilding the CPU packet. Normalized UVs allow input and output
dimensions to differ. Render targets contain linear UNORM bytes: implicit sRGB
reinterpretation is rejected. Attachment mip/layer/MSAA limits still apply.

The owning pass must declare each sampled target. Ordinary reads require an
earlier stored write within the same frame; explicit history is described below.
Same-pass feedback, unknown targets,
discarded/uninitialized contents and ambiguous bindings fail validation before
uploads. A bare replay stream cannot resolve render targets. Native handles
remain device/generation checked, with prepared-frame epochs invalidated on
reset and target rebuild. There is no implicit cross-frame history.

The resident renderer resolves these references only after whole-frame
validation, then binds them through the existing native texture APIs. Null
negative tests cover undeclared/feedback/stale-generation/reset reads;
`rhi_frame_passes` covers dimensions, source conflicts and whole-frame ordering.

## 2. Shared fullscreen pipeline

`AppendPostProcess` appends a validated, full-coverage RGBA8 pass, resolves its
input through the ordinary target contract and selects its output as `finalColor`.
The same HLSL is compiled for DX11/DX12 and Vulkan SPIR-V. There is no implicit
sRGB conversion, blending, depth testing or alpha replacement. Point-copy uses
an integer texel load, so equal-size identity copies must match every byte.
The color transform applies `input * multiply + add` to all four channels;
UNORM conversion occurs only at the output attachment. Linear resizing is
explicit, with normalized source rectangles and clamp addressing.

The public offscreen test covers alpha patterns, exact identity and chained
copies, RGBA transforms, asymmetric resize/orientation, and repeated submission.
The hidden presentation test runs two copy passes before the shared output
copy, including resize and device replacement. The CPU model/self-shadow test
also sends its main color through two exact copies before DX9 comparison.
No desktop window is shown by these tests.

## 3. Native screen capture and explicit history

The DX9 production walk draws accessories before `CaptureAccessoryScreenTexture`
(`src/render/frame_scene.cpp`). Capture happens after self-shadow/callback
composition and recording downsampling, before debug geometry and editor HUD.
`mme::PreRenderTargetCopy` precedes the copy. Consequently a screen accessory
reads the **previous successful capture**, rather than its current output.
`model_render` exercises the production accessory selector for all four modes.

| PMM value | Source seen by the accessory | End-of-frame capture |
| --- | --- | --- |
| 0: Disabled | Last capture; initially unbound | Frozen |
| 1: FullFrame | Previous capture | Full viewport |
| 2: CropFourByThree | Previous capture | Center 4:3 crop, native integer truncation |
| 3: BackgroundRefresh | Current AVI/background texture; unbound if absent | Frozen |

The original capture is single-level 1024-square X8R8G8B8, with a 512-square
fallback on allocation failure. The new helper defaults to 1024 and accepts an
explicit 512 setting; GPU allocation failure remains explicit. Captured alpha
is always one. `ScreenSourceRect` is shared with DX9 to retain the exact crop
arithmetic. The independent DX9 reference executes real `StretchRect`, without
presenting a window. Tests cover portrait/landscape, odd dimensions, enlargement,
reduction, mixed axis scaling, and both capture sizes. Enlargement uses the
measured extent-minus-one mapping per axis; reduction retains normalized edge
mapping. `LegacyScreenCapture` keeps this behavior separate from ordinary copies.
The filtered native comparison permits at most 2 bytes per channel and mean
absolute error at most 0.25 byte. Exact postprocess copies and repeated frames
continue to require byte-for-byte equality.

`FrameTarget::history` opts into persistent RGBA8 contents with an explicit
initial clear. Whole-frame validation still rejects feedback and discarded
history writes. SceneRenderer initializes history on allocation and epoch
change; ordinary targets still require a write in the same frame. History
allocations share the normal completion-token retirement, unload, and device
replacement rules. Portable first-use history is opaque black; before the first
successful capture the accessory remains unbound, matching native first-frame
ordering. Original uninitialized capture memory is not used as a visual oracle.

`ScreenTextureHistory` uses two scene-local targets. The main pass reads one,
and a pass after the selected final-color output captures into the other.
`Commit()` advances only after successful submission. Repeating a prepared frame
does not swap buffers. `RuntimeCpuSceneProducer::CommitFrame` and the independent
preview host follow this rule. Device replacement recreates defined black
history without touching CPU simulation. `screen.bmp` is a dynamic name in the
CPU X importer; it is never loaded from disk or represented by a placeholder
uploaded image. `RuntimeCpuSceneProducer::Build` rejects these scenes with
`screen_texture_requires_frame`; `BuildFrame` resolves their unbound accessory
stages before GPU submission.

`screen_texture_*` runs generated PMD/PMX PMM scenes with a real imported X
accessory, modes 0–3, and model self-shadow. `screen_texture_pmd_*` /
`screen_texture_pmx_*` use the existing opt-in external model environment
variables and add the generated screen accessory. `rhi_screen_capture_*` covers
native capture comparison, feedback-free history, freeze/background, repeated
submission, unload, device replacement, and the Dx9Measured pipeline option.

Current CPU PMM playback still rejects enabled AVI/picture backgrounds and
accessories with self-shadow enabled. The generic screen helper accepts an
explicit current background target; wiring video/image decoding into that target
remains separate. MME capture callbacks and recording/HUD integration belong to
their future producer paths. No arbitrary MME `.fx` execution, MRT or depth
sampling is advertised by this change. Portable trilinear remains the default;
native anisotropy and the existing WARP precision failure keep their own gates.

### Validation on this workstation

Windows, TITAN Xp; Release builds. The final portable resource suite passed
16/16. The x64 integration selection passed 38/38, including generated and real
PMD/PMX self-shadow comparisons, postprocess identity, capture, hidden WSI and
synchronization validation. Real PMD/PMX plus screen-accessory tests passed 6/6;
the public material-sampling gate and the expanded RGBA transform tests passed.
The x86 selection passed 24 tests, with 4 explicit skips because the 32-bit
Vulkan validation layer is unavailable. The Vulkan-only build passed all four
postprocess/capture tests, including synchronization validation. Hidden preview
tests also passed on both architectures with full-frame capture enabled.
Tests never show, activate, minimize or restore a desktop window.
