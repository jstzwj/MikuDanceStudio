"""File-only comparison input checks; no MMD, windows, devices or private assets."""

import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from freeze_scene_compare import freeze, inspect_pmm


def scene(asset):
    data = bytearray()

    def write(fmt, *values):
        data.extend(struct.pack("<" + fmt, *values))

    def text(value, size):
        data.extend(value.encode("cp932").ljust(size, b"\0"))

    text("Polygon Movie maker 0002", 30)
    write("iiif9B", 640, 360, 250, 30, *([0] * 9))  # No model slots.
    write("iiif3f3fii24sBiB", 0, 0, 0, -45, 0, 10, 0, 0, 0, 0, -1, 0, bytes(24), 0, 30, 0)
    write("i9fB", 0, 0, 10, 0, 0, 0, -45, 0, 0, 0, 0)  # Camera sparse count + current state.
    write("iii6fBi6fBi", 0, 0, 0, 0.6, 0.7, 0.8, -0.5, -1, 0.5, 0, 0,
          0.6, 0.7, 0.8, -0.5, -1, 0.5, 0, 0)
    write("B", 1)
    text("fixture.x", 100)
    write("B", 0)
    text("fixture.x", 100)
    text(str(asset), 256)
    write("B", 0)
    write("iiiBii3f3ffBB", 0, 0, 0, 1, -1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0)
    write("iBii3ff3fBB", 0, 1, -1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0)
    write("4i4B2iB", 0, 0, 0, 1, 0, 0, 0, 0, 0, 100, 0)
    text("", 256)
    write("3f", 0, 0, 1)
    text("", 256)
    write("i3f", 0, 0, 0, 1)
    text("", 256)
    write("4BfiifB", 0, 0, 1, 1, 30, 0, 0, 0.5, 1)
    physics_offset = len(data)
    write("Bfi3fB", 2, 9.8, 0, 0, -1, 0, 0)
    write("iiiBif3fBiBf", 0, 0, 0, 0, 0, 9.8, 0, -1, 0, 0, 0, 1, 0.01125)
    write("iiiBfBi", 0, 0, 0, 1, 0.01125, 0, 0)
    write("3fBii16f3BiB", 0, 0, 0, 0, -1, 0,
          *[float(i % 5 == 0) for i in range(16)], 0, 0, 1, 0, 1)
    return bytes(data), physics_offset


class ComparisonInputs(unittest.TestCase):
    def test_freeze_changes_only_physics_and_keeps_mapping(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            asset = folder / "fixture.x"
            asset.write_bytes(b"local asset fingerprint fixture")
            data, offset = scene(asset)
            source = folder / "no-physics.pmm"  # Deliberately misleading filename.
            source.write_bytes(data)
            emm = f"[Info]\nVersion=3\n[Object]\nAcs1={asset}\n[Effect]\nDefault=none\nAcs1=none\nAcs1.show=true\n".encode("cp932")
            source.with_suffix(".emm").write_bytes(emm)
            output = folder / "fixed.pmm"
            manifest = freeze(source, output, 0)
            self.assertEqual(manifest["source_physics_mode"], 2)
            self.assertEqual(manifest["scene"]["physics_mode"], 0)
            self.assertEqual(manifest["scene"]["physics_mode_offset"], offset)
            self.assertEqual([i for i, (a, b) in enumerate(zip(data, output.read_bytes())) if a != b], [offset])
            self.assertEqual(source.read_bytes(), data)
            self.assertEqual(output.with_suffix(".emm").read_bytes(), emm)
            self.assertEqual(manifest["effect_mapping"]["Effect"]["Acs1"], "none")
            self.assertEqual(manifest["assets"][0]["sha256"], hashlib.sha256(asset.read_bytes()).hexdigest())
            self.assertEqual(manifest["scene"]["camera_base"]["fov"], 30)
            self.assertEqual(manifest["scene"]["camera_current"]["position"], [0, 10, 0])
            self.assertEqual(manifest["scene"]["light_current"]["direction"], [-0.5, -1, 0.5])
            with self.assertRaisesRegex(ValueError, "seek and save first"):
                freeze(source, folder / "wrong-frame.pmm", 12)
            self.assertFalse((folder / "wrong-frame.pmm").exists())
            with self.assertRaisesRegex(ValueError, "differ from source"):
                freeze(source, source, 0)

    def test_malformed_input_is_rejected(self):
        data, _ = scene(Path("fixture.x"))
        self.assertEqual(inspect_pmm(data)["accessories"][0]["parent_model"], -1)
        for truncated in (data[:54], data[:100], data[:-1]):
            with self.assertRaisesRegex(ValueError, "truncated"):
                inspect_pmm(truncated)
        with self.assertRaisesRegex(ValueError, "trailing"):
            inspect_pmm(data + b"unexpected")
        corrupt_count = bytearray(data)
        struct.pack_into("<i", corrupt_count, 55 + 78, -1)
        with self.assertRaisesRegex(ValueError, "invalid PMM count"):
            inspect_pmm(corrupt_count)


if __name__ == "__main__":
    unittest.main()
