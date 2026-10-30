# Shared render products and sixteen graphics texture slots

`SharedFrameTargets` merges declarations by CPU resource revision identity.
The identity is independent of display names and native graphics handles.
Equal identities must have equal dimensions, format, mip/layer/sample counts and access;
conflicting declarations fail without replacing the existing target. Private
resources receive distinct identities even when their names match. A replacement
resource receives a fresh identity. The high target-ID namespace is reserved
for these canonical aliases.

Leases belong to one registry epoch. Reset, a new registry or a removed/changed
target makes resolution fail. Leases resolve into the existing typed render
target reference, separately from immutable uploaded texture keys. Contents
remain frame-local: sharing does not invent initialization or previous-frame
history. Existing frame/Null validation rejects read-before-write, attachment
feedback, dimension mismatches and stale native handles. Native prepared frames
retain their existing scene/device generation checks.

The generic graphics profile now exposes sixteen pixel texture/sampler slots
and four constant slots. This covers the local Ray trace's thirteen active
sampler registers. DX11 binds and clears all sixteen views; DX12 allocates
sixteen SRV descriptors per draw and reuses identical sampler tables inside
each completion-protected frame. The sampler heap stays within the portable
2048-entry limit; exhaustion fails explicitly. Vulkan generic bindings occupy
b0..3, t0..15 at bindings 4..19 and s0..15 at bindings 20..35. Runtime compilation,
reflection, descriptor pools and built-in generic SPIR-V use the same ABI.
The dedicated legacy model/shadow shader ABI remains unchanged.

`rhi_shared_targets` exercises aliasing, declaration conflicts, epoch invalidation,
frame initialization/feedback and Null stale handles. The generic shader image
fixture uses t15/s15 and samples sixteen distinct textures simultaneously;
DX11/DX12/Vulkan and their native validation variants retain
the existing matrix, alpha, rebuild and strict-copy checks.
