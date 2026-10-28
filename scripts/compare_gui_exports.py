"""Compare repeated GUI exports without hiding temporal or single-channel differences.

PNG loading requires Pillow. The analysis itself uses only the standard library.
Exit 0 means every original/port pair is byte-identical after RGBA decoding;
exit 1 retains any difference, even if the observed sample values overlap.
This tool does not establish that scene inputs or rendering states are equal.
"""

import argparse
from dataclasses import dataclass
import hashlib
from itertools import combinations
import json
from pathlib import Path


@dataclass(frozen=True)
class Sample:
    name: str
    width: int
    height: int
    rgba: bytes


def load(path):
    from PIL import Image

    with Image.open(path) as image:
        rgba = image.convert("RGBA")
        return Sample(str(path.resolve()), *rgba.size, rgba.tobytes())


def difference(a, b):
    count = alpha = 0
    maximum = [0] * 4
    bounds = [a.width, a.height, -1, -1]
    for offset in range(0, len(a.rgba), 4):
        left, right = a.rgba[offset:offset + 4], b.rgba[offset:offset + 4]
        if left == right:
            continue
        count += 1
        alpha += left[3] != right[3]
        for channel in range(4):
            maximum[channel] = max(maximum[channel], abs(left[channel] - right[channel]))
        y, x = divmod(offset // 4, a.width)
        bounds = [min(bounds[0], x), min(bounds[1], y), max(bounds[2], x), max(bounds[3], y)]
    return dict(left=a.name, right=b.name, pixels=count, alpha_pixels=alpha,
                max_rgba=maximum, bounds=bounds if count else None)


def compare(original, port):
    if len(original) < 2 or len(port) < 2:
        raise ValueError("at least two independently exported images per application are required")
    first = original[0]
    if first.width <= 0 or first.height <= 0:
        raise ValueError("image dimensions must be positive")
    for sample in [*original, *port]:
        if (sample.width, sample.height) != (first.width, first.height):
            raise ValueError("all exports must have identical dimensions")
        if len(sample.rgba) != first.width * first.height * 4:
            raise ValueError("incomplete RGBA image")
    pairs = [difference(a, b) for a in original for b in port]
    changing = [0, 0]
    stable = disjoint = separated = 0
    examples = []
    for offset in range(0, len(first.rgba), 4):
        values = [{sample.rgba[offset:offset + 4] for sample in group}
                  for group in (original, port)]
        a, b = values
        for group in range(2):
            changing[group] += len(values[group]) > 1
        stable_here = len(a) == len(b) == 1 and a != b
        stable += stable_here
        if a & b:
            continue
        disjoint += 1
        # Overlapping per-channel ranges do not imply a shared RGBA value.
        gap = [max(0, min(v[c] for v in a) - max(v[c] for v in b),
                   min(v[c] for v in b) - max(v[c] for v in a)) for c in range(4)]
        separated += any(gap)
        if len(examples) < 20:
            y, x = divmod(offset // 4, first.width)
            examples.append(dict(x=x, y=y, stable=stable_here,
                                 original=[list(v) for v in sorted(a)],
                                 port=[list(v) for v in sorted(b)], channel_range_gap=gap))
    def metadata(sample):
        return dict(path=sample.name, rgba_sha256=hashlib.sha256(sample.rgba).hexdigest())

    return dict(width=first.width, height=first.height,
                exact_equal=all(pair["pixels"] == 0 for pair in pairs),
                original=[metadata(s) for s in original], port=[metadata(s) for s in port],
                original_repeats=[difference(a, b) for a, b in combinations(original, 2)],
                port_repeats=[difference(a, b) for a, b in combinations(port, 2)],
                cross_pairs=pairs, changing_pixels=dict(original=changing[0], port=changing[1]),
                stable_mismatch_pixels=stable, disjoint_observed_rgba_pixels=disjoint,
                disjoint_channel_range_pixels=separated, disjoint_examples=examples)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, nargs="+", required=True)
    parser.add_argument("--port", type=Path, nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        for paths in (args.original, args.port):
            if len({p.resolve() for p in paths}) != len(paths):
                raise ValueError("repeated paths are not independent export samples")
        report = compare([load(p) for p in args.original], [load(p) for p in args.port])
        for group, paths in (("original", args.original), ("port", args.port)):
            for entry, path in zip(report[group], paths):
                entry["file_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
    except (ImportError, OSError, ValueError) as error:
        parser.exit(2, f"comparison failed: {error}\n")
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in (
        "exact_equal", "changing_pixels", "stable_mismatch_pixels",
        "disjoint_observed_rgba_pixels", "disjoint_channel_range_pixels")}))
    return 0 if report["exact_equal"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
