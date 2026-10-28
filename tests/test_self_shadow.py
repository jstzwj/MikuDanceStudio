"""Offscreen PMD/PMX producer and stock DX9 effect comparison; no GUI automation."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import cpu_import_fixture

parser = argparse.ArgumentParser()
parser.add_argument('--test', required=True)
parser.add_argument('--assets', required=True)
parser.add_argument('--variant', default='dx11-warp')
parser.add_argument('--external', choices=('pmd', 'pmx'))
parser.add_argument('--sampling', choices=('native', 'portable'), default='native')
mode = parser.add_mutually_exclusive_group()
mode.add_argument('--strict', action='store_true')
mode.add_argument('--tolerant', action='store_true')
args = parser.parse_args()

def run(model):
    comparison = 'strict' if args.strict else 'tolerant' if args.tolerant else 'measure'
    completed = subprocess.run([
        args.test, str(model), args.assets, args.variant, comparison, '', args.sampling,
    ], timeout=120)
    if completed.returncode == 77:
        raise SystemExit(77)
    completed.check_returncode()

if args.external:
    path = os.environ.get('MDS_SELF_SHADOW_' + args.external.upper())
    if not path:
        print('SKIP: configure MDS_SELF_SHADOW_' + args.external.upper())
        raise SystemExit(77)
    run(Path(path))
else:
    with tempfile.TemporaryDirectory(prefix='mds-self-shadow-') as directory:
        cpu_import_fixture.write(directory)
        root = Path(directory)
        (root / 'shadow.pmx').write_bytes(cpu_import_fixture.pmx(0x1f))
        (root / 's.spa').write_bytes((root / 's.sph').read_bytes())
        (root / 'combined.pmd').write_bytes(cpu_import_fixture.pmd('checker.bmp*s.sph'))
        (root / 'additive.pmd').write_bytes(cpu_import_fixture.pmd('s.spa'))
        for name in ('triangle.pmd', 'shadow.pmx', 'sphere.pmd', 'combined.pmd', 'additive.pmd'):
            run(root / name)
