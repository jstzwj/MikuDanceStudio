"""Audit the actual CPU runtime link graph, include closure and binary dependencies."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN_HEADERS = {'windows.h', 'd3d9.h', 'd3dx9.h', 'mikudancestudio/model.hpp',
                     'mikudancestudio/mmd_app.hpp', 'mikudancestudio/ported_funcs.hpp',
                     'mikudancestudio/d3dx_dyn.hpp'}
FORBIDDEN_LIBRARIES = ('d3d', 'd3dx', 'dxgi', 'vulkan', 'moltenvk', 'opengl', 'mmeffect',
                       'user32', 'gdi32', 'ole32', 'oleaut32', 'combase', 'qt', 'sdl')
SYSTEM_C_HEADERS = {'assert.h', 'ctype.h', 'float.h', 'limits.h', 'math.h', 'memory.h',
                    'stdarg.h', 'stddef.h', 'stdint.h', 'stdio.h', 'stdlib.h', 'string.h', 'time.h'}


def pe_imports(program):
    data = program.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise RuntimeError('invalid PE signature')
    count = struct.unpack_from('<H', data, pe + 6)[0]
    size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    directory = optional + {0x10b: 96, 0x20b: 112}[magic]
    if struct.unpack_from('<I', data, directory + 13 * 8)[0]:
        raise RuntimeError('P1 executable unexpectedly contains delayed imports')
    sections = []
    for n in range(count):
        virtual_size, rva, raw_size, offset = struct.unpack_from('<IIII', data, optional + size + 40 * n + 8)
        sections.append((rva, max(virtual_size, raw_size), offset))

    def position(rva):
        for begin, length, offset in sections:
            if begin <= rva < begin + length:
                return offset + rva - begin
        raise RuntimeError('import RVA outside PE sections')

    imports = struct.unpack_from('<I', data, directory + 8)[0]
    result = []
    if imports:
        offset = position(imports)
        while any(data[offset:offset + 20]):
            name = position(struct.unpack_from('<I', data, offset + 12)[0])
            result.append(data[name:data.index(0, name)].decode('ascii'))
            offset += 20
    return result


def audit(program, manifest, stage='P1'):
    graph = json.loads(manifest.read_text(encoding='utf-8'))
    expected = {'mds_model_cli', 'mikudancestudio_model_runtime', 'mikudancestudio_cpu_math',
                'mikudancestudio_formats'}
    root = 'mds_model_cli'
    if stage == 'P2':
        root = 'mds_cpu_scene_cli'
        expected.remove('mds_model_cli')
        expected.update({'mds_cpu_scene_cli', 'mikudancestudio_scene_runtime', 'mds_cpu_simulation',
                         'BULLET275::BULLET275', 'BulletDynamics', 'BulletCollision', 'LinearMath'})
        if 'mikudancestudio_math' in graph:
            expected.add('mikudancestudio_math')
    if set(graph) != expected:
        raise RuntimeError(f'{stage} target manifest changed: {set(graph) ^ expected}')
    visited = set()
    pending = [root]
    sources = []
    while pending:
        name = pending.pop()
        if name in visited:
            continue
        visited.add(name)
        for dependency in graph[name]['links']:
            if not dependency:
                continue
            if dependency not in expected:
                raise RuntimeError(f'non-portable runtime link: {name} -> {dependency}')
            pending.append(dependency)
        sources += [source for source in graph[name]['sources'] if source]
    scanned = set()
    vendor_platform_headers = []
    vendor_include_aliases = []
    pending = [ROOT / source for source in sources]
    while pending:
        path = pending.pop().resolve()
        if path in scanned:
            continue
        if not path.is_relative_to(ROOT):
            raise RuntimeError('project include escaped source root')
        scanned.add(path)
        source = path.read_text(encoding='utf-8', errors='surrogateescape')
        for delimiter, name in re.findall(r'^\s*#\s*include\s*([<"])([^>"\n]+)[>"]', source, re.M):
            if name.lower() in FORBIDDEN_HEADERS:
                if (stage == 'P2' and name.lower() == 'windows.h' and
                        path.relative_to(ROOT).as_posix().lower() ==
                        'recipes/bullet275/bullet-src/src/linearmath/btquickprof.h'):
                    # Pinned upstream profiling clock, behind WIN32/_WIN32.
                    # No project runtime code or public semantic API uses it.
                    vendor_platform_headers.append({'source': str(path.relative_to(ROOT)).replace('\\', '/'),
                                                    'header': name, 'purpose': 'conditional upstream profiling timer'})
                    continue
                raise RuntimeError(f'forbidden project dependency: {path.name} -> {name}')
            candidates = [path.parent / name, ROOT / 'include' / name,
                          ROOT / 'recipes/bullet275/bullet-src/src' / name]
            if (stage == 'P2' and
                    path.relative_to(ROOT).as_posix().lower() ==
                    'recipes/bullet275/bullet-src/src/bulletcollision/broadphasecollision/btdbvt.cpp' and
                    name == 'LinearMath/btQuickProf.h'):
                # Pinned upstream has this spelling in the disabled benchmark
                # branch. Follow the real file conservatively on case-sensitive
                # hosts, without treating unresolved project headers as safe.
                candidates = [ROOT / 'recipes/bullet275/bullet-src/src/LinearMath/btQuickprof.h']
                vendor_include_aliases.append({'source': str(path.relative_to(ROOT)).replace('\\', '/'),
                                               'include': name, 'actual': 'LinearMath/btQuickprof.h',
                                               'purpose': 'upstream DBVT_ENABLE_BENCHMARK include spelling'})
            local = next((p for p in candidates if p.is_file()), None)
            if local:
                pending.append(local)
            elif delimiter == '"' and name not in SYSTEM_C_HEADERS:
                raise RuntimeError(f'unresolved project include: {path.name} -> {name}')
    if sys.platform == 'win32':
        libraries = pe_imports(program)
    else:
        command = ['otool', '-L', str(program)] if sys.platform == 'darwin' else ['ldd', str(program)]
        lines = subprocess.check_output(command, text=True).splitlines()
        libraries = lines[1:] if sys.platform == 'darwin' else lines
    for library in libraries:
        if any(word in library.lower() for word in FORBIDDEN_LIBRARIES):
            raise RuntimeError(f'forbidden binary dependency: {library}')
    return {'schema': 1, 'status': 'passed', 'targets': sorted(visited),
            'vendor_platform_headers': vendor_platform_headers,
            'vendor_include_aliases': vendor_include_aliases,
            'project_files': sorted(str(p.relative_to(ROOT)).replace('\\', '/') for p in scanned),
            'binary_dependencies': libraries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', required=True, type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--stage', choices=['P1', 'P2'], default='P1')
    args = parser.parse_args()
    result = audit(args.program.resolve(), args.manifest, args.stage)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f'{args.stage} passed: link graph, project headers and binary dependencies contain no UI, GPU or native model dependency')


if __name__ == '__main__':
    main()
