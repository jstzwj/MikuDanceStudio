"""Fetch checksum-pinned official DXC releases. macOS builds pinned upstream source in CI."""
import argparse
import hashlib
import io
from pathlib import Path
import platform
import tarfile
import urllib.request
import zipfile

VERSION = "v1.8.2505.1"
RELEASES = {
    "Windows": ("dxc_2025_07_14.zip", "9ad895a6b039e3a8f8c22a1009f866800b840a74b50db9218d13319e215ea8a4"),
    "Linux": ("linux_dxc_2025_07_14.x86_64.tar.gz", "f2213da1fc99dc8778c8823078e16ba97c7f80f86a1d4520ab1adf4b462bc48c"),
}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    system = platform.system()
    name, checksum = RELEASES[system]
    data = urllib.request.urlopen(f"https://github.com/microsoft/DirectXShaderCompiler/releases/download/{VERSION}/{name}", timeout=120).read()
    if hashlib.sha256(data).hexdigest() != checksum:
        raise RuntimeError("DXC archive checksum mismatch")
    args.output.mkdir(parents=True, exist_ok=True)
    # Extract an explicit allowlist, preserving the compiler's bin/../lib rpath.
    if system == "Windows":
        archive = zipfile.ZipFile(io.BytesIO(data))
        for item in ("dxc.exe", "dxcompiler.dll", "dxil.dll"):
            (args.output / item).write_bytes(archive.read(f"bin/x64/{item}"))
        for item in ("LICENSE-LLVM.txt", "LICENSE-MS.txt", "ReleaseNotes.md"):
            if item in archive.namelist(): (args.output / item).write_bytes(archive.read(item))
    else:
        archive = tarfile.open(fileobj=io.BytesIO(data), mode="r:gz")
        for item in ("bin/dxc", "lib/libdxcompiler.so", "lib/libdxil.so", "LICENSE-LLVM.txt", "LICENSE-MS.txt"):
            member = next((m for m in archive if m.name.lstrip("./") == item), None)
            if member is None or not member.isfile(): raise RuntimeError(f"missing regular file: {item}")
            target = args.output / item
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.extractfile(member).read())
            target.chmod(0o755 if item.startswith(("bin/", "lib/")) else 0o644)
    print(f"DXC {VERSION}: SHA256 verified")

if __name__ == "__main__": main()
