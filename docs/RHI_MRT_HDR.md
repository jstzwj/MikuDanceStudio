# Ordered color attachments and HDR intermediates

The graphics contract supports dense ordered color attachments, mapped to
`SV_Target0..N-1`. A pass uses `colors`; `color` remains the single-attachment
compatibility spelling and combining both is invalid. Four is the portable
contract ceiling; each backend advertises its implemented limit separately.
All attachments share extent and sample count. Duplicate writes, undefined
loads, discarded reads and feedback are rejected before native recording.

Shader programs declare each pixel output's float component count. Pipelines
declare the ordered attachment formats, and compiled reflection must match
the declared output layout. The same blend state and color mask apply to all
attachments; independent blend operations are outside this batch.

`FormatFeatures` reports render, sampling, blend and raw readback support per
format. `RGBA16Float` uses four IEEE binary16 components, eight bytes per pixel.
Readback preserves the values and does not normalize or clamp them through an
RGBA8 image. Clear values must be finite; RGBA16F clears must fit its finite
range, while RGBA8 retains its [0,1] rule. The final presentation target remains
RGBA8; tone mapping is an explicit shader pass.

Null verifies attachment/layout/lifetime contracts and clear/readback values,
without executing shaders. Its regression exhaustively round-trips finite
binary16 values and checks ties-to-even conversion. Native execution and the
minimal MME scene profile are introduced in the following implementation steps.
