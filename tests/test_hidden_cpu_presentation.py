"""Generated CPU models through final-color presentation; always-hidden bounded runs."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import cpu_import_fixture

parser = argparse.ArgumentParser()
parser.add_argument('--preview', required=True)
parser.add_argument('--api', choices=('dx11', 'dx12', 'vulkan'), required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='mds-hidden-present-') as directory:
    cpu_import_fixture.write(directory)
    root = Path(directory)
    (root / 'shadow.pmx').write_bytes(cpu_import_fixture.pmx(0x1f))
    for model in ('triangle.pmd', 'shadow.pmx'):
        for mode in (1, 2):
            # No --lifecycle: that older diagnostic manipulates desktop windows.
            # --frames keeps this host's window hidden for the complete run.
            completed = subprocess.run([
                args.preview, args.api, str(root / model), '--frames', '3',
                '--sampling', 'portable', '--self-shadow', str(mode),
            ], capture_output=True, text=True, timeout=60)
            print(completed.stdout, end='')
            print(completed.stderr, end='')
            completed.check_returncode()
            assert 'sampling=portable self_shadow=1' in completed.stdout
            assert 'PASS independent_rhi frames=3' in completed.stdout
