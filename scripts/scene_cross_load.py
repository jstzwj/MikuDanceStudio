"""Runtime bridges for executing the original PMM loader in the local oracle.

PMM and PMD parsing execute original instructions. UI, file descriptors and D3D
resources are isolated test services. The X mesh resource loader is bridged;
accessory animation and attachment fields still come from the PMM reader.
"""

import ctypes
import json
import math
from pathlib import Path
import re
import struct

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8,
    UC_X86_REG_R9, UC_X86_REG_RIP, UC_X86_REG_RSP, UC_X86_REG_XMM0,
    UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3,
)


def cross_load_scene(context):
    uc, base, stub = (context[name] for name in ('uc', 'base', 'stub'))
    metadata, directory = context['metadata'], context['directory']
    put, integer, read_int, string = (context[name] for name in ('put', 'integer', 'read_int', 'string'))
    heap_start, heap_size = 0x20000000, 0x18000000
    uc.mem_map(heap_start, heap_size)
    context['pages'].update(range(heap_start, heap_start + heap_size, 4096))
    cursor = heap_start
    counts, descriptors, buffers = {}, {}, {}
    enabled, refresh_calls = {}, []
    model_opens = []
    next_fd = 10
    controls = {}
    checks = {}
    input_bytes = (directory / 'port.pmm').read_bytes()
    descriptors[3] = [input_bytes, 30]
    accessory = read_int(metadata['accessory_slots_address'] + 3 * 8)
    accessory_template = bytes(uc.mem_read(accessory, 0x4C0))

    def allocate(size):
        nonlocal cursor
        address = cursor
        cursor += (size + 31) & ~15
        if cursor > heap_start + heap_size:
            raise ValueError('original loader exceeded its fixture allocation budget')
        return address

    def wide(address, value):
        put(address, value.encode('utf-16-le') + b'\0\0')

    def finish(value=0):
        uc.reg_write(UC_X86_REG_RAX, value & 0xFFFFFFFFFFFFFFFF)
        uc.reg_write(UC_X86_REG_RIP, stub + 0xFFF0)
        return True

    def arguments():
        return [uc.reg_read(reg) for reg in
                (UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_R9)]

    d3dx_signatures = {
        'D3DXMatrixRotationQuaternion': ('o16', 'p4'),
        'D3DXMatrixMultiply': ('o16', 'p16', 'p16'),
        'D3DXMatrixInverse': ('o16', 'o1', 'p16'),
        'D3DXMatrixTranspose': ('o16', 'p16'),
        'D3DXMatrixTranslation': ('o16', 'f', 'f', 'f'),
        'D3DXMatrixRotationX': ('o16', 'f'),
        'D3DXMatrixRotationY': ('o16', 'f'),
        'D3DXMatrixRotationZ': ('o16', 'f'),
        'D3DXMatrixRotationYawPitchRoll': ('o16', 'f', 'f', 'f'),
        'D3DXQuaternionRotationMatrix': ('o4', 'p16'),
        'D3DXQuaternionNormalize': ('o4', 'p4'),
        'D3DXQuaternionMultiply': ('o4', 'p4', 'p4'),
        'D3DXQuaternionRotationAxis': ('o4', 'p3', 'f'),
        'D3DXQuaternionRotationYawPitchRoll': ('o4', 'f', 'f', 'f'),
        'D3DXQuaternionSlerp': ('o4', 'p4', 'p4', 'f'),
        'D3DXQuaternionToAxisAngle': ('p4', 'o3', 'o1'),
        'D3DXVec3TransformCoord': ('o3', 'p3', 'p16'),
        'D3DXVec3TransformNormal': ('o3', 'p3', 'p16'),
        'D3DXVec3Normalize': ('o3', 'p3'),
        'D3DXVec3Cross': ('o3', 'p3', 'p3'),
    }

    def d3dx_call(name):
        args, native, arrays, outputs = arguments(), [], [], []
        kinds = []
        xmm = (UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3)
        for index, kind in enumerate(d3dx_signatures[name]):
            address = args[index] if index < 4 else read_int(uc.reg_read(UC_X86_REG_RSP) + 8 + index * 8)
            if kind == 'f':
                bits = uc.reg_read(xmm[index]) if index < 4 else address
                native.append(struct.unpack('<f', struct.pack('<I', bits & 0xFFFFFFFF))[0])
                kinds.append(ctypes.c_float)
            else:
                count = int(kind[1:])
                value = (ctypes.c_float * count)()
                if address:
                    value = (ctypes.c_float * count).from_buffer_copy(uc.mem_read(address, count * 4))
                arrays.append(value)
                native.append(ctypes.cast(value, ctypes.c_void_p) if address else None)
                kinds.append(ctypes.c_void_p)
                if kind[0] == 'o' and address:
                    outputs.append((address, value))
        fn = getattr(context['d3dx'], name)
        fn.argtypes, fn.restype = kinds, ctypes.c_void_p
        fn(*native)
        for address, value in outputs:
            put(address, bytes(value))
        return finish(args[0])

    device_table = allocate(128 * 8)
    integer(context['device'], device_table)
    for index in range(128):
        integer(device_table + index * 8, stub + 0xD000 + index * 16)
    buffer_table = allocate(16 * 8)
    for index in range(16):
        integer(buffer_table + index * 8, stub + 0xC000 + index * 16)
    physics = allocate(0x90)
    world, ground = allocate(256), allocate(256)
    integer(physics + 128, world)
    integer(physics + 136, ground)
    integer(world, buffer_table)
    integer(metadata['physics_address'], physics)

    # Destruction is outside the serialized-state oracle. Allocations are owned
    # by the emulator; the PMM loader still clears/replaces every scene slot.
    skipped = {
        0xA8D50: 'ModelDispose', 0xC4E0: 'DisposeAccessory',
        0x21E10: 'RefreshFrameControls', 0xF200: 'ClearTimelineAndCurveDCs',
        0x1D250: 'RebuildTimelineTrackPanel',
        0x216A0: 'RefreshMainWindowViewport', 0x1FA90: 'LayoutViewportPanels',
        0x5F7F0: 'TimelinePainting',
        0x5CAF0: 'RefreshSelfShadowPanel', 0x60EA0: 'PanelPaint',
        0x20DD0: 'PostViewRefresh',
        0x5BA90: 'UpdatePanelValues', 0xD2240: 'SetModelColor',
        0x62BB0: 'SelectionReeval',
    }

    def handle(address):
        nonlocal next_fd
        a, b, c, d = arguments()
        rva = address - base
        if rva in (0x11A712, 0x127758):
            name = 'cosf' if rva == 0x11A712 else 'sqrtf'
            fn = getattr(context['crt'], name)
            fn.argtypes, fn.restype = [ctypes.c_float], ctypes.c_float
            value = struct.unpack('<f', struct.pack('<I', uc.reg_read(UC_X86_REG_XMM0) & 0xFFFFFFFF))[0]
            bits = struct.unpack('<I', struct.pack('<f', fn(value)))[0]
            uc.reg_write(UC_X86_REG_XMM0, bits)
            return finish()
        if rva == 0x127830:
            # __chkstk probes the Windows TEB; the emulator stack is pre-mapped.
            return finish(uc.reg_read(UC_X86_REG_RAX))
        if rva in skipped:
            counts[skipped[rva]] = counts.get(skipped[rva], 0) + 1
            refresh_calls.append(skipped[rva])
            return finish()
        if rva == 0xDEA6C:
            return finish(allocate(a))
        if 0xD000 <= address - stub < 0xD800:
            index = (address - stub - 0xD000) // 16
            if index in (26, 27):
                resource = allocate(32)
                integer(resource, buffer_table)
                buffers[resource] = allocate(b)
                integer(read_int(uc.reg_read(UC_X86_REG_RSP) + 48), resource)
                return finish()
            if index == 44:
                return finish()
            raise ValueError(f'unexpected D3D device method {index}')
        if 0xC000 <= address - stub < 0xC100:
            index = (address - stub - 0xC000) // 16
            if a == world and index == 13:
                return finish()
            if index == 11:
                integer(d, buffers[a] + b)
            elif index not in (2, 12):
                raise ValueError(f'unexpected buffer/world method {index}')
            return finish()
        if rva == 0x95C0:
            # Resolve the generated absolute CP932 path without search dialogs.
            wide(c, string(b))
            return finish(c)
        if rva == 0x9C00:
            path = string(b, True)
            wide(a + 3024, path)
            return finish(a + 3024)
        if rva == 0xDC560:
            # X mesh creation is a resource boundary; PMM fills its pose below.
            put(a, accessory_template)
            wide(a + 684, string(d, True))
            return finish(1)
        if address not in context['bridges']:
            return False
        name = context['bridges'][address]
        counts[name] = counts.get(name, 0) + 1
        if name == 'EnableWindow':
            enabled[a] = b != 0
            return finish(1)
        if name in ('asinf', 'atan2f', 'cosf', 'sqrtf', 'fmodf'):
            count = 2 if name in ('atan2f', 'fmodf') else 1
            fn = getattr(context['crt'], name)
            fn.argtypes, fn.restype = [ctypes.c_float] * count, ctypes.c_float
            values = [struct.unpack('<f', struct.pack('<I', uc.reg_read(reg) & 0xFFFFFFFF))[0]
                      for reg in (UC_X86_REG_XMM0, UC_X86_REG_XMM1)[:count]]
            uc.reg_write(UC_X86_REG_XMM0, struct.unpack('<I', struct.pack('<f', fn(*values)))[0])
            return finish()
        if name == '_read':
            data, position = descriptors[a]
            payload = data[position:position + c]
            put(b, payload)
            descriptors[a][1] += len(payload)
            return finish(len(payload))
        if name == '_wsopen_s':
            path = Path(string(b, True))
            if c & 1:
                integer(a, next_fd, 4)
                next_fd += 1
                return finish()
            if not path.is_file():
                raise ValueError(f'original requested a missing asset: {path}')
            descriptors[next_fd] = [path.read_bytes(), 0]
            model_opens.append(str(path))
            integer(a, next_fd, 4)
            next_fd += 1
            return finish()
        if name.startswith('??2'):
            return finish(allocate(a))
        if name.startswith('??3') or name.startswith('??_V'):
            return finish()
        if name in ('wcscpy_s', 'wcsncpy_s'):
            wide(a, string(c, True)[:b - 1])
            return finish()
        if name in ('MessageBoxA', 'DialogBoxParamA'):
            raise ValueError(f'original loader opened a dialog at file offset {descriptors[3][1]}')
        if name == 'SetWindowTextA':
            controls.setdefault(a, {})['text'] = string(b)
            return finish(1)
        if name in ('GetMenu', 'GetSubMenu', 'SetMenuItemInfoA', 'SetCurrentDirectoryW',
                    'EnableMenuItem', 'DrawMenuBar', 'MoveWindow', 'ShowWindow', 'CheckRadioButton',
                    'SetFocus'):
            return finish(1)
        if name == 'CheckMenuItem':
            checks[b] = c
            return finish()
        if name == 'GetMenuState':
            return finish(checks.get(b, 0))
        if name in ('SendMessageA', 'SendMessageW'):
            control = controls.setdefault(a, {'text': '', 'rows': [], 'selection': 0})
            if b == 0x14B:  # CB_RESETCONTENT
                control['rows'] = []
            elif b == 0x143:  # CB_ADDSTRING
                control['rows'].append(string(d, name.endswith('W')))
                return finish(len(control['rows']) - 1)
            elif b == 0x146:
                return finish(len(control['rows']))
            elif b == 0x148:
                rows = control['rows']
                if c >= len(rows):
                    raise ValueError('invalid combo row requested by original loader')
                put(d, rows[c].encode('cp932') + b'\0')
                return finish(len(rows[c]))
            elif b == 0x14E:
                control['selection'] = c
            elif b == 0x147:
                return finish(control['selection'])
            elif b == 0xC2:
                control['text'] = string(d)
            return finish()
        if name == 'GetWindowTextA':
            value = controls.setdefault(a, {}).get('text', '0')
            put(b, value.encode('cp932')[:c - 1] + b'\0')
            return finish(len(value))
        if name == 'GetWindowTextLengthA':
            return finish(len(controls.setdefault(a, {}).get('text', '0')))
        if name == 'sprintf_s':
            fmt = string(c)
            if '%' not in fmt:
                value = fmt
            elif fmt in ('%d', '%3d', 'toon%02d.bmp'):
                signed = ctypes.c_int(d).value
                value = fmt % signed
            elif re.fullmatch(r'%[+ -]?\d*(?:\.\d+)?f', fmt):
                value = fmt % struct.unpack('<d', struct.pack('<Q', d))[0]
            elif fmt.count('%s') == 1 and fmt.count('%') == 1:
                value = fmt % string(d)
            else:
                raise ValueError(f'unsupported original loader format {fmt!r}')
            put(a, value.encode('cp932') + b'\0')
            return finish(len(value))
        if name in d3dx_signatures:
            return d3dx_call(name)
        return False

    context['extension']['handler'] = handle
    for rva in [*skipped, 0x95C0, 0x9C00, 0xDC560, 0x127830, 0x11A712, 0x127758]:
        uc.hook_add(UC_HOOK_CODE, context['hook'], begin=base + rva, end=base + rva)
    try:
        context['invoke'](0x78E30, metadata['app'], 3)
    except Exception:
        print(f'loader RIP={uc.reg_read(UC_X86_REG_RIP):#x}, file={descriptors[3][1]}, services={counts}')
        sp = uc.reg_read(UC_X86_REG_RSP)
        print('return addresses:', [hex(v - base) for v in struct.unpack('<128Q', uc.mem_read(sp, 1024))
                                    if base <= v < base + 0x130000])
        raise
    if descriptors[3][1] != len(input_bytes):
        raise ValueError('original PMM loader did not consume the complete fixture')
    mismatches, frames = [], []
    model = read_int(metadata['model_slots_address'])
    bone_table, morph_table = read_int(model + 10056), read_int(model + 10072)
    accessory = read_int(metadata['accessory_slots_address'] + 3 * 8)
    for item in metadata['fixed_frames']:
        context['invoke'](0xCBD90, model, item['frame'], 2)
        integer(metadata['frame_address'], item['frame'], 4)
        context['invoke'](0x59C30, metadata['app'])
        context['invoke'](0x5E150, metadata['app'], 3)
        pose = b''.join(bytes(uc.mem_read(bone_table + i * metadata['bone_stride'] +
                                        metadata['pose_offset'], 28))
                        for i in range(metadata['bone_count']))
        pose += b''.join(bytes(uc.mem_read(morph_table + i * metadata['morph_stride'] +
                                         metadata['morph_value_offset'], 4))
                         for i in range(metadata['morph_count']))
        camera = b''.join(bytes(uc.mem_read(address, width)) for address, width in
                          zip(metadata['camera_fields'], (12, 12, 4, 4, 4, 4, 1)))
        acc = b''.join(bytes(uc.mem_read(accessory + offset, width)) for offset, width in
                       ((548, 12), (560, 12), (572, 4), (1200, 4), (576, 4), (580, 4),
                        (544, 1), (1196, 1)))
        matches = (pose.hex() == item['pose'] and camera.hex() == item['camera'] and
                   acc.hex() == item['accessory'])
        frames.append({'frame': item['frame'], 'bit_exact': matches})
        if not matches:
            mismatches.append({'loaded_frame': item['frame'], 'pose': pose.hex(),
                               'camera': camera.hex(), 'accessory': acc.hex()})
        if metadata.get('complex'):
            context['invoke'](0xBD780, model)
            context['invoke'](0xB9F70, model, 0, metadata['model_slots_address'], 0)
            context['invoke'](0xB9F70, model, 1, metadata['model_slots_address'], 0)
            matrices = b''.join(bytes(uc.mem_read(bone_table + i * 624 + offset, 64))
                                for i in range(metadata['bone_count']) for offset in (60, 188))
            expected_matrices = bytes.fromhex(item['matrices'])
            error = context['float_error'](matrices, expected_matrices, len(matrices) // 4)
            frames[-1].update(matrix_bit_exact=matrices == expected_matrices, matrix_max_error=error)
            if error > 0.00002:
                mismatches.append({'matrix_frame': item['frame'], 'max_error': error,
                                   'original': matrices.hex(), 'port': item['matrices']})
    # Save the same current frame/poses as the native reloaded fixture.
    integer(metadata['frame_address'], 24, 4)
    context['invoke'](0xCBD90, model, 24, 2)
    context['invoke'](0x59C30, metadata['app'])
    context['invoke'](0x5E150, metadata['app'], 3)
    context['chunks'].clear()
    context['invoke'](0x750A0, metadata['app'])
    chunks = context['chunks']
    original = b''.join(payload for _, payload in chunks)
    (directory / 'original-reloaded.pmm').write_bytes(original)
    expected = (directory / 'assets/reloaded.pmm').read_bytes()
    offset = 0
    for caller, payload in chunks:
        width = len(payload)
        meaningful = payload.index(0) + 1 if caller in context['string_writes'] and 0 in payload else width
        actual = expected[offset:offset + width]
        if actual[:meaningful] != payload[:meaningful]:
            mismatches.append({'offset': offset, 'caller_rva': hex(caller),
                               'original': payload.hex(), 'port': actual.hex()})
        offset += width
    if len(original) != len(expected):
        mismatches.append({'original_size': len(original), 'port_size': len(expected)})
    port_resaves = []
    names = ('original-port-resaved.pmm', 'original-reloaded-port-resaved.pmm') if context['verify_resaves'] else ()
    native_directory = context['native_resave_directory'] or directory
    for name in names:
        path = native_directory / 'assets' / name
        if not path.is_file():
            raise ValueError(f'missing native cross-load result: {path}')
        data, offset, differences = path.read_bytes(), 0, []
        for caller, payload in chunks:
            width = len(payload)
            meaningful = payload.index(0) + 1 if caller in context['string_writes'] and 0 in payload else width
            if data[offset:offset + meaningful] != payload[:meaningful]:
                differences.append({'offset': offset, 'caller_rva': hex(caller)})
            offset += width
        if len(data) != offset:
            differences.append({'port_size': len(data), 'original_size': offset})
        port_resaves.append({'file': name, 'matches': not differences})
        if differences:
            mismatches.append({'port_resave': name, 'differences': differences})
    # Replay the same production button-command sequence against a freshly
    # parsed original model. Only painting/curve presentation are sinks.
    ui_results = []
    sequence_file = directory / 'ui-commands.json'
    if sequence_file.is_file():
        sequence = json.loads(sequence_file.read_text())
        selected_bone = sequence[0]['bone']
        bone_count = metadata['bone_count']
        key_table = read_int(model + 10128)
        selection = read_int(model + 12576)
        # selectedBone and boneSelection are pinned by the x64 layout assertions.
        integer(model + 12572, selected_bone, 4)
        put(selection, bytes(int(i == selected_bone) for i in range(bone_count)))
        integer(base + 0x1445F8, metadata['app'])
        integer(metadata['frame_address'], 36, 4)
        enabled[400] = enabled[401] = True
        put(bone_table + selected_bone * 624 + 328, struct.pack('<f', 4.5))
        if metadata.get('complex'):
            squared_w = struct.unpack('<f', struct.pack('<f', .86))[0]
            put(bone_table + selected_bone * 624 + 340,
                struct.pack('<4f', .2, .3, .1, math.sqrt(squared_w)))
        for index, snapshot in enumerate(sequence):
            command = snapshot['id']
            if index == 1:
                put(bone_table + selected_bone * 624 + 328, struct.pack('<f', 6.25))
            if command == 423:
                for i in range(bone_count * 4):
                    put(key_table + i * 60 + 56, b'\0')
                # Only the added frame is marked, exactly as in the native UI fixture.
                previous = read_int(key_table + selected_bone * 60 + 8, 4)
                added = read_int(key_table + previous * 60 + 8, 4)
                put(key_table + added * 60 + 56, b'\1')
                for i in range(metadata['morph_count'] * 4):
                    put(read_int(model + 10136) + i * 20 + 16, b'\0')
                put(read_int(model + 10144) + 24, b'\0')
            refresh_calls.clear()
            context['invoke'](0x3F550, command, command)
            actual = {
                'id': command, 'bone': read_int(model + 12572, 4),
                'frame': read_int(metadata['frame_address'], 4),
                'cursor': read_int(model + 13648, 4), 'latest': read_int(model + 13652, 4),
                'undo_enabled': int(enabled.get(400, True)),
                'redo_enabled': int(enabled.get(401, True)),
                'physics_reset': int(read_int(metadata['physics_reset_address'], 1) != 0),
                'selection': list(uc.mem_read(selection, bone_count)),
                'keys': bytes(uc.mem_read(key_table, bone_count * 4 * 60)).hex(),
            }
            matches = actual == snapshot
            refresh_matches = refresh_calls == ['PanelPaint', 'SelectionReeval']
            ui_results.append({'id': command, 'matches': matches, 'refresh_calls': list(refresh_calls)})
            if not matches or not refresh_matches:
                mismatches.append({'ui_step': index, 'actual': actual, 'expected': snapshot})
    print(f'original PMM reload: read {descriptors[3][1]}, wrote {len(original)}, differences {len(mismatches)}')
    return {'bytes_read': descriptors[3][1], 'asset_opens': model_opens, 'fixed_frames': frames,
            'ui_commands': ui_results, 'port_resaves': port_resaves,
            'native_resave_directory': str(native_directory),
            'services': counts, 'mismatches': mismatches}
