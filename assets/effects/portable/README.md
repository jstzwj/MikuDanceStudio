# Portable MME examples

These original examples are dedicated to the public domain under CC0-1.0.
They contain no external model or texture assets.

- `MaterialTint.fx`: assign to a textured PMD/PMX material or object. It uses
  material diffuse, world/view/projection and a live `tint` parameter. MME's
  existing technique selector handles object/object_ss, Subset and UseTexture.
  Materials without a matching technique retain their ordinary draw.
- `TwoStage.fx`: assign to one scene carrier, usually a small accessory. Its
  script captures the scene at `ScriptExternal`, grades it into Ping, then
  copies Ping to the final color target. Editor overlays follow this chain.

Both files also run as ordinary DX9 effects. `MDS_RHI` selects their HLSL 5/6
shader branch. Explicit profile and pass annotations opt into the initial
portable ABI. Changing the shader functions within that ABI is supported;
adding unrecognized parameters, pass state, resources or script commands
selects the editor's same-frame DX9 fallback.

See [the supported profile and validation](../../../docs/RHI_MME_PORTABLE.md).
