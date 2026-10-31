"""Audit the actual P1 link graph, project include closure and binary dependencies."""
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


def audit(program, manifest):
    graph = json.loads(manifest.read_text(encoding='utf-8'))
    expected = {'mds_model_cli', 'mikudancestudio_model_runtime', 'mikudancestudio_cpu_math',
                'mikudancestudio_formats'}
    if set(graph) != expected:
        raise RuntimeError('P1 target manifest changed')
    visited = set()
    pending = ['mds_model_cli']
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
        sources += graph[name]['sources']
    scanned = set()
    pending = [ROOT / source for source in sources]
    while pending:
        path = pending.pop().resolve()
        if path in scanned:
            continue
        if not path.is_relative_to(ROOT):
            raise RuntimeError('project include escaped source root')
        scanned.add(path)
        source = path.read_text(encoding='utf-8')
        for delimiter, name in re.findall(r'^\s*#\s*include\s*([<"])([^>"\n]+)[>"]', source, re.M):
            if name.lower() in FORBIDDEN_HEADERS:
                raise RuntimeError(f'forbidden project dependency: {path.name} -> {name}')
            candidates = [path.parent / name, ROOT / 'include' / name]
            local = next((p for p in candidates if p.is_file()), None)
            if local:
                pending.append(local)
            elif delimiter == '"':
                raise RuntimeError(f'unresolved project include: {name}')
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
            'project_files': sorted(str(p.relative_to(ROOT)).replace('\\', '/') for p in scanned),
            'binary_dependencies': libraries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', required=True, type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = audit(args.program.resolve(), args.manifest)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('P1 passed: link graph, project headers and binary dependencies contain no UI, GPU or native model dependency')


if __name__ == '__main__':
    main()
