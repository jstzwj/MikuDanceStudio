"""Compare a scene_roundtrip_test --reference dump with original MMD 9.32 x64 code.

Requires pefile and Unicorn. The original executable must be supplied locally;
neither it nor its code is redistributed. Win32 UI/CRT calls are deterministic
bridges. PMM field selection, iteration, packing, bone/morph/camera/accessory
seek and bone-key undo/redo execute original code. Graphics calls are sinks;
this checks serialized fields and scene state, not rendered pixels.
"""

import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8,
    UC_X86_REG_R9, UC_X86_REG_RIP, UC_X86_REG_RSP,
    UC_X86_REG_XMM0,
    UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3,
)


def compare(binary, directory, cross_load=False, verify_resaves=False, native_resave_directory=None):
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    if digest != "07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1":
        raise ValueError("unsupported original executable: writer RVAs are pinned to MMD 9.32 x64")
    metadata = json.loads((directory / "input.json").read_text())
    if metadata["pointer_width"] != 8:
        raise ValueError("the original x64 writer requires an x64 fixture dump")
    pe = pefile.PE(str(binary))
    base = 0x7FF7CB420000
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    pages = set()

    def put(address, data):
        if not data:
            return
        for page in range(address & ~4095, (address + len(data) + 4095) & ~4095, 4096):
            if page not in pages:
                uc.mem_map(page, 4096)
                pages.add(page)
        uc.mem_write(address, bytes(data))

    def integer(address, value, width=8):
        put(address, value.to_bytes(width, "little"))

    def read_int(address, width=8):
        return int.from_bytes(uc.mem_read(address, width), "little")

    def string(address, wide=False):
        step = 2 if wide else 1
        result = bytearray()
        for offset in range(0, 65536, step):
            part = bytes(uc.mem_read(address + offset, step))
            if part == b"\0" * step:
                return result.decode("utf-16-le" if wide else "cp932")
            result.extend(part)
        raise ValueError("unterminated string")

    segments = [(base, pe.get_memory_mapped_image(ImageBase=base))]
    blob = (directory / "memory.bin").read_bytes()
    cursor = 0
    while cursor < len(blob):
        address, size = struct.unpack_from("<QQ", blob, cursor)
        cursor += 16
        segments.append((address, blob[cursor:cursor + size]))
        cursor += size
    stack, stub = 0x10000000, 0x11000000
    segments.extend([(stack, bytes(0x20000)), (stub, b"\xc3" * 0x10000)])
    ranges = sorted((address & ~4095, (address + len(data) + 4095) & ~4095)
                    for address, data in segments)
    merged = []
    for start, end in ranges:
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    for start, end in merged:
        uc.mem_map(start, end - start)
        pages.update(range(start, end, 4096))
    for address, data in segments:
        put(address, data)
    bridges = {}
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        for imp in entry.imports:
            address = stub + 16 * (len(bridges) + 1)
            bridges[address] = imp.name.decode() if imp.name else f"ordinal_{imp.ordinal}"
            integer(base + imp.address - pe.OPTIONAL_HEADER.ImageBase, address)
    chunks = []
    calls = {}
    crt = ctypes.CDLL("ucrtbase.dll")
    for name in ("sinf", "acosf"):
        fn = getattr(crt, name)
        fn.argtypes = [ctypes.c_float]
        fn.restype = ctypes.c_float
    heap_cursor = 0x12000000
    put(heap_cursor, bytes(0x10000))
    device = 0x13000000
    put(device, bytes(0x2000))
    integer(device, device + 0x1000)
    for index in range(128):
        integer(device + 0x1000 + 8 * index, stub + 0xFFE0)
    integer(metadata["renderer"] + metadata["device_offset"], device)
    d3dx = ctypes.WinDLL("d3dx9_43.dll")
    projection = d3dx.D3DXMatrixPerspectiveFovLH
    projection.argtypes = [ctypes.POINTER(ctypes.c_float)] + [ctypes.c_float] * 4
    projection.restype = ctypes.c_void_p
    phase = "save"
    extension = {}

    def hook(machine, address, size, data):
        nonlocal heap_cursor
        if extension.get("handler") and extension["handler"](address):
            return
        if address == stub:
            machine.emu_stop()
            return
        if address == stub + 0xFFE0:
            # Graphics sink: SetTransform has no effect on the scene-state fields.
            if machine.reg_read(UC_X86_REG_RDX) != 3:
                raise ValueError("unexpected device call")
            machine.reg_write(UC_X86_REG_RAX, 0)
            return
        if address == base + 0x9290:
            # WideToSjis(this, destination, source, capacity), ASCII fixture paths/names.
            dest = machine.reg_read(UC_X86_REG_RDX)
            source = machine.reg_read(UC_X86_REG_R8)
            capacity = machine.reg_read(UC_X86_REG_R9)
            text = string(source, True).encode("cp932")
            if text:
                put(dest, text[:capacity - 1] + b"\0")
            machine.reg_write(UC_X86_REG_RAX, dest)
            machine.reg_write(UC_X86_REG_RIP, stub + 0xFFF0)
            return
        # The cookie checks process integrity, not serialized behavior.
        if address == cookie:
            machine.reg_write(UC_X86_REG_RIP, stub + 0xFFF0)
            return
        if address in (base + 0x11A718, base + 0x11A720):
            name = "sinf" if address == base + 0x11A718 else "acosf"
            bits = machine.reg_read(UC_X86_REG_XMM0) & 0xFFFFFFFF
            value = struct.unpack("<f", struct.pack("<I", bits))[0]
            result = getattr(crt, name)(value)
            machine.reg_write(UC_X86_REG_XMM0, struct.unpack("<I", struct.pack("<f", result))[0])
            machine.reg_write(UC_X86_REG_RIP, stub + 0xFFF0)
            return
        if address in (base + 0xDEA6C, base + 0x11A726, base + 0x11A738):
            a, b, c = [machine.reg_read(reg) for reg in
                       (UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8)]
            if address == base + 0xDEA6C:
                result = heap_cursor
                heap_cursor += (a + 15) & ~15
                if heap_cursor > 0x12010000:
                    raise ValueError("oracle allocation limit")
            else:
                put(a, bytes(machine.mem_read(b, c)) if address == base + 0x11A726
                    else bytes([b & 255]) * c)
                result = a
            machine.reg_write(UC_X86_REG_RAX, result)
            machine.reg_write(UC_X86_REG_RIP, stub + 0xFFF0)
            return
        if address not in bridges:
            return
        name = bridges[address]
        calls[name] = calls.get(name, 0) + 1
        a, b, c, d = [machine.reg_read(reg) for reg in
                      (UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_R9)]
        result = 0
        if name == "_wsopen_s":
            integer(a, 3, 4)
        elif name == "_write":
            caller = read_int(machine.reg_read(UC_X86_REG_RSP)) - base
            if c > 10000 or b == 0:
                raise ValueError(f"invalid write at {caller:#x}: {b:#x}, {c:#x}")
            if (b & ~4095) not in pages:
                raise ValueError(f"unmapped write at {caller:#x}: {b:#x}, {c:#x}")
            chunks.append((caller, bytes(machine.mem_read(b, c))))
            result = c
        elif name == "GetDlgItem":
            result = b
        elif name == "SendMessageA":
            # The fixture has camera + two model rows, and one accessory row.
            if b == 0x146:  # CB_GETCOUNT
                result = 1 if a == 471 else 3
            elif b == 0x148:  # CB_GETLBTEXT
                put(d, b"triangle\0")
                result = 8
            elif b not in (0xB1, 0xC2) and phase == "save":
                raise ValueError(f"unexpected UI message {b:#x}")
        elif name == "GetWindowTextA":
            put(b, b"0\0")
            result = 1
        elif name == "atoi":
            result = int(string(a) or "0")
        elif name == "strcpy_s":
            put(a, string(c).encode("cp932") + b"\0")
        elif name == "sprintf_s":
            fmt = string(c)
            if fmt == "%d":
                fmt = str(d)
            elif fmt == "%3d":
                fmt = f"{d:3d}"
            elif "%" in fmt:
                raise ValueError(f"unsupported sprintf format: {fmt}")
            put(a, fmt.encode("cp932") + b"\0")
            result = len(fmt)
        elif name in ("_close", "swprintf_s", "SetWindowTextW", "MessageBeep", "InvalidateRect"):
            # Empty disabled-media paths and the post-save title have no payload effect.
            pass
        elif name == "GetClientRect":
            put(b, struct.pack("<4i", 0, 0, 900, 900))
            result = 1
        elif name.startswith("??_V") or name == "EnableWindow":
            pass
        elif name == "GetWindowTextLengthA":
            result = 1
        elif name == "D3DXMatrixPerspectiveFovLH":
            arguments = [struct.unpack("<f", struct.pack("<I", machine.reg_read(reg) & 0xFFFFFFFF))[0]
                         for reg in (UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3)]
            arguments.append(struct.unpack("<f", machine.mem_read(
                machine.reg_read(UC_X86_REG_RSP) + 40, 4))[0])
            matrix = (ctypes.c_float * 16)()
            projection(matrix, *arguments)
            put(a, bytes(matrix))
            result = a
        else:
            raise ValueError(f"unexpected original call: {name}")
        machine.reg_write(UC_X86_REG_RAX, result)

    cookie_call = base + 0x78E1B
    cookie = cookie_call + 5 + struct.unpack("<i", uc.mem_read(cookie_call + 1, 4))[0]
    uc.hook_add(UC_HOOK_CODE, hook, begin=stub, end=stub + 0xFFFF)
    uc.hook_add(UC_HOOK_CODE, hook, begin=base + 0x9290, end=base + 0x9290)
    uc.hook_add(UC_HOOK_CODE, hook, begin=cookie, end=cookie)
    for rva in (0x11A718, 0x11A720, 0xDEA6C, 0x11A726, 0x11A738):
        uc.hook_add(UC_HOOK_CODE, hook, begin=base + rva, end=base + rva)
    rsp = stack + 0x1E008
    integer(rsp, stub)
    uc.reg_write(UC_X86_REG_RSP, rsp)
    uc.reg_write(UC_X86_REG_RCX, metadata["app"])
    try:
        print(f"mapped {len(blob)} input bytes; executing original writer", flush=True)
        uc.emu_start(base + 0x750A0, stub, timeout=30000000)
    except Exception:
        print(f"original RIP: {uc.reg_read(UC_X86_REG_RIP):#x}")
        raise
    if uc.reg_read(UC_X86_REG_RIP) != stub:
        raise RuntimeError(f"original writer did not return: RIP={uc.reg_read(UC_X86_REG_RIP):#x}, calls={calls}")
    expected = (directory / "port.pmm").read_bytes()
    original = b"".join(payload for _, payload in chunks)
    (directory / "original.pmm").write_bytes(original)
    mismatches, ignored = [], []
    offset = 0
    string_writes = {0x751F4: 30, 0x754EF: 256, 0x77710: 100, 0x7777A: 100,
                     0x777D9: 256, 0x78199: 256, 0x78255: 256, 0x78328: 256}
    for caller, payload in chunks:
        actual = expected[offset:offset + len(payload)]
        # Original stack padding after fixed-width strings is unspecified.
        meaningful = len(payload)
        if caller in string_writes:
            if len(payload) != string_writes[caller]:
                raise ValueError("unexpected fixed-width string write")
        if caller in string_writes and b"\0" in payload:
            meaningful = payload.index(0) + 1
        if actual[:meaningful] != payload[:meaningful]:
            mismatches.append({"offset": offset, "caller_rva": hex(caller),
                               "original": payload.hex(), "port": actual.hex()})
        elif actual != payload:
            ignored.append({"offset": offset, "length": len(payload), "caller_rva": hex(caller)})
        offset += len(payload)
    if offset != len(expected):
        mismatches.append({"original_size": offset, "port_size": len(expected)})
    def invoke(rva, a, b=0, c=0, d=0):
        integer(rsp, stub)
        for reg, value in ((UC_X86_REG_RSP, rsp), (UC_X86_REG_RCX, a),
                           (UC_X86_REG_RDX, b), (UC_X86_REG_R8, c), (UC_X86_REG_R9, d)):
            uc.reg_write(reg, value)
        uc.emu_start(base + rva, stub, timeout=30000000)
        if uc.reg_read(UC_X86_REG_RIP) != stub:
            raise RuntimeError(f"original {rva:#x} did not return")

    frame_comparisons = []
    phase = "state"
    def pose_bytes():
        bones = b"".join(bytes(uc.mem_read(metadata["bone_table"] +
                         i * metadata["bone_stride"] + metadata["pose_offset"], 28))
                         for i in range(metadata["bone_count"]))
        morphs = b"".join(bytes(uc.mem_read(metadata["morph_table"] +
                          i * metadata["morph_stride"] + metadata["morph_value_offset"], 4))
                          for i in range(metadata["morph_count"]))
        return bones + morphs

    def float_error(actual, expected, count):
        if len(actual) != len(expected):
            raise ValueError("fixed-frame output length mismatch")
        pairs = list(zip(struct.unpack(f"<{count}f", actual[:count * 4]),
                         struct.unpack(f"<{count}f", expected[:count * 4])))
        if not all(math.isfinite(a) and math.isfinite(b) for a, b in pairs):
            raise ValueError("non-finite fixed-frame output")
        return max((abs(a - b) for a, b in pairs), default=0.0)

    for item in metadata["fixed_frames"]:
        invoke(0xCBD90, metadata["model"], item["frame"], 2)
        integer(metadata["frame_address"], item["frame"], 4)
        invoke(0x59C30, metadata["app"])
        invoke(0x5E150, metadata["app"], 3)
        actual = pose_bytes()
        expected_pose = bytes.fromhex(item["pose"])
        error = float_error(actual, expected_pose, len(actual) // 4)
        frame_comparisons.append({"frame": item["frame"], "bit_exact": actual == expected_pose,
                                  "max_error": error})
        if actual != expected_pose:
            mismatches.append({"fixed_frame": item["frame"], "error": error})
        camera = b"".join(bytes(uc.mem_read(address, width)) for address, width in
                          zip(metadata["camera_fields"], (12, 12, 4, 4, 4, 4, 1)))
        camera_expected = bytes.fromhex(item["camera"])
        camera_error = float_error(camera, camera_expected, 8)
        frame_comparisons[-1].update(camera_bit_exact=camera == camera_expected,
                                     camera_max_error=camera_error)
        if camera != camera_expected:
            mismatches.append({"camera_frame": item["frame"], "error": camera_error})
        accessory = b"".join(bytes(uc.mem_read(address, width)) for address, width in
                             zip(metadata["accessory_fields"], (12, 12, 4, 4, 4, 4, 1, 1)))
        accessory_expected = bytes.fromhex(item["accessory"])
        accessory_error = float_error(accessory, accessory_expected, 8)
        frame_comparisons[-1].update(accessory_bit_exact=accessory == accessory_expected,
                                     accessory_max_error=accessory_error)
        if accessory != accessory_expected:
            mismatches.append({"accessory_frame": item["frame"], "error": accessory_error})
    invoke(0xCBD90, metadata["model"], 24, 2)
    frame_address = stack + 0x100
    integer(frame_address, 24, 4)
    undo_comparisons = []
    for name, rva in (("undo", 0xCEF90), ("redo", 0xCFB80)):
        invoke(rva, metadata["model"], frame_address)
        key_address = read_int(metadata["key_pointer_address"])
        actual_keys = bytes(uc.mem_read(key_address, metadata["key_bytes"])).hex()
        expected_undo = metadata[name]
        matches = not (actual_keys != expected_undo["keys"] or
                pose_bytes().hex() != expected_undo["pose"] or
                read_int(metadata["undo_cursor_address"], 4) != expected_undo["cursor"] or
                read_int(frame_address, 4) != expected_undo["frame"])
        undo_comparisons.append({"operation": name, "matches": matches})
        if not matches:
            mismatches.append({"operation": name, "keys_match": actual_keys == expected_undo["keys"]})
    report = {"sha256": digest,
              "original_writer_rva": "0x750A0", "bytes": len(original),
              "original_state_rvas": {"model_seek": "0xcbd90", "camera_seek": "0x59c30",
                                      "accessory_seek": "0x5e150", "undo": "0xcef90", "redo": "0xcfb80"},
              "writes": len(chunks), "bridges": calls, "ignored_string_padding": ignored,
              "fixed_frames": frame_comparisons,
              "undo_redo": undo_comparisons,
              "mismatches": mismatches}
    if cross_load:
        from scene_cross_load import cross_load_scene
        report["cross_load"] = cross_load_scene(locals())
        mismatches.extend(report["cross_load"]["mismatches"])
    report_name = 'comparison-x86-port.json' if native_resave_directory else 'comparison.json'
    (directory / report_name).write_text(json.dumps(report, indent=2))
    print(f"original bytes={len(original)}, writes={len(chunks)}, mismatches={len(mismatches)}")
    return not mismatches


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("--cross-load", action="store_true")
    parser.add_argument("--port-test", type=Path,
                        help="run the native fixture and both complete cross-load chains")
    parser.add_argument("--port-test-x86", type=Path,
                        help="also cross-load and compare the x86 port (requires --port-test)")
    args = parser.parse_args()
    original, reference = args.original.resolve(), args.reference.resolve()
    if args.port_test_x86 and not args.port_test:
        parser.error('--port-test-x86 requires the x64 --port-test fixture')
    if args.port_test:
        executable = args.port_test.resolve()
        subprocess.run([str(executable), '--reference', str(reference)], check=True)
        directories = [reference, reference / 'complex']
        if not all(compare(original, directory, True) for directory in directories):
            raise SystemExit(1)
        subprocess.run([str(executable), '--cross-check', str(reference)], check=True)
        if not all(compare(original, directory, True, True) for directory in directories):
            raise SystemExit(1)
        if args.port_test_x86:
            x86_reference = reference / 'x86'
            for directory in directories:
                target = x86_reference / directory.relative_to(reference)
                target.mkdir(parents=True, exist_ok=True)
                for name in ('original.pmm', 'original-reloaded.pmm'):
                    shutil.copyfile(directory / name, target / name)
            subprocess.run([str(args.port_test_x86.resolve()), '--cross-check', str(x86_reference)], check=True)
            if json.loads((x86_reference / 'input.json').read_text())['pointer_width'] != 4:
                raise ValueError('--port-test-x86 must name an x86 fixture executable')
            if not all(compare(original, directory, True, True,
                               x86_reference / directory.relative_to(reference))
                       for directory in directories):
                raise SystemExit(1)
        raise SystemExit(0)
    raise SystemExit(0 if compare(original, reference, args.cross_load) else 1)

