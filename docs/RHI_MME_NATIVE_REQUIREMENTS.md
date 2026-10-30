# Native MME requirements inventory

Configure `MIKUDANCESTUDIO_RAY_TEST_EFFECT` to the local, unmodified `ray.fx`,
build `mme_ray_smoke_test` and `mme_offscreen_turn_test`, then run:

```powershell
ctest --test-dir build-x64/build -C Release --output-on-failure -R '^mme_ray_(shared_)?requirements$'
```

Both gates use never-shown HWNDs. They write `mme-ray-requirements.txt` and
`mme-ray-shared-requirements.txt` in the build directory. No Ray files are copied
into the repository. The first executes the actual native screen script and
records executed Buffer passes; the second uses the production offscreen turn
and a selected DefaultEffect material child. Neither is a full Ray image test.

The versioned text inventory includes live FX texture values, shared flags,
pool/texture identity groups, requested and actual dimensions/formats/mips,
ordered DefaultEffect selectors, SAS-selected techniques and parsed commands.
Sampler names/registers come from the actual compiled shader CTAB, including
only bindings retained by the compiler. Identity numbers are report-local,
never native addresses. Depth placeholder textures are distinguished from the
resource's requested depth surface format.

The v3 report records numeric viewport ratios, automatic source
import status, and a shader row for every selected technique/pass in each effect.
Successful source import is followed by wrapper generation and actual VS/PS SM5
compilation. First-stage failures mark later shader work `blocked-by-source`;
parameters/resources/states are explicitly `unassessed`, not assumed supported.
Compilation results are independent of Vulkan execution and whole-frame acceptance.
The public ordinary MRT scene tests also exercise the successful reporting path.
Source failures now include mapped file/line/column, declaration, token and
technique/pass context. Source-only probes now import the MaterialMap child;
the root first reports `MME_legacy_static_initializer`. Those probes are not a
new native execution inventory. See [material import scope and current validation](RHI_MME_RAY_MATERIAL.md).

Local x64 DX9 HAL run on 2026-10-30 passed both gates. The screen shader uses
pixel sampler registers through s12: **13 slots**, requiring expansion beyond
the previous four-slot RHI ABI. A 16-slot graphics profile covers the native
SM3 pixel-sampler ceiling without claiming support for Ray's remaining shader,
resource or script features. The trace also exposes floating-point intermediate
targets, viewport-ratio sizing and offscreen child scheduling. Those requirements
remain explicit compatibility work, rather than evidence of native RHI Ray.

| Captured scope | Effects | Texture parameters | SAS resources | Selected techniques | Declared passes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Ray screen script | 1 | 39 | 39 | 1 | 31 |
| Screen plus MaterialMap material child | 2 | 47 | 39 | 6 | 35 |

The screen run executes all 31 Buffer draws in recorded order. The child snapshot
is taken before later test mutations replace its binding with main_default/none.
Gbuffer2RT through Gbuffer8RT have equal live identities across the two effects;
the child does not allocate separate SAS resources for those shared references.

For comparisons, regenerate both inventories in the same viewport and compare
them as text. Pool values must be sampled after all involved instances load:
a shared parameter can refer to a texture another effect created, rather than
the private texture retained by its own SAS resource record. Native identities
must not be inferred from a filename or from matching parameter names alone.
