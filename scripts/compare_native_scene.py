"""Compare original MMD + MMHack + MMEffect with the port on one GPU.

Original binaries and community assets remain local. Each architecture is
compared with its own original; no cross-architecture numeric normalization.
"""

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def identity(path):
    path = path.resolve(strict=True)
    result = {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    revision = subprocess.run(["git", "-C", str(path.parent), "rev-parse", "HEAD"],
                              capture_output=True, text=True)
    if revision.returncode == 0:
        result["git_commit"] = revision.stdout.strip()
        status = subprocess.run(["git", "-C", str(path.parent), "status", "--porcelain"],
                                capture_output=True, text=True, check=True)
        result["git_status"] = status.stdout.splitlines()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original64", type=Path, required=True)
    parser.add_argument("--mme64", type=Path, required=True, help="Original DLL directory")
    parser.add_argument("--original32", type=Path)
    parser.add_argument("--mme32", type=Path)
    parser.add_argument("--x64", type=Path, default=Path("build-x64/build/Release/native_scene_render_test.exe"))
    parser.add_argument("--x86", type=Path, default=Path("build-x86/build/Release/native_scene_render_test.exe"))
    parser.add_argument("--model", type=Path, help="Local PMX, never modified")
    parser.add_argument("--ray", type=Path, help="Local ray-mmd repository")
    parser.add_argument("--hardware-vp", action="store_true", help="Explicit hardware vertex processing for both sides")
    parser.add_argument("--case", action="append", choices=["generated", "features", "pmx", "material", "outline", "post"])
    parser.add_argument("--architecture", type=int, choices=[32, 64], action="append",
                        help="Limit the run to selected architectures")
    parser.add_argument("--timeout", type=float, default=1800,
                        help="Seconds allowed per process (default: 1800)")
    parser.add_argument("--output", type=Path, default=Path("out/native-scene-comparison"))
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if bool(args.original32) != bool(args.mme32):
        parser.error("--original32 and --mme32 must be supplied together")
    if args.architecture and 32 in args.architecture and not args.original32:
        parser.error("--architecture 32 requires --original32 and --mme32")
    cases = {"generated": [], "features": ["--features", "1"]}
    if args.model:
        cases["pmx"] = ["--model", str(args.model.resolve(strict=True))]
    if args.ray:
        model = cases.get("pmx", [])
        main_fx = args.ray / "Main/main.fx"
        cases["material"] = model + ["--model-effect", str(main_fx.resolve(strict=True))]
        cases["outline"] = model + ["--model-effect", str((args.ray / "Outline/Fixed Color/outline.fx").resolve(strict=True))]
        cases["post"] = cases["material"] + ["--scene-effect", str((args.ray / "ray.fx").resolve(strict=True))]
    selected = args.case or list(cases)
    if any(name not in cases for name in selected):
        parser.error("Selected case requires --model and/or --ray")
    root = args.output / datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    root.mkdir(parents=True)
    manifest = {"inputs": {}, "runs": []}
    architectures = [(64, args.x64, args.original64, args.mme64)]
    if args.original32:
        architectures.append((32, args.x86, args.original32, args.mme32))
    if args.architecture:
        architectures = [row for row in architectures if row[0] in args.architecture]
    failed = skipped = 0
    for bits, executable, original, mme in architectures:
        for path in [executable, original, mme / "MMHack.dll", mme / "MMEffect.dll"]:
            manifest["inputs"][str(path)] = identity(path)
        launcher = ([sys.executable, str(Path(__file__).with_name("run_native_render.py"))]
                    if bits == 32 else [])
        for name in selected:
            for option, value in zip(cases[name][::2], cases[name][1::2]):
                if option != "--features":
                    manifest["inputs"][value] = identity(Path(value))
            first = root / f"{name}-x{bits}"
            for cross in [False, True]:
                output = first.with_name(first.name + "-cross") if cross else first
                command = launcher + [str(executable.resolve()), "--original", str(original.resolve()),
                    "--mmhack", str((mme / "MMHack.dll").resolve()), "--mme", str((mme / "MMEffect.dll").resolve()),
                    "--output", str(output.resolve())] + cases[name]
                if cross:
                    command += ["--cross-load", str((first / "port_assigned.emm").resolve())]
                with output.with_suffix(".log").open("w", encoding="utf-8") as log:
                    if args.hardware_vp:
                        command += ["--hardware-vp", "1"]
                    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                            timeout=args.timeout)
                row = {"case": name, "architecture_bits": bits, "cross_load": cross,
                       "command": command, "exit_code": result.returncode}
                if result.returncode == 0:
                    row["report"] = json.loads((output / "report.json").read_text())
                    if row["report"]["failures"] != 0:
                        raise RuntimeError("Invalid native completion report")
                elif result.returncode == 77:
                    skipped += 1
                else:
                    failed += 1
                manifest["runs"].append(row)
                (root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
                print(f"{output.name}: exit {result.returncode}", flush=True)
                if result.returncode:
                    break
    print(root / "manifest.json")
    return 1 if failed else 77 if skipped else 0


if __name__ == "__main__":
    raise SystemExit(main())
