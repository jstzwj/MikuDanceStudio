# RHI interface contract (long-term design and current narrow adapter)

The implemented attachment/pass subset and acceptance matrix are maintained in
[RHI feature support](RHI_FEATURE_SUPPORT.md). That executable subset takes
precedence over the long-term design below.

This contract is the target for incremental extraction; `rhi.hpp` still exposes
the earlier, smaller `Device` API. Do not route the MMD application through this
design until an isolated DX9 batch agrees with the legacy path and later scene
captures agree with real PMM frames. Unity 2021.3's `Runtime/GfxDevice` device,
threaded client, setup, and backend directories informed the separation of
responsibilities, not these type names or implementation details. No Unity
code is copied.

## Ownership and API boundaries

| Owner | Proposed interface | Responsibility | Must not own |
| --- | --- | --- | --- |
| Host | `PlatformSurface` | Borrow the native window/view and report pixel size; platform-specific handle stays outside the portable core. | PMM document, GPU state |
| Backend factory | `CreateDevice(Backend, PlatformSurface, DeviceOptions)` | Select an explicitly requested backend, probe `Capabilities`, return a device or a diagnostic failure; no silent backend substitution. | Frame state |
| Device | `CreateBuffer`, `CreateTexture`, `CreateSampler`, `CreatePipeline`, `Destroy`, `Status` | Own generation-tagged GPU resources, immutable pipeline descriptions, backend shader variants, and device epoch. Reject stale/wrong-device handles. | OS window lifetime, scene/animation |
| Swapchain | `Resize`, `Acquire`, `Present` | Own presentation images, color space, size, vsync, and out-of-date detection. A presentation image is valid for one acquired frame. | Model buffers, animation |
| Encoder | `BeginPass`, `BindPipeline`, `BindVertexBuffer`, `BindIndexBuffer`, `BindResources`, `DrawIndexed`, `EndPass`, `Finish` | Record ordered, validated commands and explicit pass load/store/clear operations. The encoder has one owner and one submission; no implicit global current device. | Device reset, PMM parsing |
| Queue | `Submit(finishedCommands) -> Fence`, `Wait(Fence)` | Submit and retire work. DX9 may execute immediately; DX12/Vulkan/Metal may use native command buffers and fences. | Renderable scene data |
| Recovery coordinator | `OnDeviceLost`, `RecreateDevice`, `RebuildResources` | Stop submissions, wait or discard in-flight work as appropriate, recreate swapchain and GPU resources from retained CPU descriptors/data, then resume. | Silent recreation of MME resources |

The portable `SceneSnapshot` describes what to draw, not a GPU resource or
render command. A future render-data builder converts model data and snapshot
draw order to immutable mesh/material references; the renderer resolves those
references through its own resource cache. IK, physics, and MME stay in the
legacy application while each family is compared.

The proposed call sequence is `Acquire -> BeginCommands -> BeginPass(target,
load/store, clear) -> BindPipeline/resources -> DrawIndexed -> EndPass ->
Finish -> Submit -> Present`. A submission returns a fence that keeps referenced
resources alive until retirement. Destroy during encoding or pending GPU use
must be deferred or rejected, never invalidate commands already submitted.
Multiple devices may coexist; a handle identifies its owning device as well as
its generation. `Status` distinguishes healthy, surface-out-of-date, device-lost,
and unrecoverable. Recreating a device invalidates its old epoch and all its
handles. CPU copies of PMM/model data survive; GPU allocations and DX9 default-
pool objects do not. An MME effect's reset and shader lifecycle remain a
separate compatibility milestone.

The next interface revision should be expressed as separate, non-copyable
objects with roughly these signatures (illustrative C++17 contract, **not**
declarations provided by the current `rhi.hpp`):

```cpp
struct ResourceHandle { uint32_t device, epoch, slot, generation; };
struct PipelineDesc { /* topology, layouts, shaders, depth, blend, targets */ };
struct PassDesc { /* attachments, load/store, clear, sample count */ };
struct FrameImage { ResourceHandle color; uint64_t acquireSerial; };
enum class DeviceStatus { Ready, SurfaceOutOfDate, Lost, Fatal };

class Device {
public:
    virtual Capabilities GetCapabilities() const = 0;
    virtual Result<ResourceHandle> CreateBuffer(const BufferDesc&, ByteSpan) = 0;
    virtual Result<ResourceHandle> CreatePipeline(const PipelineDesc&) = 0;
    virtual Result<std::unique_ptr<Swapchain>> CreateSwapchain(PlatformSurface) = 0;
    virtual Result<void> Destroy(ResourceHandle) = 0;
    virtual Result<std::unique_ptr<CommandEncoder>> BeginCommands() = 0;
    virtual Result<Fence> Submit(FinishedCommands&&) = 0;
    virtual DeviceStatus Status() const = 0;
};
class CommandEncoder {
public:
    virtual Result<void> BeginPass(const PassDesc&) = 0;
    virtual Result<void> BindPipeline(ResourceHandle) = 0;
    virtual Result<void> DrawIndexed(const IndexedDraw&) = 0;
    virtual Result<void> EndPass() = 0;
    virtual Result<FinishedCommands> Finish() = 0;
};
class Swapchain {
public:
    virtual Result<FrameImage> Acquire() = 0;
    virtual Result<void> Present(FrameImage, Fence) = 0;
    virtual Result<void> Resize(uint32_t width, uint32_t height) = 0;
};
```

`Result`, `ByteSpan`, `Fence`, and the complete descriptors are deliberately
uncommitted until DX9 pipeline/pass tests establish their semantics. Frame
images cannot be copied or presented twice. Cross-device resource binding is
invalid. A fence can be polled or waited without requiring a window handle.
The recovery coordinator uses the device status and a host-owned CPU resource
registry; it does not hand a lost `IDirect3DDevice9` to a new application
session or quietly mutate the old handles.

The experimental `rhi.hpp` implements a narrow fixed-function `LineList`
pipeline with `ScreenPositionColor` (20-byte XYZRHW + color), and a textured
`TriangleList` pipeline with `ScreenPositionColorUv` (28-byte XYZRHW + color
+ UV). Both use non-indexed `Draw`; their state is set explicitly.
`PrepareForReset` unbinds adapter vertex and
index buffers, invalidates handles to dynamic default-pool buffers and drops
their allocations; the **host** calls DX9 `Reset` and releases its other
default-pool resources. `RestoreAfterReset` resumes only after the device is
ready. Managed buffers and uploaded textures remain valid across that reset.
Generation and device
identity reject stale and foreign handles. This is not an implementation of
the proposed generic pipeline/encoder/swapchain interfaces.

## Pipeline and format contract

`PipelineDesc` must explicitly define topology, vertex attributes/stride,
index format, shaders, depth/stencil, rasterization, blending, render-target
formats, sample count, and winding. `SamplerDesc` defines addressing and
filtering; immutable binding layouts specify texture/uniform slots. A shader
package contains separately compiled backend variants; no automatic HLSL-to-
Metal/Vulkan translation is promised. Validate unsupported combinations using
capabilities before recording. DX9 fixed-function materials are a distinct
pipeline variant, not a shader that other backends claim to implement.

Use an explicit clip-space/depth and matrix convention at the render-data
boundary; do not change PMM or bone-matrix conventions to satisfy a backend.
Render passes expose load/store and attachment ownership, including transient
MSAA resolve. Barriers and descriptor heaps are backend details initially;
the portable contract states read/write usage and synchronization dependencies
that DX12/Vulkan can compile to native transitions. If those dependencies
cannot express a required operation, extend the contract rather than hiding a
full-device stall.

## Migration gates

1. **Now:** Keep `rhi.hpp` experimental. Test one independent `PositionColor`
   indexed triangle against the same native DX9 device's direct path and
   compare all readback pixels and a non-clear interior pixel. Neither path
   touches the MMD application's device. Also retain headless Null validation.
   The isolated parity test passed in both x64 and x86 on the current Windows
   machine: 4,096 equal pixels and a red center pixel; this is not an MMD
   model draw.
   The DX9 line fixture reproduces the selection-box producer's four white
   line segments: x64/x86 pixel readback comparisons must have zero differences
   and at least one non-clear pixel. `rhi_d3d9_test` exercises a real device
   `Reset` and verifies that managed buffers survive while old dynamic handles
   fail. The Null backend checks the same handle lifecycle without a GPU.
2. **After isolated parity:** Capture an actual application's selection-line
   batch, replay it against both DX9 paths, and compare render state, targets,
   and pixels. The isolated replay now reproduces the captured target size,
   viewport, 8-sample color/depth surfaces, and inherited DX9 states. The
   depth-dependent pixel difference found with the initial adapter has been
   resolved in isolated replay (details below). Validate native and adapter
   pixels against the same live in-app depth/stencil contents before extracting
   any app draw family. Design pipeline/pass/swapchain ownership and device-loss
   orchestration before adding an opt-in switch; leave the old path as its
   immediate fallback.
3. **After real PMM parity:** Add DX11 with explicit shader/material variants,
   then DX12 with queue/fence/barrier semantics. Vulkan and Metal follow the
   same data boundary independently. Do not claim cross-backend parity from a
   Null test or a synthetic triangle.

CTest returning `77` because DX9 device creation fails is **not** a pass for
GPU pixel equivalence. It means the test was unavailable on that machine;
only headless contract tests and successful compilation remain verified.

The optional `rhi_real_selection_gui` CTest uses the real v1 `sample.pmm` when
`MDS_PMM_GUI_FIXTURE` points to it and `MDS_PMM_RELOCATE_MODEL` names a model
to relocate. It completes the missing-model UI reselect, focuses the window,
drags a nonempty bone selection, captures `line_selection.bin` (160 bytes) and
`line_selection.state`, and replays the bytes against independent native DX9
and adapter devices. The state file includes target/depth formats and sample
quality, viewport, FVF, stream layout, blend, depth, cull, lighting, color and
alpha stage, scissor, and color-write values. Both x64 and x86 Release runs
produced zero pixel differences and non-clear pixels with the **captured
vertices and state on an isolated 1920x1080 8-sample target**. The synthetic
parity and device-reset tests also passed. Without a licensed real
fixture or working GUI/GPU, CTest skips rather than reporting parity.

At the actual selection draw, both architectures reported a 1920x1080
`A8R8G8B8` target (`format=21`), 8-sample MSAA, D24S8 depth (`format=75`),
FVF 68, stride 20, and texture 0. The old path inherited `lighting=1`,
`cull=3`, `depth=1`, `depthWrite=1`, `depthFunc=4` (less/equal), `blend=1`,
`source=5`, `destination=6`, `colorOp=4`, `colorArg=2`. The initial adapter
used `lighting=0`, `cull=1`, `depth=0`, `depthWrite=0`, `colorOp=2`, and
`colorArg=0`. A controlled depth probe exposed this mismatch: with line Z
at 0.5 and depth cleared to 0, the old path drew no colored pixels while
the initial adapter drew 192 (x64) or 66 (x86) pixels. The current selection
pipeline reproduces the captured states; both paths now draw zero colored
pixels in that probe.

The selection pipeline now explicitly requests `LessEqual` depth test and
write, counterclockwise culling, lighting, the inherited color/alpha stage,
alpha test, and the captured stencil test/write. The isolated native and
adapter replays also compare all captured render and stage states. On the
actual v1 PMM data, normal and deliberately depth-occluded lines both have
zero differing pixels on x64 and x86. The earlier depth-occlusion divergence
was resolved by matching the inherited state, not by disabling depth.

`MDS_RHI_LIVE_COMPARE_DIR` enables a one-shot in-application diagnostic that
resolves the live 8-sample target before and immediately after the legacy
`mme::DrawPrimitive` call, then again after `mme::EndScene`. It records target
hashes, changed pixels inside the selection rectangle, bridge call counts,
and MME host state. The bridge is invoked once through its host path. The
host may forward directly when `endSceneFired=1`, as observed in this PMM.
The initial zero-pixel captures were one frame early. `PrepareFrameLineOverlay`
writes the rectangle after rendering for the following frame; when the test
first moved the mouse, the live VB still contained eight copies of the old
top-left point. The diagnostic now checks the actual eight VB vertices before
starting its one-shot capture. Real v1 PMM runs on x64 and x86 each changed
192 pixels in the box immediately after the old line draw and recorded one
MME host bridge call. The positive-control occlusion probes confirmed that
the lines were not rejected by depth or stencil.

`MDS_RHI_LIVE_AB=1` performs an application-internal comparison. It copies
the current 8-sample color target twice in the same frame, draws the same
captured line batch through the legacy DX9 path and borrowed-scene RHI adapter,
resolves both copies, and compares all 2,073,600 pixels. Depth and stencil
tests use the application's current surface; writes are disabled in both
probes so they cannot affect each other or the actual draw. The same-frame
test passed with zero differences and 192 changed selection pixels on x64
and x86. Each actual selection draw passed through MME once. A separate
two-frame comparison logs background changes, but its whole-frame hashes are
not a parity gate because the scene can change between frames.

`MDS_RHI_SELECTION_ADAPTER=1` opts the application into the borrowed-scene
adapter for the four-line selection rectangle only. The default remains the
legacy path. The adapter creates its own managed VB, applies the explicit
fixed pipeline, routes the draw through the existing MME bridge, and restores
the captured D3D9 state. If setup fails before a successful draw, the old
path executes. The `rhi_real_selection_optin_gui` CTest exercises this path
with the real v1 PMM on x64/x86. Model, accessory, shadow, and other overlay
batches remain on their existing render paths; this narrow gate does not
establish parity for those draw families or for other graphics backends.

## Portable texture upload and textured overlay batches

The implemented `Device` surface now has `CreateTexture`, `UploadTexture`,
`BindTexture`, and `Destroy`. `TexturePixels` contains RGBA8 bytes, row pitch,
and buffer length. Upload checks extent and pitch before copying rows; DX9
converts RGBA to its native A8R8G8B8 byte order. A texture must be uploaded
before binding. A handle carries device identity and generation, so a
destroyed or foreign texture cannot be rebound. DX9 uses a managed texture,
which survives device reset; dynamic default-pool buffers are invalidated.
The Null backend enforces the same binding and lifetime rules.

`DrawSpriteOverlay` uses screen-space XYZRHW, diffuse color, UV (`28` bytes per
vertex), and `TriangleList`. The real v1 PMM emitted eight triangles. Captured
D3D9 state used the selection pipeline's depth, stencil, alpha and blend
settings, plus anisotropic min/mag/mip sampling at level 16 and wrap
addressing. In the optional adapter path, decoded RGBA bytes are uploaded to
an RHI-owned texture and reused while the atlas revision remains unchanged.
The state block restores the application's state; the draw still crosses the
MME bridge once.

`MDS_RHI_SPRITE_AB=1` copies the application's 8-sample color target twice
in one frame and compares a direct DX9 draw against the adapter with the
current depth/stencil surface held read-only. On real `sample.pmm`, x64 and
x86 both compared all 2,073,600 pixels with zero differences. The batch
changed 4,891 pixels on x64 and 3,631 on x86, and the actual MME triangle
bridge ran once. The x86 application's actual target differed by two pixels
from each read-only probe, on both the legacy and opted-in adapter paths;
that number is logged separately because the real draw can write depth and
stencil while the probes cannot. It is not counted as an adapter pixel
difference.

`MDS_RHI_SPRITE_ADAPTER=1` opts in only this HUD sprite batch. Unsupported
sampler states, unreadable textures, or setup failure fall back to the
original draw; a successful draw is never repeated if state restoration later
fails. `MDS_RHI_TEXT_ADAPTER=1` applies the same owned-texture path to the text
overlay. `MDS_RHI_TEXT_AB=1` compares its old and adapter draws on copied
targets in the same frame, while `MDS_RHI_SPRITE_AB=1` does so for HUD sprites.
The actual draw and its MME bridge count are checked separately. The real v1
`sample.pmm` GUI tests for both batches and the selection rectangle passed on
x64 and x86 with zero same-frame pixel differences, including the opt-in runs
and a 1080x2300 target. The captured viewport could extend four pixels past
that target; the independent replay keeps the real viewport and relies on DX9
clipping. The adapter can be disabled at CMake configuration with
`MIKUDANCESTUDIO_RHI_D3D9=OFF`; the app then keeps the legacy paths.
The x64 Release application and Null backend test also build and pass with
that option off. On Linux, the same portable `rhi.hpp`, `rhi_null.cpp`, and
`rhi_null_test.cpp` compile with GCC in C++17 mode and `-Wall -Wextra -Werror`; the
texture upload, binding, stale-handle, and reset test passes without a window.

## Atlas source and reuse

The HUD PNG resource is decoded to tightly packed RGBA8 when the Windows UI
loads it. The decoder (`rhi_image.cpp`, stb_image v2.30, dual MIT/public-domain)
has no Win32 or DX9 dependency and is covered by a fixture hash test. The
GDI-generated text atlas retains RGBA8 bytes at the same time it fills the
legacy DX9 texture. Optional adapter draws consume those CPU bytes directly;
they do not lock and read back the DX9 texture during normal drawing.

Each live DX9 device has a borrowed-scene cache with separate sprite and text
slots. A slot's revision and dimensions select an existing RHI-managed texture;
a changed revision uploads a replacement. The cache is released before device
reset and during application shutdown. The diagnostic compares each CPU atlas
against its legacy DX9 texture byte for byte; real v1 `sample.pmm` produced
zero source differences on x64/x86. The opt-in GUI tests also recorded one
upload followed by a cache hit for each slot, while same-frame draw comparison
remained pixel-identical. `rhi_image` passes on Windows and Linux (GCC C++17,
`-Wall -Wextra -Werror`). These atlases still originate from the Windows UI's
PNG resource and GDI glyph drawing; portable text shaping and a platform
independent asset provider remain separate work.

## Asset IDs and experimental font source

`AssetSource::Read(AssetId, bytes)` separates resource identity from storage.
`CallbackAssetSource` accepts the Windows PNG resource callback (IDs `0x66`
HUD and `0x72` accessory helper); `FileAssetSource` maps those IDs to fixed
filenames in a caller-selected directory. Both feed `LoadAtlas`, which decodes
RGBA8 and hashes image content and dimensions into a stable nonzero revision.
`AtlasTextureCache` uploads once per ID/revision, replaces changed textures,
and explicitly invalidates before device reset, allowing reupload afterward.
The portable `rhi_assets` test runs the complete file/callback/read/decode/
upload/bind/reuse/replace/reset/reupload sequence with the Null backend on
Windows and Linux. The existing live DX9 overlay cache uses this same image
revision but remains a borrowed-scene adapter rather than a generic render
pass implementation. Its unchanged sprite/text batches continue to produce
zero same-frame pixel differences on x64/x86 real `sample.pmm`.

The GDI atlas uses Arial in English mode and MS PGothic in Japanese mode
(`DrawGlyph`, 30-pixel bold, Shift-JIS); neither Windows system font is copied
into this repository. The experimental replacement uses an **unmodified**
Noto Sans JP variable font from the official `google/fonts` `ofl/notosansjp`
distribution (font SHA-256 `c2f3b4d463500a2ddcd3849cded1fceeb9fd6d1c32e6cbecd568453ba50fc68f`).
Upstream font and license files were obtained from
`https://github.com/google/fonts/tree/main/ofl/notosansjp`; retain the hash
check because the `main` branch is not a release pin.
The source font is under SIL Open Font License 1.1; its required notice is
retained at `res/fonts/NotoSansJP-OFL.txt`. No font binary is bundled.
`scripts/gen_portable_font_atlas.py` reads the existing Shift-JIS glyph list,
uses Pillow/FreeType at size 30 and weight 700, and generates the committed
`res/assets/scene_font_noto.png` (logical asset ID `0x100`, not a Win32
resource). Regenerate with
`python scripts/gen_portable_font_atlas.py --font NotoSansJP.ttf --output
res/assets/scene_font_noto.png`; the script rejects a different font hash.
This is a fixed atlas matching the current glyph layout, **not** portable
dynamic text shaping. Noto's glyph contours/rasterizer are not expected to
match GDI's Arial/MS PGothic fallback.

Set `MDS_RHI_PORTABLE_FONT=1`, `MDS_RHI_PORTABLE_FONT_DIR=<asset directory>`,
and `MDS_RHI_TEXT_ADAPTER=1` to opt into drawing this PNG through the cached
RHI texture. Without all three, the legacy GDI-derived source stays active.
The diagnostic writes `text_overlay.portable_source` and leaves the normal
GDI source check intact. On real v1 `sample.pmm` at x64 and x86, the candidate
differs by **45,860 of 1,048,576 RGBA channels** from the GDI atlas. The
`rhi_real_text_portable_candidate_gui` test asserts a measured nonzero source
delta; the normal `rhi_real_text_optin_gui` remains the zero-difference
regression gate. The candidate is not the default and is **not claimed to be
pixel-identical** to legacy output. Font selection, Japanese/English fallback,
and dynamic glyph rendering require separate parity decisions.

Model material preparation, the first gated DX9 batch, and actual three-platform
resource CI results are documented in [RHI_MODEL_BATCH_VALIDATION.md](RHI_MODEL_BATCH_VALIDATION.md).
The later [Base/Sphere resource validation](RHI_MATERIAL_TEXTURE_VALIDATION.md)
defines material input roles, transforms, samplers, alpha sources and Mipmap
policy, with four configured x64/x86 pixel comparisons and an explicit blocked
106x106 -> 128x128 legacy resize case.
