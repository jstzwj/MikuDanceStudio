"""Public live-editor import regression; every parent window remains hidden."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import cpu_import_fixture

parser = argparse.ArgumentParser()
parser.add_argument('--test', required=True)
parser.add_argument('--backend', required=True)
parser.add_argument('--external', choices=('pmd', 'pmx'))
args = parser.parse_args()

def run(path, accessories=()):
    subprocess.run([args.test, args.backend, str(path), *map(str, accessories)],
                   check=True, timeout=150)

if args.external:
    path = os.environ.get('MDS_SELF_SHADOW_' + args.external.upper())
    if not path:
        print('SKIP: configure MDS_SELF_SHADOW_' + args.external.upper())
        raise SystemExit(77)
    run(Path(path))
else:
    with tempfile.TemporaryDirectory(prefix='mds-live-editor-') as directory:
        cpu_import_fixture.write(directory)
        root = Path(directory)
        (root / 'screen.x').write_text(
            (root / 'bound.x').read_text(encoding='ascii').replace('checker.bmp', 'screen.bmp'),
            encoding='ascii')
        for name in ('triangle.pmd', 'triangle.pmx'):
            run(root / name, (root / 'bound.x', root / 'screen.x'))
