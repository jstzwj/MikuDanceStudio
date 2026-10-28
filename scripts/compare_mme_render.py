#!/usr/bin/env python3
"""Run original MME engines and the port with identical GPU fixtures.

Requires Pillow and a connected Windows graphics session. The original DLL
and optional model remain local assets; the test verifies the DLL's SHA-256.
The native oracle uses port MMD/MMHack query adapters, not the original GUI.
"""

import argparse
import array
import datetime
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

from PIL import Image, ImageChops, ImageEnhance, ImageDraw


def run(executable, output, model=None, native=None, model_effect=None, scene_effect=None):
    output.mkdir()
    command = [str(executable.resolve()), "--output", str(output.resolve())]
    if model:
        command += ["--model", str(model.resolve())]
    if native:
        command += ["--mme-original", str(native.resolve())]
    for option, effect in [("--model-effect", model_effect), ("--scene-effect", scene_effect)]:
        if effect:
            command += [option, str(effect.resolve())]
    with output.with_suffix(".log").open("w", encoding="utf-8") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=600)
    if result.returncode:
        raise RuntimeError(f"{output.name}: exit {result.returncode}; see {output.with_suffix('.log')}")
    check_completion(output, native, scene_effect)


def check_completion(output, native, scene_effect=None):
    report = json.loads((output / "report.json").read_text(encoding="utf-8"))
    if report.get("failures") != 0 or report.get("native") != bool(native):
        raise RuntimeError(f"{output.name}: invalid completion report")
    if scene_effect:
        rows = list(struct.iter_unpack("<272I", (output / "external.draws").read_bytes()))
        passes = [json.loads(line) for line in (output / "external.passes").read_text().splitlines()]
        if len(rows) != len(passes) or not any(
            row[14] and item["vs"] != "0" and item["ps"] != "0" and any(item["textures"])
            for row, item in zip(rows, passes)
        ):
            raise RuntimeError(f"{output.name}: scene effect did not execute a textured buffer pass")


def ordered_float(bits):
    return (~bits & 0xFFFFFFFF) if bits & 0x80000000 else bits | 0x80000000


def compare(reference, actual):
    extensions = {".png", ".r32f", ".draws", ".light", ".passes"}
    names = {p.name for p in reference.iterdir() if p.suffix in extensions}
    actual_names = {p.name for p in actual.iterdir() if p.suffix in extensions}
    if not names or names != actual_names:
        raise RuntimeError(f"artifact set mismatch: {names ^ actual_names}")
    results = {}
    for name in sorted(names):
        left, right = reference / name, actual / name
        if left.suffix == ".png":
            with Image.open(left) as image:
                a = image.convert("RGBA")
            with Image.open(right) as image:
                b = image.convert("RGBA")
            same_size = a.size == b.size
            count = sum(x != y for x, y in zip(a.getdata(), b.getdata())) if same_size else None
            results[name] = {"equal": same_size and count == 0, "different_pixels": count}
            if same_size:
                results[name]["max_channel_error"] = max(high for low, high in ImageChops.difference(a, b).getextrema())
        else:
            a, b = left.read_bytes(), right.read_bytes()
            item = {"equal": a == b, "bytes": len(a)}
            if len(a) != len(b):
                item["actual_bytes"] = len(b)
            if left.suffix == ".r32f" and len(a) == len(b):
                aa, bb = array.array("I"), array.array("I")
                aa.frombytes(a)
                bb.frombytes(b)
                item["different_texels"] = sum(x != y for x, y in zip(aa, bb))
                item["max_ulp"] = max(abs(ordered_float(x) - ordered_float(y)) for x, y in zip(aa, bb))
            if left.suffix == ".draws":
                stride = 272 * 4
                if len(a) % stride or len(b) % stride:
                    raise RuntimeError("invalid draw trace record size")
                item["draw_count"] = len(a) // stride
                if a != b and len(a) == len(b):
                    word = next(i for i, (x, y) in enumerate(zip(struct.iter_unpack("<I", a),
                                                              struct.iter_unpack("<I", b))) if x != y)
                    item["first_difference"] = {"draw": word // 272, "word": word % 272}
            if left.suffix == ".passes":
                aa = [json.loads(line) for line in a.splitlines()]
                bb = [json.loads(line) for line in b.splitlines()]
                item["draw_count"] = len(aa)
                if aa != bb:
                    item["first_difference"] = next(
                        (i for i, (x, y) in enumerate(zip(aa, bb)) if x != y),
                        min(len(aa), len(bb)),
                    )
            results[name] = item
    return results


def contact_sheet(reference, actual, destination):
    stages = ["unassigned", "material", "subset_none", "parameters_changed", "multipass", "relative"]
    if (reference / "external.png").exists():
        stages = ["unassigned", "external", "external_reset_before", "external_reset_after",
                  "external_reset_recovered", "external_detached"]
    tile, label = 256, 24
    sheet = Image.new("RGB", (tile * 3, (tile + label) * len(stages)), "white")
    draw = ImageDraw.Draw(sheet)
    for index, stage in enumerate(stages):
        with Image.open(reference / f"{stage}.png") as image:
            a = image.convert("RGB")
        with Image.open(actual / f"{stage}.png") as image:
            b = image.convert("RGB")
        diff = ImageEnhance.Brightness(ImageChops.difference(a, b)).enhance(8)
        for column, (image, title) in enumerate([(a, "Original MME"), (b, "Port"), (diff, "Difference x8")]):
            x, y = column * tile, index * (tile + label)
            draw.text((x + 4, y + 4), f"{stage}: {title}", fill="black")
            sheet.paste(image.resize((tile, tile)), (x, y + label))
    sheet.save(destination)


def effect_identity(path):
    result = {"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    revision = subprocess.run(["git", "-C", str(path.resolve().parent), "rev-parse", "HEAD"],
                              capture_output=True, text=True)
    if revision.returncode == 0:
        result["git_commit"] = revision.stdout.strip()
        status = subprocess.run(["git", "-C", str(path.resolve().parent), "status", "--porcelain"],
                                capture_output=True, text=True, check=True)
        result["git_status"] = status.stdout.splitlines()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--x64", type=Path, default=Path("build-x64/build/Release/scene_mme_gpu_test.exe"))
    parser.add_argument("--x86", type=Path, help="Optional port x86 executable; differences are reported separately")
    parser.add_argument("--original", type=Path, required=True, help="Original MMEffect v0.37 x64 English DLL")
    parser.add_argument("--original-x86", type=Path, help="Original MMEffect v0.37 x86 English DLL; requires --x86")
    parser.add_argument("--model", type=Path, help="Optional PMD/PMX; otherwise use generated PMD")
    parser.add_argument("--model-effect", type=Path, help="External effect assigned to the whole model")
    parser.add_argument("--scene-effect", type=Path, help="Postprocess effect on the first accessory; requires a textured buffer pass")
    parser.add_argument("--output", type=Path, default=Path("out/mme-comparison"))
    options = parser.parse_args()
    if options.original_x86 and not options.x86:
        parser.error("--original-x86 requires --x86")
    # Each run owns a new directory; stale artifacts cannot satisfy a check.
    root = options.output / datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    root.mkdir(parents=True)
    native, port = root / "native-x64", root / "port-x64"
    run(options.x64, native, options.model, options.original, options.model_effect, options.scene_effect)
    run(options.x64, port, options.model, None, options.model_effect, options.scene_effect)
    results = {"native_x64_vs_port_x64": compare(native, port)}
    if options.x86:
        port86 = root / "port-x86"
        run(options.x86, port86, options.model, None, options.model_effect, options.scene_effect)
        results["port_x64_vs_port_x86"] = compare(port, port86)
        if options.original_x86:
            native86 = root / "native-x86"
            run(options.x86, native86, options.model, options.original_x86,
                options.model_effect, options.scene_effect)
            results["native_x86_vs_port_x86"] = compare(native86, port86)
            results["original_x86_sha256"] = hashlib.sha256(options.original_x86.read_bytes()).hexdigest()
    results["scope"] = "Original MME machine code with port MMD/MMHack queries and production host dispatch"
    results["model"] = str(options.model.resolve()) if options.model else "generated PMD"
    results["original_sha256"] = hashlib.sha256(options.original.read_bytes()).hexdigest()
    if options.model:
        results["model_sha256"] = hashlib.sha256(options.model.read_bytes()).hexdigest()
    for key in ["model_effect", "scene_effect"]:
        if path := getattr(options, key):
            results[key] = effect_identity(path)
    differences = [name for name, value in results["native_x64_vs_port_x64"].items() if not value["equal"]]
    results["native_x64_differences"] = differences
    differences86 = [name for name, value in results.get("native_x86_vs_port_x86", {}).items()
                     if not value["equal"]]
    if options.original_x86:
        results["native_x86_differences"] = differences86
    results["x86_note"] = ("Compared with original x86 MME; MMD/MMHack queries still come from the port"
                           if options.original_x86 else
                           "Recorded separately; original x86 MME has not been used as an oracle")
    (root / "comparison.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    contact_sheet(native, port, root / "comparison.png")
    print(root / "comparison.json")
    if differences or differences86:
        print(f"Native/port differences: x64={differences}, x86={differences86}", file=sys.stderr)
        return 1
    print("All available original MME / same-architecture port GPU artifacts match.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
