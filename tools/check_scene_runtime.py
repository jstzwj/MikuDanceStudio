"""Run public P2 scene fixtures, independent-process repeats and frozen CPU snapshots."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
import cpu_import_fixture
from check_model_runtime import compare

SCENES = ['v1', 'v2', 'pmx', 'ik-v1', 'physics-v2', 'sdef-physics-v2',
          'bound-v1', 'bound-v2', 'passes-v2']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--record-reference', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    assets = output / '\u573a\u666f_\u30df\u30af'
    cpu_import_fixture.write(assets)
    japanese = '\u30bb\u30f3\u30bf\u30fc.pmd'
    (assets / japanese).write_bytes(cpu_import_fixture.pmd())
    (assets / 'japanese.pmm').write_bytes(cpu_import_fixture.scene(2, japanese))
    scenes = SCENES + ['japanese']
    reference = ROOT / 'tests/data/p2_scene_v1.json'
    expected = None if args.record_reference else json.loads(reference.read_text(encoding='utf-8'))
    result = {'schema': 1, 'input_sha256': {}, 'cases': {}}
    maximum = 0
    for name in scenes:
        path = assets / (name + '.pmm')
        result['input_sha256'][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
        for mode in ([0] if name.endswith('v1') or name == 'v1' else [0, 1, 2, 3]):
            case = f'{name}/mode-{mode}'
            command = [str(args.program.resolve()), '--input', str(path), '--physics-mode', str(mode),
                       '--frame', '0', '--frame', '3', '--frame', '10', '--frame', '15', '--frame', '0',
                       '--play-frames', '30']
            actual = json.loads(subprocess.check_output(command))
            if actual['status'] != 'passed' or len(actual['snapshots']) != 6:
                raise RuntimeError(f'{case}: incomplete CPU scene chain')
            repeated = json.loads(subprocess.check_output(command))
            if actual != repeated:
                raise RuntimeError(f'{case}: separate-process trajectory is not repeatable')
            result['cases'][case] = actual
            if expected is not None:
                maximum = max(maximum, compare(actual, expected['cases'][case], case))
            saved = assets / f'{name}-{mode}.pmm'
            subprocess.run(command + ['--save', str(saved), '--output', str(output / 'latest.json')], check=True)
            reloaded = json.loads(subprocess.check_output([str(args.program.resolve()), '--input', str(saved)] + command[5:]))
            compare(reloaded, actual, case + '/saved')
    if args.record_reference:
        reference.write_text(json.dumps(result, separators=(',', ':')) + '\n', encoding='utf-8')
    elif result['input_sha256'] != expected['input_sha256']:
        raise RuntimeError('public PMM inputs changed without a reviewed baseline')
    (output / 'snapshots.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    report = {'schema': 1, 'stage': 'P2', 'status': 'passed', 'cases': len(result['cases']),
              'strict_relative_tolerance': 2e-5, 'maximum_absolute_error': maximum,
              'reference_sha256': hashlib.sha256(reference.read_bytes()).hexdigest(),
              'checks': ['public PMD/PMX, IK, SDEF, physics modes 0..3, external parents, accessories',
                         'load, seek, playback, reset, PMM save/reload',
                         'independent-process exact repeats', 'CP932 references in Unicode directories']}
    (output / 'p2-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'P2 passed: {len(result["cases"])} scene/mode cases, maximum absolute error={maximum:.9g}')


if __name__ == '__main__':
    main()
