"""Compare generated public bytes and numeric bits to the versioned P0 baseline."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def capture(program, output):
    subprocess.run([str(program), str(output)], check=True)
    files = {}
    for name in ("generated.pmd", "generated.pmx", "generated.vmd", "generated.vpd"):
        data = (output / name).read_bytes()
        files[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    return {"files": files, "numeric": json.loads((output / "numeric.json").read_text())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    expected_path = Path(__file__).resolve().parents[1] / "tests/data/portable_baseline_v1.json"
    expected = json.loads(expected_path.read_text(encoding="utf-8"))
    actual = capture(args.program.resolve(), args.output.resolve())
    profile = actual["numeric"]["profile"]
    assert actual["files"] == expected["files"], "public file bytes differ from baseline v1"
    assert actual["numeric"]["translation_bits"] == expected["numeric_profiles"][profile], (
        "numeric bits differ from the declared compatibility profile")
    repeated = capture(args.program.resolve(), args.output.resolve())
    assert repeated == actual, "baseline capture is not repeatable"
    report = {"schema": 1, "baseline": "portable_baseline_v1", "status": "passed", **actual}
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("PASS: four exact public file hashes, track float bits and repeat capture")


if __name__ == "__main__":
    main()
