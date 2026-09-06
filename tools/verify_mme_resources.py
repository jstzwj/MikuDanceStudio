# 验证 MMEffect.dll 资源段里的字符串是否为正确 UTF-16（RC 转码后）。
# 用法: python tools/verify_mme_resources.py <dll路径>
import struct
import sys

path = sys.argv[1]
data = open(path, 'rb').read()
pe = struct.unpack_from('<I', data, 0x3C)[0]
opt = pe + 0x18
nsec = struct.unpack_from('<H', data, pe + 6)[0]
secoff = opt + struct.unpack_from('<H', data, pe + 20)[0]

secs = []
for i in range(nsec):
    name = data[secoff + 40 * i: secoff + 40 * i + 8].rstrip(b'\x00').decode()
    vs, va, size_raw, raw = struct.unpack_from('<IIII', data, secoff + 40 * i + 8)
    secs.append((name, va, vs, raw))

def rva2off(rva):
    for name, va, vs, raw in secs:
        if va <= rva < va + vs:
            return raw + (rva - va)
    return None

rsrc = next(s for s in secs if s[0] == '.rsrc')
base = rsrc[3]

def walk(dir_off, path_ids=()):
    _, _, _, _, nnamed, nid = struct.unpack_from('<IIHHHH', data, dir_off)
    out = []
    for i in range(nnamed + nid):
        eo = dir_off + 16 + 8 * i
        namefield, offset = struct.unpack_from('<II', data, eo)
        if namefield & 0x80000000:
            no = base + (namefield & 0x7FFFFFFF)
            slen = struct.unpack_from('<H', data, no)[0]
            ident = data[no + 2: no + 2 + 2 * slen].decode('utf-16-le')
        else:
            ident = namefield
        if offset & 0x80000000:
            out += walk(base + (offset & 0x7FFFFFFF), path_ids + (ident,))
        else:
            de = base + offset
            rva, size = struct.unpack_from('<II', data, de)
            out.append((path_ids + (ident,), rva2off(rva), size))
    return out

found = 0
for ids, off, size in walk(base):
    blob = data[off: off + size]
    for t in ['确定', '取消', 'エフェクトファイル割り当て', '使用特效', '更新']:
        if t.encode('utf-16-le') in blob:
            kind = {1: 'CURSOR', 2: 'BITMAP', 3: 'ICON', 4: 'MENU',
                    5: 'DIALOG', 6: 'STRING', 9: 'ACCEL', 14: 'GROUP_ICON',
                    16: 'VERSION', 24: 'MANIFEST'}.get(ids[0], ids[0])
            print(f'  {kind} id={ids[1]}: UTF-16 "{t}" OK')
            found += 1
print('total hits:', found)
