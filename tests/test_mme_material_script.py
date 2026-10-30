"""Generate public PMD/PMX geometry; native oracles never show a window."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import cpu_import_fixture

parser = argparse.ArgumentParser()
parser.add_argument('--test', required=True)
parser.add_argument('--backend', required=True)
parser.add_argument('--ray-effect', type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='mds-material-script-') as directory:
    cpu_import_fixture.write(directory)
    for name in ('triangle.pmd', 'triangle.pmx'):
        command = [args.test, args.backend, str(Path(directory) / name)]
        if args.ray_effect:
            command.append(str(args.ray_effect.resolve(strict=True).parent /
                               'Materials/material_2.0.fx'))
        result = subprocess.run(command, check=False, timeout=150)
        if result.returncode:
            raise SystemExit(result.returncode)
