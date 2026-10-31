"""Build and repeat the P0/P1 foundation tests without opening a window."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--generator")
    parser.add_argument("--architecture")
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--parallel", default="2")
    parser.add_argument("--vulkan-include", type=Path)
    parser.add_argument("--vulkan-library", type=Path)
    parser.add_argument("--probe", action="store_true")
    parser.add_argument("--require-device", action="store_true")
    parser.add_argument("--require-hardware", action="store_true")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    evidence = build / "evidence"
    evidence.mkdir(parents=True, exist_ok=True)
    report = {
        "schema": 1, "stage": "P0+P1", "status": "failed",
        "host": {"system": platform.system(), "release": platform.release(),
                 "macos_version": platform.mac_ver()[0],
                 "architecture": platform.machine(), "python": platform.python_version()},
        "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
        "ci_run": os.environ.get("GITHUB_RUN_ID"), "steps": [],
    }

    def run(name, command):
        print(f"P0: {name}", flush=True)
        start = time.monotonic()
        with (evidence / f"{name}.log").open("w", encoding="utf-8") as log:
            result = subprocess.run([str(x) for x in command], cwd=ROOT,
                                    stdout=log, stderr=subprocess.STDOUT)
        report["steps"].append({"name": name, "exit_code": result.returncode,
                                "seconds": round(time.monotonic() - start, 3)})
        if result.returncode:
            print((evidence / f"{name}.log").read_text(encoding="utf-8", errors="replace")[-16000:])
            raise RuntimeError(f"{name} failed ({result.returncode})")

    try:
        configure = ["cmake", "-S", ROOT / "cmake/portable-baseline", "-B", build,
                     "-DBUILD_TESTING=ON", f"-DCMAKE_BUILD_TYPE={args.configuration}"]
        if args.generator:
            configure += ["-G", args.generator]
        if args.architecture:
            configure += ["-A", args.architecture]
        probe = args.probe or args.require_device or args.require_hardware
        configure += [f"-DMDS_P0_VULKAN_PROBE={'ON' if probe else 'OFF'}"]
        if args.vulkan_include:
            configure += [f"-DMDS_P0_VULKAN_INCLUDE={args.vulkan_include.resolve()}"]
        run("configure", configure)
        build_command = ["cmake", "--build", build, "--config", args.configuration,
                         "--parallel", args.parallel]
        run("build", build_command)
        run("incremental-build", build_command)
        report["environment"] = json.loads((build / f"environment-{args.configuration}.json").read_text())
        report["test_runs"] = []
        # Repeat the suite, so CTest setup fixtures run again. Repeating a single
        # CLI round trip in isolation would intentionally refuse its old output.
        for n in (1, 2):
            junit = evidence / f"tests-{n}.xml"
            run(f"tests-{n}", ["ctest", "--test-dir", build, "-C", args.configuration,
                              "--output-on-failure", "--no-tests=error", "--timeout", "90",
                              "--output-junit", junit])
            cases = ET.parse(junit).getroot().findall(".//testcase")
            skipped = [case.attrib["name"] for case in cases if case.find("skipped") is not None]
            required = {"bullet_allocation", "bullet_simulation", "bullet_upstream_sources",
                        "physics_world_state", "cpu_scene_simulation", "portable_baseline_contract",
                        "cpu_math", "model_runtime", "model_runtime_cli", "model_runtime_dependencies"}
            names = {case.attrib["name"] for case in cases}
            if not required <= names or set(skipped) - {"pmm_scene_session_external"}:
                raise RuntimeError(f"missing required tests or unexpected skips: {skipped}")
            report["test_runs"].append({"total": len(cases), "skipped": skipped})
        report["baseline"] = json.loads((build / "baseline/report.json").read_text())
        for key, name in (("model_runtime", "p1-report.json"), ("model_dependencies", "p1-dependencies.json")):
            paths = list(build.rglob(name))
            if len(paths) != 1:
                raise RuntimeError(f"missing or ambiguous P1 evidence: {name}")
            report[key] = json.loads(paths[0].read_text(encoding="utf-8"))
        report["bullet_upstream"] = json.loads((ROOT / "recipes/bullet275/upstream.json").read_text())
        if probe:
            program = build / args.configuration / "mds_platform_probe.exe" if os.name == "nt" else build / "mds_platform_probe"
            command = [program, "--output", evidence / "gpu-probe.json"]
            if args.vulkan_library:
                command += ["--library", args.vulkan_library.resolve()]
            if args.require_hardware:
                command += ["--require-hardware"]
            elif args.require_device:
                command += ["--require-device"]
            run("gpu-probe", command)
            report["gpu_probe"] = json.loads((evidence / "gpu-probe.json").read_text())
        report["status"] = "passed"
    except (OSError, ValueError, RuntimeError) as error:
        report["failure"] = str(error)
        print(f"P0 FAILED: {error}", file=sys.stderr)
    finally:
        (evidence / "p0-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if report["status"] == "passed":
        print(f"P0 PASS: {evidence / 'p0-report.json'}")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
