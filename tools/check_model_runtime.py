"""Run the P1 public model chain, frozen numeric checks and failure cases."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess

from generate_p1_fixture import generate

ROOT = Path(__file__).resolve().parents[1]


def compare(actual, expected, location='snapshot'):
    if isinstance(expected, dict):
        if actual.keys() != expected.keys():
            raise RuntimeError(f'{location}: fields differ')
        return max((compare(actual[k], v, f'{location}.{k}') for k, v in expected.items()), default=0)
    if isinstance(expected, list):
        if len(actual) != len(expected):
            raise RuntimeError(f'{location}: length differs')
        return max((compare(a, b, f'{location}[{n}]') for n, (a, b) in enumerate(zip(actual, expected))), default=0)
    if isinstance(expected, (float, int)):
        if not math.isfinite(actual) or abs(actual - expected) > 2e-5 * max(1, abs(expected)):
            raise RuntimeError(f'{location}: {actual} != {expected}')
        return abs(actual - expected)
    if actual != expected:
        raise RuntimeError(f'{location}: {actual} != {expected}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    # Exercise native filesystem paths independently of track-name encodings.
    model, motion = generate(output / '\u6a21\u578b_\u30df\u30af')
    expected = json.loads((ROOT / 'tests/data/p1_model_v1.json').read_text(encoding='utf-8'))
    inputs = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in [model, motion]}
    if inputs != expected['input_sha256']:
        raise RuntimeError('public PMX/VMD input hashes changed')
    program = str(args.program.resolve())
    capabilities = json.loads(subprocess.check_output([program, '--capabilities']))
    if capabilities['stage'] != 'P1' or set(capabilities['unsupported']) != set(expected['unsupported']):
        raise RuntimeError('P1 capability scope changed without a reviewed baseline')
    frames = [0, 4, 8, 12, 4, 0, 8]
    command = [program, '--model', str(model), '--motion', str(motion)]
    for frame in frames:
        command += ['--frame', str(frame)]
    actual = json.loads(subprocess.check_output(command))
    if actual['status'] != 'passed' or actual['warnings']:
        raise RuntimeError('public model chain did not complete cleanly')
    if len(actual['snapshots']) != len(frames):
        raise RuntimeError('missing snapshots')
    maximum = 0
    for frame, snapshot in zip(frames, actual['snapshots']):
        maximum = max(maximum, compare(snapshot, expected['frames'][str(frame)]))
    repeated = subprocess.check_output(command)
    if json.loads(repeated) != actual:
        raise RuntimeError('separate process evaluation is not repeatable')
    for frame in (0, 4, 8):
        snapshots = [s for s in actual['snapshots'] if s['frame'] == frame]
        if any(s != snapshots[0] for s in snapshots):
            raise RuntimeError('backward/repeated seek accumulated state')
    # Identity pose is exact; trigonometric/intermediate matrices use the
    # explicitly declared numeric threshold, never an image comparison.
    if actual['snapshots'][0] != expected['frames']['0']:
        raise RuntimeError('identity pose differs from exact baseline')
    bad_model = output / 'truncated.pmx'
    bad_motion = output / 'truncated.vmd'
    bad_model.write_bytes(model.read_bytes()[:128])
    bad_motion.write_bytes(motion.read_bytes()[:64])
    failures = [(['--model', str(bad_model), '--motion', str(motion), '--frame', '0'], 'model-read-failed'),
                (['--model', str(model), '--motion', str(bad_motion), '--frame', '0'], 'motion-read-failed'),
                (['--model', str(model), '--motion', str(motion), '--frame', '-1'], 'invalid-frame'),
                (['--model', str(model), '--motion', str(motion), '--frame', '0', '--frame', '4294967295'], 'invalid-frame')]
    for n, (options, reason) in enumerate(failures):
        destination = output / f'rejected-{n}.json'
        if destination.exists():
            destination.unlink()  # our generated test output only
        result = subprocess.run([program, *options, '--output', str(destination)], capture_output=True)
        if result.returncode == 0 or destination.exists() or reason not in result.stderr.decode('utf-8'):
            raise RuntimeError(f'failed import/seek published a result: {reason}')
    report = {'schema': 1, 'stage': 'P1', 'status': 'passed', 'input_sha256': inputs,
              'frames': frames, 'max_absolute_error': maximum,
              'numeric_threshold': '2e-5 * max(1, abs(reference)); identity exact',
              'capabilities': capabilities, 'repeatable': True, 'negative_cli_cases': len(failures)}
    (output / 'snapshots.json').write_text(json.dumps(actual, indent=2) + '\n', encoding='utf-8')
    (output / 'p1-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'P1 passed: seven seeks, two processes, four rejection cases; max error {maximum}')


if __name__ == '__main__':
    main()
