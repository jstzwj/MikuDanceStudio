"""Public P3 scene acceptance. Offscreen only; private assets are never uploaded."""
import argparse
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
import cpu_import_fixture as fixture

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--driver", choices=("hardware", "software"), default="hardware")
    parser.add_argument("--backend", choices=("vulkan", "dx11", "dx12"), default="vulkan")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    # Exercise a native UTF-8 filesystem path on every OS, independent of ACP.
    root = args.output / "scene-\u65e5\u672c\u8a9e"
    fixture.write(root)
    (root / "shadow.pmx").write_bytes(fixture.pmx(0x1D))
    cases = [("triangle.pmd", 1), ("shadow.pmx", 1), ("shadow.pmx", 2),
             ("articulated.pmd", 1), ("articulated.pmx", 1),
             ("v2.pmm", 0), ("pmx.pmm", 0), ("bound-v2.pmm", 0)]
    reports = []
    for ordinal, (name, shadow_mode) in enumerate(cases):
        prefix = args.output / f"{ordinal}-{args.backend}"
        command = [str(args.program.resolve()), "--input", str((root / name).resolve()),
                   "--assets", str(args.assets.resolve()), "--output", str(prefix.resolve()),
                   "--backend", args.backend, "--driver", args.driver,
                   "--frames", "6", "--frame", "3", "--replace-at", "3", "--resize", "1",
                   "--shadow", str(shadow_mode)]
        if "MDS_TEST_VULKAN_LIBRARY" in os.environ:
            command += ["--vulkan-library", os.environ["MDS_TEST_VULKAN_LIBRARY"]]
        subprocess.run(command, check=True, timeout=120)
        report = json.loads(prefix.with_suffix(".json").read_text(encoding="utf-8"))
        pixels = prefix.with_suffix(".rgba").read_bytes()
        assert report["status"] == "passed" and report["cpu_updates"] == 5
        assert report["device_replacements"] == 1 and report["validation_errors"] == 0
        assert (report["width"], report["height"]) == (131, 97)
        assert len(pixels) == report["width"] * report["height"] * 4
        assert sum(pixels[i:i+4] != bytes((255,255,255,0)) for i in range(0,len(pixels),4)) > 100, name
        if shadow_mode:
            values = struct.unpack("<16384f", prefix.with_suffix(".shadow.f32").read_bytes())
            assert all(math.isfinite(v) and 0 <= v <= 1 for v in values)
            if name == "shadow.pmx":
                assert min(values) < .99, (name, shadow_mode)
        report.update(fixture=name, shadow_mode=shadow_mode, strict_post_copy=True)
        reports.append(report)
    (args.output / "acceptance.json").write_text(json.dumps({"schema":1,"stage":"P3","status":"passed","cases":reports}, indent=2)+"\n", encoding="utf-8")
    print(f"P3: {len(cases)} generated PMD/PMX/PMM scene cases passed")

if __name__ == "__main__": main()
