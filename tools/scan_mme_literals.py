# 扫描 MME 模块源码中的字符串字面量原始高位字节（编码依赖点）。
import glob
import re

PATTERN = re.compile(r'"((?:[^"\\]|\\.)*)"')

for p in sorted(glob.glob('src/**/*.cpp', recursive=True) +
                glob.glob('src/**/*.h', recursive=True)):
    raw = open(p, 'rb').read()
    try:
        text = raw.decode('utf-8')
        is_utf8 = True
    except UnicodeDecodeError:
        is_utf8 = False
        text = raw.decode('gbk', errors='replace')
    hits = []
    for m in PATTERN.finditer(text):
        s = m.group(1)
        if any(ord(c) > 0x7F for c in s):
            line = text.count('\n', 0, m.start()) + 1
            hits.append((line, s[:40]))
    if hits or not is_utf8:
        print(p, 'utf8' if is_utf8 else 'NOT-UTF8(raw)', 'literals:', hits[:5])
