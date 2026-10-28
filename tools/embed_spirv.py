"""Embed build-generated SPIR-V words; the executable needs no shader compiler."""
import argparse
from pathlib import Path
import struct

parser = argparse.ArgumentParser()
parser.add_argument('--output', required=True)
parser.add_argument('modules', nargs='+', help='symbol=path.spv')
args = parser.parse_args()
lines = ['#pragma once', '#include <cstdint>', 'namespace mikudancestudio::rhi::spirv {']
for module in args.modules:
    name, path = module.split('=', 1)
    data = Path(path).read_bytes()
    if not name.isidentifier() or len(data) % 4 or data[:4] != b'\x03\x02\x23\x07':
        raise ValueError(f'invalid SPIR-V module: {name}')
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    lines.append(f'inline constexpr std::uint32_t {name}[] = {{')
    for start in range(0, len(words), 8):
        lines.append('    ' + ','.join(f'0x{word:08x}' for word in words[start:start + 8]) + ',')
    lines.append('};')
lines.append('}')
Path(args.output).write_text('\n'.join(lines) + '\n', encoding='utf-8')
