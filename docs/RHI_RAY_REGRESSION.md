# RayMMD local regression

RayMMD is a local external test asset. Its effects, models and textures are
not copied into this repository. The tested checkout is revision
`218a91cd1c53f49850b2772ebbcb98bf258e75b1`; input hashes and checkout status
are recorded in the local comparison manifest.

## Execution boundary

The current generic MME profiles do not implement arbitrary FX9 translation.
Unmodified RayMMD still executes through DX9, including when the user selects
DX11, DX12 or Vulkan. Whole-frame eligibility rejects Ray before uploading
scene resources or submitting work to the selected RHI backend.

Ray's actual requirements include CONTROLOBJECT, includes, shared parameters,
OFFSCREENRENDERTARGET/DefaultEffect child assignments, ordered scene scripts,
multiple color attachments and formats beyond RGBA8. For example, the bloom
chain uses A16B16G16R16F at several viewport ratios, and other stages use L8
and A8. The optional SMAA path also declares A8L8. These need explicit
capability and shader migration work before Ray can execute through the
shared RHI.

## Hidden editor gate

Set `MIKUDANCESTUDIO_RAY_TEST_EFFECT` to a local absolute `ray.fx` path when
configuring a testing build. It enables `editor_ray_fallback_dx11`,
`editor_ray_fallback_dx12` and `editor_ray_fallback_vulkan` for enabled backends.
Each uses generated PMD and PMX models in a never-shown parent window:

- Assign real `Main/main.fx` and `Outline/Fixed Color/outline.fx` to a subset
  and then the complete model; require whole-frame DX9 fallback.
- Load the original `ray.x`; discover its same-name `ray.fx` automatically,
  mixed with a supported portable material.
- Require one geometry update and one DX9 scene per fallback frame, no RHI
  uploads or submissions, and no presentation of the preceding RHI image.
- Exercise repeats, extent changes, backend changes and scene unload.
- Remove Ray and require recovery to the selected backend when runtime
  effects are available, including exact recovery of the earlier image.
  With no Vulkan runtime compiler, the custom material retains explicit DX9
  fallback; removing that material must recover ordinary RHI frames as well.

The test catches a recovery bug: cached offscreen child bindings outlive the
removed scene owner in the DX9 engine. Portable eligibility must inspect
live base assignments rather than treat those derived cache nodes as active
effects. The fix preserves native DX9 caching and rendering behavior.

The host blocks unexpected dialogs before creation and fails the test if one
was requested. It uses no desktop input or focus operations.

```powershell
ctest --test-dir build-x64/build -C Release -R '^editor_ray_fallback_|^mme_ray_' --output-on-failure
ctest --test-dir build-x86/build -C Release -R '^editor_ray_fallback_|^mme_ray_' --output-on-failure
```

## Native DX9 comparison

`mme_ray_script_gpu` compiles the actual effect, creates SAS resources and runs
its step/resume fullscreen script on a hidden HAL device. `mme_ray_offscreen_gpu`
checks DefaultEffect child resolution, turn target restoration and mip-repeat
finalization. These smoke tests alone do not establish full image parity.

`scripts/compare_native_scene.py` additionally compares the port with each
architecture's original MMD + MMHack + MMEffect, using a local PMX:

```powershell
python scripts/compare_native_scene.py --original64 C:/local/x64/MikuMikuDance.exe --mme64 C:/local/MMEffect64 --original32 C:/local/x86/MikuMikuDance.exe --mme32 C:/local/MMEffect32 --model C:/local/miku.pmx --ray C:/local/ray-mmd --case material --case outline --case post --output out/ray-native
```

The existing oracle maps the reference modules in a hidden fixture; it does
not launch the original editor GUI. Each case also reloads the port-written
EMM through both engines. Reports include final color, shadow, intermediate
targets and draw records, plus input hashes and process completion. Comparisons
retain the oracle's existing exact criteria. Output remains under ignored
`out/`; the input model and Ray checkout are read only.

The covered material, outline and postprocess configurations use the fixture's
existing lighting and parameter defaults. This does not cover arbitrary Ray
presets, animated `ray_controller.pmx` controls, every skybox/light/fog effect,
or the complete visible editor workflow. Successful DX9 comparison or fallback
must not be reported as native DX11/DX12/Vulkan execution of Ray.

## Validation, 2026-10-29

The local Sour Miku PMX has 139,692 vertices, 25 materials and 763 SDEF
vertices. It and the Ray checkout stayed outside the repository and unchanged.

| Gate | x64 | x86 |
| --- | --- | --- |
| Hidden editor fallback/recovery, DX11/DX12/Vulkan | 3 passed | 3 passed |
| Actual Ray script and offscreen-turn smoke | 2 passed | 2 passed |
| Original material/outline/post, initial and cross EMM load | 6 processes passed | 6 processes passed |
| Exact native stage pairs | 117 | 117 |

There were no failures or skips in those runs. Each editor gate runs both
generated PMD and PMX fixtures. The native comparisons cover 234 stage pairs
and 21,990 paired Pass records. Final color, independent shadow, effective
constants, matrices, geometry and recorded state all matched. Target readbacks
also matched, including 1,404 A8, 168 L8, 5,608 A16B16G16R16F, 7,048 R32F and
15,618 A8R8G8B8 records on each side of the comparisons.

The local native manifest is
`out/ray-expansion-native/20261029-201607-630366/manifest.json`. The x64
diagnostics-off product and x86 editor built successfully. Null shader-contract
validation passed; code formatting, Python syntax and `git diff --check` also
passed. These test runtimes include extensive synchronous per-draw capture and
are not editor frame-rate measurements.
The final targeted rerun passed all three native portable-example gates and
the x64 DX11 editor gate. The x86 Vulkan gate also passed its added check that
ordinary RHI frames recover after clearing custom effects without runtime DXC.
