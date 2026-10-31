"""Check the vendored source tree against the supplied Bullet 2.75 archive."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def canonical_bytes(data):
    return data.replace(b"\r\n", b"\n")


def verify_sources(source_root, manifest_path, archive=None):
    source_root = Path(source_root)
    manifest = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    expected = manifest["files"]
    actual = {path.relative_to(source_root).as_posix(): path
              for path in source_root.rglob("*") if path.is_file()}
    if actual.keys() != expected.keys():
        raise ValueError(f"Upstream file set changed: missing={sorted(expected.keys() - actual.keys())}, "
                         f"extra={sorted(actual.keys() - expected.keys())}")
    for relative, digest in expected.items():
        if hashlib.sha256(canonical_bytes(actual[relative].read_bytes())).hexdigest() != digest:
            raise ValueError(f"Upstream Bullet source changed: {relative}")
    if archive is not None:
        archive = Path(archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != manifest["archive_sha256"]:
            raise ValueError("Bullet archive SHA-256 differs from the recorded original")
        with zipfile.ZipFile(archive) as upstream:
            prefix = manifest["archive_prefix"]
            files = {name[len(prefix):]: name for name in upstream.namelist()
                     if name.startswith(prefix) and not name.endswith("/")}
            if files.keys() != expected.keys():
                raise ValueError("Bullet archive source file set differs from the manifest")
            for relative, name in files.items():
                if canonical_bytes(upstream.read(name)) != canonical_bytes(actual[relative].read_bytes()):
                    raise ValueError(f"Source differs from the original archive: {relative}")
    return len(expected)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).parent / "bullet-src/src")
    parser.add_argument("--manifest", type=Path, default=Path(__file__).parent / "upstream.json")
    parser.add_argument("--archive", type=Path)
    args = parser.parse_args()
    count = verify_sources(args.source, args.manifest, args.archive)
    print(f"Verified {count} unmodified Bullet 2.75 source files (line endings normalized).")
