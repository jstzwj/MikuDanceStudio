# Ordered color attachments and HDR intermediates

The graphics contract supports dense ordered color attachments, mapped to
`SV_Target0..N-1`. A pass uses `colors`; `color` remains the single-attachment
compatibility spelling and combining both is invalid. Four is the portable
contract ceiling; each backend advertises its implemented limit separately.
All attachments share extent and sample count. Duplicate writes, undefined
loads, discarded reads and feedback are rejected before native recording.

Shader programs declare each pixel output's float component count. Pipelines
declare the ordered attachment formats, and compiled reflection must match
the declared output layout. RGBA attachments require float4 outputs so missing
alpha never acquires backend-dependent values; R32 accepts one to four float
components and stores the first. The same blend state and color mask apply to all
attachments; independent blend operations are outside this batch.

`FormatFeatures` reports render, sampling, blend and raw readback support per
format. `RGBA16Float` uses four IEEE binary16 components, eight bytes per pixel.
Readback preserves the values and does not normalize or clamp them through an
RGBA8 image. Clear values must be finite; RGBA16F clears must fit its finite
range, while RGBA8 retains its [0,1] rule. The final presentation target remains
RGBA8; tone mapping is an explicit shader pass.

Null verifies attachment/layout/lifetime contracts and clear/readback values,
without executing shaders. Its regression exhaustively round-trips finite
binary16 values and checks ties-to-even conversion.

DX11, DX12 and Vulkan now query native format features, bind all ordered
attachments, include every color format in pipeline keys and copy raw texels
on readback. Compiled HLSL/SPIR-V reflection checks the output count, locations
and float component counts before a program can execute.

`rhi_mrt_hdr_test` runs the same generated two-output scene, two-texture HDR
combine, half-float copy, tone mapping and RGBA8 copy on all three backends.
Exactly representable MRT constants and both copy passes require exact values.
HDR arithmetic allows one binary16 ULP (native target conversion can truncate);
tone mapping allows one UNORM8 step. Negative values, values above one, alpha,
raw eight-byte texels, repeated submissions, resize and device replacement
are checked. A mixed RGBA16F/RGBA8 pass checks additive blending, signed clears
and PSO format changes, including UNORM source clamping before blending.
DX12 debug and Vulkan validation variants also check native errors.

The bounded public `mrt-hdr-v1` MME scene profile is described in
[portable MME](RHI_MME_PORTABLE.md). Its CPU graph transports attachment formats,
parameters and ordered pass inputs/outputs without native handles. It does not
translate general Ray scripts, arbitrary FX9 shaders or parameter semantics.

Local validation (2026-10-29): the x64 acceptance scope reached 66 passing cases,
including Null contracts, seven MRT/HDR variants, three public FX/DX9 oracles,
generic shaders, strict postprocess copies, dynamic buffers, screen capture,
hidden editor ownership/fallback, all three real Ray fallback gates, local
Sour Miku PMX imports and generated self-shadow chains. The focused x86 scope
reached 22 passes and five explicit skips for unavailable runtime DXC or Vulkan
validation-layer capabilities. Both architecture GUI builds, diagnostics-off
product builds and the build with newer backends disabled compiled successfully.

DX9 initialization was intermittently unavailable during the broad runs:
the initial x64 run had 65 passes and one CreateDevice failure; the initial
x86 run had 15 passes, five skips and seven failures before any frame executed.
Errors were 0x8876086C/0x8876086A. Pre-existing standalone DX9 controls subsequently
passed, and all affected cases passed unmodified isolated reruns. This device
availability instability remains recorded; no test was changed to skip those
failures, and no image tolerance was relaxed to obtain a pass. All test hosts
remained offscreen or hidden; no editor desktop window was opened.
