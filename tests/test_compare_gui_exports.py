"""Raw RGBA fixtures exercise export analysis without a GPU or Pillow."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from compare_gui_exports import Sample, compare


def sample(name, *pixels):
    return Sample(name, len(pixels), 1, bytes(c for p in pixels for c in p))


class ComparisonTests(unittest.TestCase):
    def test_equal(self):
        a = sample("a", (1, 2, 3, 4))
        result = compare([a, a], [a, a])
        self.assertTrue(result["exact_equal"])
        self.assertEqual(result["disjoint_observed_rgba_pixels"], 0)

    def test_one_pixel_alpha_difference_is_retained(self):
        a = sample("a", (0, 0, 0, 255), (10, 10, 10, 255))
        b = sample("b", (0, 0, 0, 255), (10, 10, 10, 254))
        result = compare([a, a], [b, b])
        self.assertFalse(result["exact_equal"])
        self.assertEqual(result["stable_mismatch_pixels"], 1)
        self.assertEqual(result["cross_pairs"][0]["alpha_pixels"], 1)
        self.assertEqual(result["cross_pairs"][0]["max_rgba"], [0, 0, 0, 1])
        self.assertEqual(result["cross_pairs"][0]["bounds"], [1, 0, 1, 0])

    def test_overlapping_samples_do_not_count_as_exact(self):
        a = sample("a", (1, 2, 3, 4))
        b = sample("b", (2, 2, 3, 4))
        result = compare([a, b], [b, a])
        self.assertFalse(result["exact_equal"])
        self.assertEqual(result["stable_mismatch_pixels"], 0)
        self.assertEqual(result["changing_pixels"], dict(original=1, port=1))
        self.assertEqual(result["disjoint_observed_rgba_pixels"], 0)

    def test_temporal_mask_cannot_hide_systematic_difference(self):
        a = [sample(str(v), (v, 0, 0, 255)) for v in (1, 2)]
        b = [sample(str(v), (v, 0, 0, 255)) for v in (4, 5)]
        result = compare(a, b)
        self.assertEqual(result["stable_mismatch_pixels"], 0)
        self.assertEqual(result["disjoint_observed_rgba_pixels"], 1)
        self.assertEqual(result["disjoint_examples"][0]["channel_range_gap"], [2, 0, 0, 0])

    def test_overlapping_channel_ranges_can_have_disjoint_colors(self):
        a = [sample("a", p) for p in ((0, 0, 0, 255), (1, 1, 0, 255))]
        b = [sample("b", p) for p in ((0, 1, 0, 255), (1, 0, 0, 255))]
        result = compare(a, b)
        self.assertEqual(result["disjoint_channel_range_pixels"], 0)
        self.assertEqual(result["disjoint_observed_rgba_pixels"], 1)

    def test_incomplete_or_unpaired_inputs_are_rejected(self):
        a = sample("a", (0, 0, 0, 255))
        for original, port in (([a], [a, a]), ([a, a], []),
                               ([a, a], [a, Sample("bad", 2, 1, a.rgba)]),
                               ([a, a], [a, Sample("short", 1, 1, b"")])):
            with self.subTest(original=original, port=port), self.assertRaises(ValueError):
                compare(original, port)


if __name__ == "__main__":
    unittest.main()
