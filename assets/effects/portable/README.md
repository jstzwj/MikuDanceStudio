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
- `LegacyStandard.fx`: unchanged FX9 source exercising all 24 camera/geometry
  matrix variants, camera/light vectors and colors, native names, frame clocks
  and float4 POSITION with out parameters. No profile or alternate branch.
- `LegacyScene.fx` / `LegacySharedMaterial.fx`: an ordinary SAS scene script
  publishes a shared mask, captures the scene and grades/copies the result;
  the separate material samples the mask during `ScriptExternal`.
- `LegacyMrtMaterial.fx` / `LegacyMrtScene.fx`: assign the material to a model
  and the scene to one accessory. Ordinary FX9 COLOR0/1 outputs feed float
  captures, half-size float MRT, quarter-size HDR composition, tone mapping and
  an exact final copy. Both files import directly, without a custom profile.
  Supported material editing uses the shared renderer; captured builtin outline
  draws retain DX9 fallback. See [sizing and scope](../../../docs/RHI_MME_LEGACY_MRT.md).
- `DefaultEffectScene.fx` / `LegacyChildMaterial.fx` / `LegacyLeafMaterial.fx`:
  assign the first to a scene accessory and the leaf to a model. The scene's
  ordered `DefaultEffect` rows select a child material for PMD/PMX filenames;
  that material selects the leaf for its own offscreen target. Each turn builds
  its mip chain before the parent samples an explicit LOD. These unlit examples
  disable depth testing and use procedural colors without external textures.
- `NestedScene.fx`: assign to a second scene accessory alongside
  `DefaultEffectScene.fx`. Swapping their native display order changes the
  nested capture/composition order and therefore the final color.
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

All files also run as ordinary DX9 effects. The `Legacy*`, `DefaultEffectScene`
and `NestedScene` examples import their original FX9 source directly. Other
examples use `MDS_RHI` to select their HLSL 5/6 shader branch.
Explicit profile and pass annotations opt into the initial
portable ABI. Changing the shader functions within that ABI is supported;
adding unrecognized parameters, pass state, resources or script commands
selects the editor's same-frame DX9 fallback.

See [the supported profile and validation](../../../docs/RHI_MME_PORTABLE.md).
See [parameter and script limits](../../../docs/RHI_MME_SEMANTICS.md) before
porting an existing effect. These examples do not provide general FX9 or Ray
translation.
