"""Fetch the pinned official MoltenVK binary for the macOS P0 probe only."""
import argparse
import hashlib
from pathlib import Path
import tarfile
import urllib.request

VERSION = "1.4.2"
SHA256 = "f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e"
MEMBER = "MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / f"MoltenVK-macos-{VERSION}.tar"
    urllib.request.urlretrieve(
        f"https://github.com/KhronosGroup/MoltenVK/releases/download/v{VERSION}/MoltenVK-macos.tar", archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError("MoltenVK archive SHA-256 mismatch")
    # Copy one fixed regular file; do not extract archive paths or symlinks.
    with tarfile.open(archive) as tar:
        member = tar.getmember(MEMBER)
        if not member.isfile():
            raise RuntimeError("MoltenVK library is not a regular file")
        with tar.extractfile(member) as source:
            (args.output / "libMoltenVK.dylib").write_bytes(source.read())
    print(f"MoltenVK {VERSION}: verified {SHA256}")


if __name__ == "__main__":
    main()
