# Generic graphics shader profile

`ShaderProgram` is an immutable CPU description, with VS/PS source or SPIR-V,
explicit vertex inputs and binding layout. Native objects and caches belong to
the device. Replacing a device recreates them from the CPU description.

The initial profile supports one RGBA8 or R32Float color attachment, the existing
depth/stencil attachment, four constant slots (`b0..b3`, shared between stages),
four pixel texture slots (`t0..t3`) and four pixel samplers (`s0..s3`). Constants
have an explicit byte size and optional named members. Matrices are row-major;
arrays start on a 16-byte boundary. Constant blocks are multiples of 16 bytes.
Vertex texture reads, descriptor arrays, UAVs, MRT and depth sampling are outside
this profile and must be rejected rather than silently approximated.

The Vulkan ABI maps constants to bindings 0..3, images to 4..7, samplers to 8..11
in set 0. HLSL uses the same register numbers as D3D. Native reflection checks
the compiled bindings against the description. Layouts can declare optimized-out
bindings, but callers must still supply the complete declared layout.

Uploaded textures and pass outputs retain their existing distinct ownership and
initialization rules. Every binding is revalidated at draw time. A target must
be declared sampled in the active pass, initialized and stored by a preceding
pass. Sampling the current attachment, stale handles and prior reset submissions
are rejected. Constant snapshots are CPU-owned; native uploads live until the
submission completes. Null tests exercise this contract without a graphics device.
