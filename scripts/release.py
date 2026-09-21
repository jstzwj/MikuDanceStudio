"""Validate a release tag and package one Windows Release build."""
import argparse
import hashlib
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def version():
    value = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[1-9][0-9]*\.[0-9]{2}", value):
        raise ValueError("VERSION must have the form 10.00")
    if int(value.split(".")[0]) > 65535:
        raise ValueError("VERSION major exceeds the Windows version-resource limit")
    return value


def package(build_dir, arch, output):
    release = build_dir / "Release"
    files = {
        "MikuDanceStudio.exe": release / "MikuDanceStudio.exe",
        "Data/MMDxShow.dll": release / "MMDxShow.dll",
        "LICENSE": ROOT / "LICENSE",
        "README.md": ROOT / "README.md",
        "VERSION": ROOT / "VERSION",
        "RELEASES.md": ROOT / "docs/RELEASES.md",
        "licenses/Bullet.txt": ROOT / "recipes/bullet275/LICENSE.txt",
    }
    for source in files.values():
        if not source.is_file():
            raise FileNotFoundError(source)
    # Refuse to label an x86 binary as x64 (or vice versa).
    expected = {"x86": 0x14C, "x64": 0x8664}[arch]
    for name in ("MikuDanceStudio.exe", "Data/MMDxShow.dll"):
        data = files[name].read_bytes()
        offset = int.from_bytes(data[0x3C:0x40], "little")
        if data[offset:offset + 4] != b"PE\0\0" or int.from_bytes(data[offset + 4:offset + 6], "little") != expected:
            raise ValueError(f"Wrong PE architecture: {files[name]}")
    output.mkdir(parents=True, exist_ok=True)
    stem = f"MikuDanceStudio-v{version()}-windows-{arch}"
    archive = output / f"{stem}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
        for name, source in files.items():
            bundle.write(source, f"{stem}/{name}")
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".zip.sha256").write_text(f"{digest}  {archive.name}\n", encoding="ascii")
    print(archive)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", help="Require this exact vMAJOR.MINOR tag to match VERSION")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--arch", choices=("x86", "x64"))
    parser.add_argument("--output", type=Path, default=ROOT / "out/release")
    args = parser.parse_args()
    value = version()
    if args.tag is not None and args.tag != f"v{value}":
        parser.error(f"Tag {args.tag!r} does not match VERSION (v{value})")
    if args.build_dir:
        if not args.arch:
            parser.error("--build-dir requires --arch")
        package(args.build_dir, args.arch, args.output)
    else:
        print(value)


if __name__ == "__main__":
    main()
