"""Exercise the production frame envelope in a never-shown editor host."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import cpu_import_fixture

parser = argparse.ArgumentParser()
parser.add_argument('--test', required=True)
parser.add_argument('--backend', required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='mds-editor-frame-') as directory:
    cpu_import_fixture.write(directory)
    root = Path(directory)
    (root / 'triangle.pmx').write_bytes(cpu_import_fixture.pmx(material_flags=0x1d))
    (root / 'screen.x').write_text(
        (root / 'bound.x').read_text(encoding='ascii').replace('checker.bmp', 'screen.bmp'),
        encoding='ascii')
    for name in ('triangle.pmd', 'triangle.pmx'):
        subprocess.run([args.test, args.backend, str(root / name), str(root / 'screen.x')],
                       check=True, timeout=150)
