"""Generated CPU X/PMM screen textures and native DX9 capture; no visible windows."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import os
import cpu_import_fixture

parser = argparse.ArgumentParser()
parser.add_argument('--test', required=True)
parser.add_argument('--assets', required=True)
parser.add_argument('--backend', required=True)
parser.add_argument('--external', choices=('pmd', 'pmx'))
parser.add_argument('--preview', action='store_true')
args = parser.parse_args()
model = None
if args.external:
    model = os.environ.get('MDS_SELF_SHADOW_' + args.external.upper())
    if not model:
        print('SKIP: configure MDS_SELF_SHADOW_' + args.external.upper())
        raise SystemExit(77)
with tempfile.TemporaryDirectory(prefix='mds-screen-') as directory:
    cpu_import_fixture.write(directory)
    path = Path(directory) / 'bound.x'
    path.write_text(path.read_text(encoding='ascii').replace('checker.bmp', 'screen.bmp'), encoding='ascii')
    if args.preview:
        (Path(directory) / 'bound-v2.pmm').write_bytes(cpu_import_fixture.scene(
            2, 'articulated.pmd', True, bindings=True, capture_mode=1))
    (Path(directory) / 'bound-pmx.pmm').write_bytes(cpu_import_fixture.scene(2, 'articulated.pmx', True, bindings=True))
    command = ([args.test, args.backend, str(Path(directory) / 'bound-v2.pmm'),
                '--scene-passes', '--self-shadow', '1', '--frames', '4']
               if args.preview else [args.test, directory, args.assets, args.backend])
    if model:
        command.append(model)
    completed = subprocess.run(command, timeout=150)
    raise SystemExit(completed.returncode)
