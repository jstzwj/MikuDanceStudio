"""Negative acceptance cases: offline image parity must not imply live coverage."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from run_cpu_geometry_diagnostic import LIVE_PHASES, live_history_exact


def complete_capture():
    groups = {}
    for index, (phase, (version, playing)) in enumerate(LIVE_PHASES.items(), 1):
        owner = '101' if version == 'v1' else '202'
        common = dict(phase=phase, pmmVersion=version, playing=playing,
                      process=owner, status='PASS', differentPixels='0', draws='1')
        rows = [dict(common, boundary=str(n), backend=backend, family='clear' if n == 0 else 'model_material')
                for n in range(2) for backend in ('dx11', 'dx12')]
        rows.append(dict(common, boundary='1', backend='both', family='lifecycle'))
        groups[(owner, str(index))] = rows
    return groups


class LiveEvidenceTest(unittest.TestCase):
    def test_complete_captures(self):
        self.assertTrue(live_history_exact(complete_capture()))

    def test_stopped_frame_labelled_playing(self):
        groups = complete_capture()
        for rows in groups.values():
            if rows[0]['phase'] == 'playing':
                for row in rows:
                    row['playing'] = '0'
        self.assertFalse(live_history_exact(groups))

    def test_partial_or_inconsistent_capture(self):
        base = complete_capture()
        changes = (
            lambda rows: rows.pop(1),
            lambda rows: rows.pop(),
            lambda rows: rows[2].update(differentPixels='1'),
            lambda rows: rows[2].update(pmmVersion='v2'),
            lambda rows: rows[2].update(playing='1'),
            lambda rows: rows[2].update(status='BLOCKED'),
            lambda rows: rows.reverse(),
        )
        for change in changes:
            with self.subTest(change=change):
                groups = copy.deepcopy(base)
                change(next(iter(groups.values())))
                self.assertFalse(live_history_exact(groups))

    def test_missing_reopen_or_duplicate_phase(self):
        groups = complete_capture()
        groups.pop(next(reversed(groups)))
        self.assertFalse(live_history_exact(groups))
        groups = complete_capture()
        groups[('101', '99')] = copy.deepcopy(next(iter(groups.values())))
        self.assertFalse(live_history_exact(groups))


if __name__ == '__main__':
    unittest.main()
