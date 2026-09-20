"""Generate MSVC import-library metadata from the shared D3DX declarations.

The signature object is input to LIB /DEF, never to the application linker.
LIB uses its stdcall decorations to create correct undecorated x86 imports.
No SDK binary, executable address, runtime shim or replacement DLL is needed.
"""
import argparse
from pathlib import Path
import re


X86_WORD_TYPES = frozenset({
    "BOOL", "D3DCOLOR", "D3DFORMAT", "D3DPOOL", "DWORD", "HMODULE",
    "LPCSTR", "LPCVOID", "LPCWSTR", "UINT", "float", "int",
    "unsigned int", "unsigned long",
})
# A type keyword must not be mistaken for an optional parameter name:
# for example, unsigned long long is not unsigned long named "long".
TYPE_KEYWORDS = frozenset({
    "bool", "char", "char8_t", "char16_t", "char32_t", "const", "double",
    "enum", "float", "int", "long", "short", "signed", "struct",
    "union", "unsigned", "void", "volatile", "wchar_t", "__int64",
})


def validate_x86_word_parameter(parameter):
    """Reject declarations whose x86 argument width has not been established."""
    parameter = " ".join(parameter.split())
    # Pointee size does not affect the width of an ordinary x86 pointer.
    # Arrays, references, function pointers and other declarators deliberately
    # require explicit support instead of guessing their calling convention.
    if re.fullmatch(r"[A-Za-z_]\w*(?:\s+[A-Za-z_]\w*)*\s*"
                    r"(?:\*\s*(?:(?:const|volatile)\s*)?)+"
                    r"(?:[A-Za-z_]\w*)?", parameter):
        return
    if parameter in X86_WORD_TYPES:
        return
    parts = parameter.rsplit(" ", 1)
    if (len(parts) == 2 and parts[0] in X86_WORD_TYPES
            and re.fullmatch(r"[A-Za-z_]\w*", parts[1])
            and parts[1] not in TYPE_KEYWORDS):
        return
    raise ValueError(f"Unverified x86 D3DX parameter width: {parameter!r}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("header", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("runtime", choices=("d3dx9_32", "d3dx9_43"))
    args = parser.parse_args()
    source = args.header.read_text(encoding="utf-8-sig")
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    functions = re.findall(r"\bWINAPI\s+(D3DX\w+)\s*\(([^()]*)\)\s*;", source)
    assert len(functions) >= 40, "D3DX declaration surface was not found"
    args.output.mkdir(parents=True, exist_ok=True)
    definitions = [f"LIBRARY {args.runtime}.dll", "EXPORTS"]
    signatures = ["// Build-only signature metadata; never linked into the executable."]
    for name, parameters in functions:
        # All current D3DX arguments are pointers, floats or 32-bit scalars.
        # Reject future by-value 64-bit/aggregate declarations rather than
        # silently producing an incorrect x86 stack decoration.
        parts = [p.strip() for p in parameters.split(",") if p.strip() not in ("", "void")]
        for parameter in parts:
            validate_x86_word_parameter(parameter)
        parameters = ", ".join("int" for _ in parts)
        signatures.append(f'extern "C" void __stdcall {name}({parameters}) {{}}')
        definitions.append(name)
    (args.output / "d3dx_imports.def").write_text("\n".join(definitions) + "\n")
    (args.output / "d3dx_signatures.cpp").write_text("\n".join(signatures) + "\n")


if __name__ == "__main__":
    main()
