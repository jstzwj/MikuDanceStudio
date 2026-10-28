"""Format the project's C/C++ sources, or check their clang-format style."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRS = (ROOT / "include" / "mikudancestudio", ROOT / "src")
SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc"}


def find_formatter(explicit):
    if explicit:
        command = shutil.which(explicit)
        if command:
            return Path(command)
        raise FileNotFoundError(f"clang-format executable not found: {explicit}")

    # Match the VS Code C/C++ extension's default formatter when installed.
    executable = "clang-format.exe" if os.name == "nt" else "clang-format"
    for editor in (".vscode", ".vscode-insiders"):
        extensions = Path.home() / editor / "extensions"
        candidates = sorted(
            extensions.glob(f"ms-vscode.cpptools-*/LLVM/bin/{executable}"),
            key=lambda path: path.stat().st_mtime,
            reverse=True,
        )
        if candidates:
            return candidates[0]

    command = shutil.which("clang-format")
    if command:
        return Path(command)

    if os.name == "nt":
        # Visual Studio's optional C++ Clang tools component provides this too.
        for variable in ("ProgramFiles", "ProgramFiles(x86)"):
            program_files = os.environ.get(variable)
            if not program_files:
                continue
            installations = Path(program_files) / "Microsoft Visual Studio"
            candidates = sorted(installations.glob("*/*/VC/Tools/Llvm/bin/clang-format.exe"))
            if candidates:
                return candidates[-1]

    raise FileNotFoundError(
        "Install the VS Code C/C++ extension or clang-format 19+, "
        "or pass --clang-format PATH."
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check without modifying files")
    parser.add_argument("--clang-format", metavar="PATH", help="Use a specific clang-format executable")
    args = parser.parse_args()
    try:
        formatter = find_formatter(args.clang_format)
    except FileNotFoundError as error:
        parser.error(str(error))

    files = sorted(
        str(path.relative_to(ROOT))
        for directory in SOURCE_DIRS
        for path in directory.rglob("*")
        if path.is_file() and path.suffix.lower() in SOURCE_EXTENSIONS
    )
    subprocess.run([str(formatter), "--version"], check=True)
    options = ["--style=file", "--fallback-style=none"]
    options += ["--dry-run", "--Werror"] if args.check else ["-i"]
    # Keep each invocation below Windows' command-line length limit.
    result = 0
    for start in range(0, len(files), 40):
        completed = subprocess.run(
            [str(formatter), *options, *files[start:start + 40]], cwd=ROOT
        )
        if completed.returncode:
            result = completed.returncode
    if result == 0:
        action = "Checked" if args.check else "Formatted"
        print(f"{action} {len(files)} C/C++ source files.")
    return result


if __name__ == "__main__":
    sys.exit(main())
