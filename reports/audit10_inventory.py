"""Reproducible, read-only inventory for audit10 (run from any directory)."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[1]
SUFFIXES = {'.cpp', '.c', '.h', '.hpp', '.inc', '.py', '.ps1', '.rc', '.def', '.fx', '.xml'}
ROOTS = ['src', 'include', 'third_party/mmeffect', 'scripts', 'tools', 'recipes', 'res']
PATTERNS = {
    'reinterpret_cast': r'\breinterpret_cast\s*<',
    'raw_padding': r'\bRawPad\s*<',
    'offset_accessor': r'\bAt\s*<',
    'manual_vtable': r'\bVt\s*\(|\bvtable\s*\[',
    'review_marker': r'\b(?:TODO|FIXME|UNCERTAIN|approximation|stub|divergence)\b',
}
cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
paths = sorted({p for root in ROOTS for p in (ROOT / root).rglob('*')
                if p.is_file() and p.suffix.lower() in SUFFIXES
                and 'bullet-src' not in p.parts and '__pycache__' not in p.parts}
               | {ROOT / 'CMakeLists.txt', ROOT / 'conanfile.py', ROOT / 'exports/MikuMikuDance.def'})
rows = []
for p in paths:
    data = p.read_bytes()
    content = data.decode('utf-8', errors='replace')
    rel = p.relative_to(ROOT).as_posix()
    # Lexical indicators only: comments and string literals are deliberately
    # retained. These counts are triage aids, not counts of defects.
    rows.append(dict(path=rel, lines=len(content.splitlines()),
                     sha256=hashlib.sha256(data).hexdigest(),
                     explicitly_in_cmake=(rel in cmake) if p.suffix == '.cpp' else None,
                     indicators={k: len(re.findall(v, content, re.I))
                                 for k, v in PATTERNS.items()}))
out = ROOT / 'reports/audit10_inventory.json'
out.write_text(json.dumps({'method': 'lexical inventory, not semantic proof',
                          'files': rows}, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
print(json.dumps({'files': len(rows), 'lines': sum(r['lines'] for r in rows),
                  'cpp_not_explicitly_in_cmake': [r['path'] for r in rows
                       if r['explicitly_in_cmake'] is False],
                  'indicators': {k: sum(r['indicators'][k] for r in rows)
                                  for k in PATTERNS}}, indent=2))
