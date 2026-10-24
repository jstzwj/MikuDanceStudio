# Manual diagnostic tools

Run these helpers from the repository root. Build and release automation lives in
[`scripts`](../scripts); maintained regression tests live in [`tests`](../tests)
and run through CTest.

| Tool | Purpose |
|---|---|
| [`strcheck2.py`](strcheck2.py) | Audit original MMD v9.32 x64 `.rdata` string coverage in `src` and `include`. Accepts `--exe`, `--repo`, and `--json`; the default report is `out/audits/strcheck2_result.json`. |
| [`mdiff.py`](mdiff.py) | Compare existing bone-dump JSON files, matching models by name bytes and bone count. Accepts two dump paths, `-v`, and `--model=<name>`. |
| [`pageheap.ps1`](pageheap.ps1) | Enable PageHeap for an executable using `-ExeName` and `-Enable 1`; `-Enable 0` removes its IFEO registry key. Run in an administrative PowerShell session. |

The string audit uses heuristic substring matches and always exits successfully;
inspect its report. The bone comparison summarizes numeric differences. Neither
tool establishes complete behavioral equivalence with the original program.

Completed `wave*` refactoring scripts, their temporary checks, old crash and
memory-capture scripts, the DLL-era MME probes, and checked-in audit output have
been retired. Historical copies remain available through Git history. Current
MME resource validation targets the embedded executable through CTest's
`embedded_mme_resources` test; ray effect tests use the optional
`MIKUDANCESTUDIO_RAY_TEST_EFFECT` CMake setting.
