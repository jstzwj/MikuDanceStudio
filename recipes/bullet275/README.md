# Bullet 2.75 source and build contract

`bullet-src/src` is the unmodified `src` tree from the supplied `bullet-2.75.zip`.
The archive SHA-256 is
`fe2e369593b034c00a1245676edd8417a58c8030a94e061220f0c228ca61fe8f`.
`upstream.json` records all 382 files. Verification normalizes CRLF to LF and
rejects changed, missing or extra files. The original ZIP is not required by CI:

```powershell
python recipes/bullet275/verify_sources.py
python recipes/bullet275/verify_sources.py --archive bullet-2.75.zip
```

Keep all build changes in this directory's CMake/Conan files, outside the
upstream tree. Do not patch the exported source during a Conan build.

The package builds `LinearMath`, `BulletCollision` and `BulletDynamics`, which
are the libraries used by MikuDanceStudio. The unused SoftBody source remains
intact; its original uninitialized `static const T zerodummy` is rejected by
modern MSVC (C2737), so it is not part of this rigid-body package.

MSVC uses `/fp:precise`; x86 additionally uses `/arch:IA32` for the
legacy scalar x87 evaluation model. `btScalar` remains single precision. Conan
propagates these settings to consumers because Bullet also has inline code in
its public headers. Debug keeps its normal optimization and runtime checks;
there are no unconditional `/O2` overrides or unsupported `/RTC-` switches.
Visual Studio 2022's precise model disables floating-point contraction by default;
MSVC has no `/fp:contract-` switch.

The package and application both require C++17 and use `/Zc:alignedNew`.
This keeps allocation and virtual deletion consistent for over-aligned upstream
classes, including the dispatcher, without changing their definitions.

These settings establish a shared numerical model. They do not recreate all
VC9 instruction choices, intermediate stores, CRT functions or expression
orders, and do not promise bitwise equality with the MMD binary. Historical
reference comparisons must retain their original expected data.

Keep `/fp:precise` as the default. In a 600-frame x64 Release/Intel comparison
with Sour Miku, rebuilding all three Bullet libraries with `/fp:strict` left
the reported errors unchanged; `/fp:fast` increased the maximum bone rotation
error from 25.18 to 26.70 degrees and moved the first pose outside tolerance
from frame 18 to frame 5. The application retained its precise floating-point
model in both experiments. Neither is a proven configuration-only improvement.
See the build-option comparison in `docs/X64_RECONSTRUCTION.md`.

The default trajectory regression still compares two fresh 600-frame runs
exactly and checks stop/seek/mode/model/PMM lifecycle behavior. The old MMD-derived
prefix and kernel hashes remain unchanged and can be checked explicitly:

```powershell
cmake -S . -B <application-build> -DMIKUDANCESTUDIO_STRICT_PHYSICS_PARITY=ON
ctest --test-dir <application-build> -C Release -L strict_physics_parity --output-on-failure
```

The upstream build is not expected to pass those historical bitwise checks.
They are diagnostics, not regenerated upstream baselines. The numerical
comparison to an optional local MMD binary instead accepts each rigid body's
and bone's world pose within configurable limits: `0.01` model units of Euclidean
position error and `0.1` degrees of shortest-arc rotation error by default.
These are initial acceptance budgets, not a claim of perceptual equivalence.

```powershell
physics_trajectory_test.exe --original <original-x64-MMD.exe> --frames 600 `
  --position-tolerance 0.01 --rotation-tolerance-degrees 0.1 `
  --report out/physics-tolerance.json
```

Add `--model <model.pmx>` for a local asset or `--d3dx-dispatch amd` for the other
SDK math path. Reports include maximum and RMS pose errors, their frame/object,
the first pose outside the limits, and the original raw-field/ULP diagnostics.
The historical `2e-5` raw-field limit is reported separately and no longer
determines pose acceptance. Nonfinite values, state-flag differences and
nonrepeatable fresh runs still fail. `--stages 1` retains exact stage comparison;
`--reference-prefix 1` explicitly enables the historical prefix check.

If `MDS_ORIGINAL_MMD_EXE` is provided to CMake for an x64 build, CTest registers
the `physics_reference` label for generated-model comparisons in both math
paths. `MDS_NATIVE_PMX_FILE` additionally registers the same comparisons for a
local PMX. Neither the original executable nor third-party assets are bundled
with CI; CI tests the tolerance policy, while these oracle runs require local
inputs. Initial measurements show that the generated model passes these limits,
but the real Sour Miku model exceeds them; see `docs/X64_RECONSTRUCTION.md`.

The application's own x86 math helpers live under `src/math/mmd_compat`, use
the public header `mikudancestudio/mmd_math.hpp`, and are not part of Bullet.

Source copyright/license notices remain intact; see `LICENSE.txt` and the
upstream source-file headers.
