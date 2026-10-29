# Screen textures and postprocessing

## 1. General material target references

`ModelReplayDraw::targetTextures` names frame-local mutable RGBA8 render
products, separately from immutable `textures` upload keys. An active stage
selects exactly one source. A target reference includes its expected dimensions;
resize requires rebuilding the CPU packet. Normalized UVs allow input and output
dimensions to differ. Render targets contain linear UNORM bytes: implicit sRGB
reinterpretation is rejected. Attachment mip/layer/MSAA limits still apply.

The owning pass must declare each sampled target. Reads require an earlier
stored write within the same frame. Same-pass feedback, unknown targets,
discarded/uninitialized contents and ambiguous bindings fail validation before
uploads. A bare replay stream cannot resolve render targets. Native handles
remain device/generation checked, with prepared-frame epochs invalidated on
reset and target rebuild. There is no implicit cross-frame history.

The resident renderer resolves these references only after whole-frame
validation, then binds them through the existing native texture APIs. Null
negative tests cover undeclared/feedback/stale-generation/reset reads;
`rhi_frame_passes` covers dimensions, source conflicts and whole-frame ordering.
