"""Run a direct-import probe with an isolated incomplete-runtime fixture."""
import argparse
import ctypes
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("probe", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("runtime", choices=("d3dx9_32", "d3dx9_43"))
    args = parser.parse_args()
    # Child inherits the error mode: a loader error must not open a modal UI.
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    previous = kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    try:
        with tempfile.TemporaryDirectory(prefix="mmd-d3dx-loader-") as directory:
            folder = Path(directory)
            probe = folder / "probe.exe"
            marker = folder / "main-entered.txt"
            shutil.copyfile(args.probe, probe)
            shutil.copyfile(args.fixture, folder / (args.runtime + ".dll"))
            result = subprocess.run([str(probe), str(marker)], cwd=folder,
                                    creationflags=subprocess.CREATE_NO_WINDOW,
                                    capture_output=True, timeout=20)
            status = result.returncode & 0xFFFFFFFF
            assert status == 0xC0000139, f"Expected STATUS_ENTRYPOINT_NOT_FOUND, got {status:#x}"
            assert not marker.exists(), "Application main ran despite missing required exports"
            print("PASS: Windows rejected incomplete D3DX before main (STATUS_ENTRYPOINT_NOT_FOUND)")
    finally:
        kernel32.SetErrorMode(previous)


if __name__ == "__main__":
    main()
