# Portable MME examples

These original examples are dedicated to the public domain under CC0-1.0.
They contain no external model or texture assets.

- `MaterialTint.fx`: assign to a textured PMD/PMX material or object. It uses
  material diffuse, world/view/projection and a live `tint` parameter. MME's
  existing technique selector handles object/object_ss, Subset and UseTexture.
  Materials without a matching technique retain their ordinary draw.
- `LegacyMaterial.fx`: unmodified-style FX9 model material without a profile
  or modern shader branch. It exercises automatic declaration/sampler/I/O
  translation, current draw matrices, arrays, bools and live shared/control
  values. See [the bounded importer](../../../docs/RHI_MME_LEGACY_IMPORT.md).
- `TwoStage.fx`: assign to one scene carrier, usually a small accessory. Its
  script captures the scene at `ScriptExternal`, grades it into Ping, then
  copies Ping to the final color target. Editor overlays follow this chain.
- `MrtHdr.fx`: assign to one scene carrier. Split writes two RGBA16F layers,
  Combine samples both into HDR, ToneMap writes RGBA8, then Finish copies it
  exactly. Signed/highlight values and live multiply/add parameters are kept
  until explicit tone mapping. It uses the bounded `mrt-hdr-v1` profile.
- `ControlMaterial.fx` / `ControlPost.fx`: live CONTROLOBJECT position/world,
  a shared exposure parameter, float2 arrays and a bool. Assign them to a
  model and a scene accessory to exercise pool sharing and controller updates.
- `IncludedControlPost.fx`: includes ControlPost.fx; loading snapshots both
  source variants so backend/device changes retain the loaded include revision.
- `SceneScript.fx`: one scene accessory using the `scene-v2` profile. Its SAS
  script captures after an explicit clear, grades, repeats Lift using live
  LoopByCount/LoopGetIndex, and copies Pong exactly. Zero iterations preserve
  Pong's clear color. Private loop-index writes persist after successful frames.

All files also run as ordinary DX9 effects. Except for LegacyMaterial.fx,
`MDS_RHI` selects their HLSL 5/6
shader branch. Explicit profile and pass annotations opt into the initial
portable ABI. Changing the shader functions within that ABI is supported;
adding unrecognized parameters, pass state, resources or script commands
selects the editor's same-frame DX9 fallback.

See [the supported profile and validation](../../../docs/RHI_MME_PORTABLE.md).
See [parameter and script limits](../../../docs/RHI_MME_SEMANTICS.md) before
porting an existing effect. These examples do not provide general FX9 or Ray
translation.
