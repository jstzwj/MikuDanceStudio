# Effect Mapping: Ray default checkbox mismatch

## Cause

The mapping dialog used literal full-path/basename equality, while rendering
already used ordered DefaultEffect wildcard matching and the carrier identity
for `self`. Consequently `self=hide`, `*controller*.pmx=hide`, and `*=hide`
missed in the dialog. Its missing-row fallback was checked, so the UI showed
objects enabled and `(none)` even when the render resolver hid them.

This is a confirmed UI/resolver discrepancy, not evidence that every remaining
Ray rendering difference has been resolved.

## Original v0.37 x64 evidence

- `0x1800411C3`: dialog queries the effective assignment through `0x18002DB80`.
- `0x1800411F4`, `0x1800413E1`: checkbox uses `show != 0`, encoded as
  `(shown + 1) << 12`.
- `0x18002DBE4`: missing visibility is `-1`, which is checked. `none` and
  missing assignments must not be confused with an explicit `hide`.
- `0x18002B880`, `0x18002B890`: case-sensitive `self` and owner identity.
- `0x18002B8C5`: case-insensitive basename wildcard matcher; first matching
  annotation wins.
- `0x18002B981`: `main_default` inherits the same object's Main assignment.
- `0x1800428DB/E5`: manual visibility is stored independently of the effect
  path in the original assignment table.

## Changes

- Rendering and dialog reads share `MmeFindDefaultEffectRow`; each dialog tab
  carries its actual scene owner rather than borrowing the active render turn.
- Checkbox handling preserves missing/none as checked and hide as unchecked.
- Manual row overrides are exact full-path entries inserted before default
  wildcard rules. Editing one row does not rewrite a shared wildcard, and
  a preceding `*=hide` no longer swallows an appended manual assignment.
- Remove writes an explicit `none`; Reset removes the exact override. Hiding
  and showing preserves the previous row value; explicitly showing a
  default-hidden object writes `none` instead of restoring `hide` again.

For the checked-in Ray asset configuration, MaterialMap hides `ray.x` and
`ray_controller.pmx`; a model named `Time of day fast.pmx` matches the generic
PMX material row. FogMap/LightMap/EnvLightMap hide these three filenames.
Matching uses the filename, not the name of its containing Skybox directory.

## Scope and validation

The new `mme_effect_mapping` regression calls the production matcher and row
override helpers with self identity, wildcard precedence, Ray representative
rules, missing/none, and isolated manual overrides.

Release builds succeeded for x64 and x86. CTest passed 21/21 on each architecture
(x64 15.14 seconds; x86 88.08 seconds), including mapping and real-Ray GPU tests.
Logs: `build-x64/effect-mapping-build.log`,
`build-x64/effect-mapping-ctest.log`, and corresponding `build-x86` paths.

This remains a row-based assignment adapter. Original independent per-subset
path/visibility overrides, `main_default` visibility inheritance, duplicate
instances of the same file, full EMM persistence, and all reset/visibility
interactions are not certified as 1:1.
No full-scene original-versus-port pixel comparison was performed for this fix.
