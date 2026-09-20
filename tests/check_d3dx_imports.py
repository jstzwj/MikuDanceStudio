"""Check normal (not delay) PE imports without third-party Python modules."""
import argparse
from pathlib import Path
import struct


def imports(path):
    data = Path(path).read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[pe:pe + 4] == b"PE\0\0"
    machine, sections = struct.unpack_from("<HH", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    directories = optional + (112 if machine == 0x8664 else 96)
    table = optional + optional_size

    def offset(rva):
        for i in range(sections):
            size, start, raw_size, raw = struct.unpack_from("<IIII", data, table + 40 * i + 8)
            if start <= rva < start + max(size, raw_size):
                return raw + rva - start
        raise AssertionError(f"Unmapped RVA: {rva:x}")

    def string(rva):
        start = offset(rva)
        return data[start:data.index(b"\0", start)].decode("ascii")

    normal = {}
    import_rva = struct.unpack_from("<I", data, directories + 8)[0]
    cursor = offset(import_rva)
    while True:
        thunk, _, _, name, first_thunk = struct.unpack_from("<IIIII", data, cursor)
        if not name:
            break
        dll = string(name).lower()
        names = []
        position = offset(thunk or first_thunk)
        width, format = (8, "<Q") if machine == 0x8664 else (4, "<I")
        while value := struct.unpack_from(format, data, position)[0]:
            if not value & (1 << (width * 8 - 1)):
                names.append(string(value + 2))
            position += width
        normal[dll] = names
        cursor += 20
    delayed = []
    delay_rva = struct.unpack_from("<I", data, directories + 13 * 8)[0]
    if delay_rva:
        cursor = offset(delay_rva)
        while name := struct.unpack_from("<I", data, cursor + 4)[0]:
            delayed.append(string(name).lower())
            cursor += 32
    return machine, normal, delayed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    args = parser.parse_args()
    machine, normal, delayed = imports(args.executable)
    runtime = "d3dx9_43.dll" if machine == 0x8664 else "d3dx9_32.dll"
    assert runtime in normal, f"Required normal import absent: {runtime}"
    assert not any("d3dx" in name for name in delayed), delayed
    assert not {"mmeffect.dll", "mmhack.dll"} & normal.keys()
    assert {"D3DXMatrixMultiply", "D3DXMatrixTranspose"} <= set(normal[runtime])
    print(f"PASS: {runtime}, {len(normal[runtime])} normal imports, no D3DX delay load")


if __name__ == "__main__":
    main()
