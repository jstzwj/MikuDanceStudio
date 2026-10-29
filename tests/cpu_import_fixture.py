"""Original generated meshes/scenes; no third-party model or texture bytes."""
import struct
from pathlib import Path


class Bytes(bytearray):
    def u8(self, n): self.extend(struct.pack('<B', n))
    def u16(self, n): self.extend(struct.pack('<H', n))
    def u32(self, n): self.extend(struct.pack('<I', n))
    def f(self, *v): self.extend(struct.pack('<' + 'f' * len(v), *v))
    def zero(self, n): self.extend(bytes(n))
    def fixed(self, text, size):
        encoded = text.encode('ascii')
        assert len(encoded) < size
        self.extend(encoded); self.zero(size - len(encoded))
    def text(self, text):
        encoded = text.encode('utf-16-le')
        self.u32(len(encoded)); self.extend(encoded)


POSITIONS = ((-10, -8, 0), (0, 10, 0), (10, -8, 0))


def pmd(texture='checker.bmp', alpha=1):
    b = Bytes(b'Pmd'); b.f(1); b.fixed('CPU triangle', 20); b.zero(256)
    b.u32(3)
    for index, p in enumerate(POSITIONS):
        b.f(*p); b.f(0, 0, -1); b.f(index / 2, index % 2)
        b.u16(0); b.u16(0); b.u8(100); b.u8(0)
    b.u32(3)
    for index in (0, 1, 2): b.u16(index)
    b.u32(1); b.f(.8, .6, .4, alpha, 4, .1, .1, .1, .3, .3, .3)
    b.u8(255); b.u8(1); b.u32(3); b.fixed(texture, 20)
    b.u16(1); b.fixed('B', 20); b.u16(65535); b.u16(0)
    b.u8(0); b.u16(0); b.f(0, 0, 0); b.u16(0)  # no IK
    b.u16(1); b.fixed('F', 20); b.u32(3); b.u8(0)  # base morph
    for index, p in enumerate(POSITIONS): b.u32(index); b.f(*p)
    b.u8(0); b.u8(1); b.fixed('Root', 50)
    b.u32(1); b.u16(0); b.u8(0); b.u8(0)  # display, English flag
    for index in range(10): b.fixed('toon%02d.bmp' % (index + 1), 100)
    b.u32(0); b.u32(0)
    return b


def pmx(material_flags=0x11):
    b = Bytes(b'PMX '); b.f(2); b.u8(8)
    b.extend(bytes((0, 0, 2, 1, 1, 1, 1, 1)))
    for name in ('CPU triangle', '', '', ''): b.text(name)
    b.u32(3)
    for index, p in enumerate(POSITIONS):
        b.f(*p); b.f(0, 0, -1); b.f(index / 2, index % 2)
        b.u8(0); b.u8(0); b.f(1)  # BDEF1, edge scale
    b.u32(3)
    for index in (0, 1, 2): b.u16(index)
    b.u32(1); b.text('checker.bmp')
    b.u32(1); b.text('M'); b.text('')
    b.f(.8, .6, .4, 1, .1, .1, .1, 4, .3, .3, .3)
    b.u8(material_flags); b.f(0, 0, 0, 1, 1)  # explicit PMX material flags
    b.extend(bytes((0, 255, 0, 1, 0)))  # base, no sphere, shared Toon 1
    b.text(''); b.u32(3)
    b.u32(1); b.text('B'); b.text(''); b.f(0, 0, 0)
    b.u8(255); b.u32(0); b.u16(9); b.u8(0)
    b.u32(1); b.text('F'); b.text(''); b.u8(4); b.u8(1)
    b.u32(1); b.u16(0); b.f(2, 0, 0)  # vertex morph
    b.u32(1); b.text('Root'); b.text(''); b.u8(1)
    b.u32(1); b.u8(0); b.u8(0)
    b.u32(0); b.u32(0)
    return b


def articulated_pmd():
    b = Bytes(b'Pmd'); b.f(1); b.fixed('CPU IK physics', 20); b.zero(256)
    b.u32(3)
    for index, p in enumerate(POSITIONS):
        b.f(*p); b.f(0, 0, -1); b.f(index / 2, index % 2)
        b.u16(1); b.u16(0); b.u8(70); b.u8(0)
    b.u32(3)
    for index in (0, 1, 2): b.u16(index)
    b.u32(1); b.f(.8, .6, .4, 1, 4, .1, .1, .1, .3, .3, .3)
    b.u8(255); b.u8(1); b.u32(3); b.fixed('checker.bmp', 20)
    bones = (('R', 65535, 1, 1, (0, 5, 0)), ('L', 0, 2, 4, (0, 6, 0)),
             ('E', 1, 0, 6, (0, 8, 0)), ('I', 65535, 2, 2, (2, 8, 0)))
    b.u16(len(bones))
    for name, parent, tail, kind, position in bones:
        b.fixed(name, 20); b.u16(parent); b.u16(tail); b.u8(kind); b.u16(0); b.f(*position)
    b.u16(1); b.u16(3); b.u16(2); b.u8(1); b.u16(8); b.f(.25); b.u16(1)
    b.u16(1); b.fixed('F', 20); b.u32(3); b.u8(0)
    for index, p in enumerate(POSITIONS): b.u32(index); b.f(*p)
    b.u8(0); b.u8(1); b.fixed('Root', 50); b.u32(4)
    for index in range(4): b.u16(index); b.u8(0)
    b.u8(0)
    for index in range(10): b.fixed('toon%02d.bmp' % (index + 1), 100)
    b.u32(1); b.fixed('dynamic', 20); b.u16(0); b.u8(0); b.u16(0); b.u8(0)
    b.f(.6, .6, .6, 0, 5, 0, 0, 0, 0, 1, .1, .1, .1, .5); b.u8(1)
    b.u32(0)
    return b


def bone_only_pmd():
    b = Bytes(b'Pmd'); b.f(1); b.fixed('CPU attachment', 20); b.zero(256)
    b.u32(0); b.u32(0); b.u32(0)
    b.u16(1); b.fixed('B', 20); b.u16(65535); b.u16(0); b.u8(1); b.u16(0); b.f(0, 0, 0)
    b.u16(0); b.u16(1); b.fixed('F', 20); b.u32(0); b.u8(0)
    b.u8(0); b.u8(1); b.fixed('Root', 50); b.u32(1); b.u16(0); b.u8(0); b.u8(0)
    for index in range(10): b.fixed('toon%02d.bmp' % (index + 1), 100)
    b.u32(0); b.u32(0)
    return b


def articulated_pmx():
    b = Bytes(b'PMX '); b.f(2); b.u8(8); b.extend(bytes((0, 0, 2, 1, 1, 1, 1, 1)))
    for name in ('CPU IK physics SDEF', '', '', ''): b.text(name)
    b.u32(3)
    for index, p in enumerate(POSITIONS):
        b.f(*p); b.f(0, 0, -1); b.f(index / 2, index % 2)
        b.u8(3); b.u8(0); b.u8(1); b.f(.5, 0, 5, 0, -1, 5, 0, 1, 5, 0, 1)
    b.u32(3)
    for index in (0, 1, 2): b.u16(index)
    b.u32(1); b.text('checker.bmp'); b.u32(1); b.text('M'); b.text('')
    b.f(.8, .6, .4, 1, .1, .1, .1, 4, .3, .3, .3)
    b.u8(0x11); b.f(0, 0, 0, 1, 1); b.extend(bytes((0, 255, 0, 1, 0))); b.text(''); b.u32(3)
    b.u32(4)
    for index, (name, parent, tail, position) in enumerate((('R', 255, 1, (0, 5, 0)),
            ('L', 0, 2, (0, 6, 0)), ('E', 1, 0, (0, 8, 0)), ('I', 255, 2, (2, 8, 0)))):
        b.text(name); b.text(''); b.f(*position); b.u8(parent); b.u32(0)
        b.u16(0x29 if index == 3 else 9); b.u8(tail)
        if index == 3: b.u8(2); b.u32(8); b.f(.25); b.u32(1); b.u8(1); b.u8(0)
    b.u32(1); b.text('F'); b.text(''); b.u8(4); b.u8(2); b.u32(1)
    b.u8(3); b.f(.3, .2, 0, 0, 0, 0, 1)
    b.u32(1); b.text('Root'); b.text(''); b.u8(1); b.u32(4)
    for index in range(4): b.u8(0); b.u8(index)
    b.u32(1); b.text('dynamic'); b.text(''); b.u8(0); b.u8(0); b.u16(0); b.u8(0)
    b.f(.6, .6, .6, 0, 5, 0, 0, 0, 0, 1, .1, .1, .1, .5); b.u8(1); b.u32(0)
    return b


def scene(version=2, model='triangle.pmd', articulated=False, model_count=1, bindings=False, passes=False, reverse_display=False, capture_mode=0):
    b = Bytes(); b.fixed('Polygon Movie maker ' + ('0002' if version == 2 else '0001'), 30)
    b.u32(640); b.u32(480); b.u32(200); b.f(45)
    b.zero(7 if version == 2 else 6); b.u8(1); b.u8(model_count)
    if version == 1:
        for _ in range(model_count): b.fixed('CPU triangle', 20)
    for order in range(model_count):
        _scene_model(b, version, model, articulated, order, model_count-order-1 if reverse_display else order)
    # Initial camera key and current camera state.
    b.zero(12); b.f(-45, 0, 0, -45 if bindings else 0, 0, 0, 0)
    if version == 2: b.u32(1 if bindings else 0xffffffff); b.u32(1 if bindings else 0xffffffff)
    b.zero(24); b.u8(1); b.u32(45); b.u8(0); b.u32(0)
    b.f(0, 0, 0, 0, 0, -45, 0, 0, 0); b.u8(1)
    b.zero(12); b.f(.6, .6, .6, -.5, -1, -.5); b.u8(1); b.u32(0)
    b.f(.6, .6, .6, -.5, -1, -.5)
    b.u8(0); b.u32(0); b.u8(2 if bindings else 0)
    if bindings:
        for i in range(2): b.fixed('bound accessory ' + str(i), 100)
        for i in range(2):
            b.u8(i); b.fixed('bound accessory', 100); b.fixed('bound.x', 256); b.u8(i)
            def accessory_key(frame, previous, following, flag, x):
                b.u32(frame); b.u32(previous); b.u32(following); b.u8(flag)
                b.u32(1); b.u32(1); b.f(x, 0, 0, .1, .2, -.1, .12); b.u8(0); b.u8(1)
            accessory_key(0, 0, 1, 1, i * 2)
            b.u32(1); b.u32(1); accessory_key(10, 0, 0, 51, i * 2 + 1)
            b.u8(1); b.u32(1); b.u32(1); b.f(.1, .2, -.1, .12, i * 2, 0, 0); b.u8(0)
            if version == 2: b.u8(i)
    b.zero(28); b.zero(257); b.zero(272); b.zero(269)
    b.u8(0); b.u8(int(passes)); b.u8(int(passes)); b.f(60); b.u32(capture_mode)
    if version == 1 and bindings:
        # Optional v1 gated tail through the camera-parent table (stage 9).
        b.u32(1); b.f(.5); b.zero(model_count); b.u8(0); b.u8(1)
        b.f(1); b.u8(0); b.u8(1)
        for _ in range(model_count): b.f(1)
        b.u8(1); b.u8(2); b.f(9.8); b.u32(10); b.f(0, -1, 0); b.u8(0)
        b.u8(1); b.u8(0); b.f(0); b.zero(model_count)
        b.u8(1); b.zero(18); b.u32(0)
        b.u8(1); b.u32(0); b.u32(0); b.u32(0)
        b.u8(1); b.u8(0)
        b.u8(1); b.u32(1); b.u32(1); b.u32(0); b.u32(1); b.u32(1)
    if version == 2:
        b.u32(1 if bindings else 0); b.f(.5); b.u8(1 if passes else 0); b.u8(2 if articulated else 0)
        b.f(9.8); b.u32(10); b.f(0, -1, 0); b.u8(0)
        b.zero(12); b.u8(0); b.u32(10); b.f(9.8, 0, -1, 0); b.u8(1); b.u32(0)
        b.u8(0); b.u32(0); b.zero(18); b.u32(0); b.zero(92); b.u8(1)
        for order in range(model_count): b.u8(order + 1); b.u32(5)
    return b


def _scene_model(b, version, model, articulated, order, display_order):
    names = ('R', 'L', 'E', 'I') if articulated else ('B',)
    b.u8(order + 1)
    if version == 1: b.fixed('CPU triangle', 20)
    else:
        b.extend(b'\x01M\x01M')
    b.fixed(model, 256)
    if version == 2:
        b.u8(1); b.u32(len(names))
        for name in names: b.u8(1); b.extend(name.encode('ascii'))
        b.u32(1); b.extend(b'\x01F'); b.u32(1 if articulated else 0)
        if articulated: b.u32(3)
        b.u32(1 if articulated else 0)
        if articulated: b.u32(0xffffffff)
    b.u8(display_order); b.u8(1); b.u32(0)
    for _ in range(4): b.u32(0)
    b.u8(1 if version == 1 else 0); b.u8(1); b.u32(0); b.u32(100)
    # Independent animated bone: the vertex/index/texture identities must
    # survive geometry changes; the final solved pose contains no IK/physics.
    def bone(frame, previous_key, next_key, x, disabled=0):
        b.u32(frame); b.u32(previous_key); b.u32(next_key)
        b.extend(bytes((20,) * 8 + (107,) * 8))
        b.f(x, 0, 0, 0, 0, 0, 1); b.u8(1)
        if version == 2: b.u8(disabled)
    for index in range(len(names)): bone(0, 0, len(names) + index, 0)
    b.u32(len(names))
    for index in range(len(names)):
        b.u32(len(names) + index); bone(10, index, 0, 3 if index in (0, 3) else 0, int(articulated and index == 0))
    b.u32(0); b.u32(0); b.u32(1 if articulated else 0); b.f(0); b.u8(1)
    b.u32(1 if articulated else 0)
    if articulated: b.u32(1); b.u32(10); b.u32(0); b.u32(0); b.f(.5); b.u8(1)
    b.zero(12); b.u8(1)
    if articulated:
        b.u8(1)
        if version == 2: b.u32(0xffffffff); b.u32(0)
    b.u8(1); b.u32(0)
    for _ in names:
        b.f(0, 0, 0, 0, 0, 0, 1)
        if version == 1: b.f(0)
        b.zero(2 if version == 1 else 3)
    b.f(0)
    if articulated: b.u8(1)
    if version == 2:
        if articulated: b.zero(8); b.u32(0xffffffff); b.u32(0)
        b.u8(0); b.f(1); b.u8(0); b.u8(order)


def write(directory):
    root = Path(directory)
    root.mkdir(parents=True, exist_ok=True)
    (root / 'triangle.pmd').write_bytes(pmd())
    (root / 'sphere.pmd').write_bytes(pmd('s.sph'))
    (root / 'triangle.pmx').write_bytes(pmx())
    (root / 'alpha98.pmd').write_bytes(pmd(alpha=.98))
    for flags in (0x1f, 0x17, 0x1b, 0x0c):
        (root / ('flags-%02x.pmx' % flags)).write_bytes(pmx(flags))
    # 2x2, uncompressed BGRA. Alpha is opaque so rasterization is unambiguous.
    pixels = bytes((80, 150, 240, 255, 140, 200, 100, 255)) * 2
    bmp = b'BM' + struct.pack('<IHHI', 54 + len(pixels), 0, 0, 54)
    bmp += struct.pack('<IiiHHIIIIII', 40, 2, 2, 1, 32, 0, len(pixels), 0, 0, 0, 0)
    (root / 'checker.bmp').write_bytes(bmp + pixels)
    (root / 's.sph').write_bytes(bmp + pixels)
    (root / 'v1.pmm').write_bytes(scene(1))
    (root / 'v2.pmm').write_bytes(scene(2))
    (root / 'pmx.pmm').write_bytes(scene(2, 'triangle.pmx'))
    (root / 'articulated.pmd').write_bytes(articulated_pmd())
    (root / 'bone-only.pmd').write_bytes(bone_only_pmd())
    (root / 'articulated.pmx').write_bytes(articulated_pmx())
    (root / 'ik-v1.pmm').write_bytes(scene(1, 'articulated.pmd', True))
    (root / 'physics-v2.pmm').write_bytes(scene(2, 'articulated.pmd', True, 2))
    (root / 'reverse-v2.pmm').write_bytes(scene(2, 'articulated.pmd', True, 2, reverse_display=True))
    (root / 'sdef-physics-v2.pmm').write_bytes(scene(2, 'articulated.pmx', True, 2))
    (root / 'bound-v2.pmm').write_bytes(scene(2, 'articulated.pmd', True, bindings=True))
    (root / 'passes-v2.pmm').write_bytes(scene(2, 'articulated.pmd', True, bindings=True, passes=True))
    (root / 'bound-v1.pmm').write_bytes(scene(1, 'articulated.pmd', True, bindings=True))
    (root / 'bound.x').write_text('''xof 0303txt 0032
Mesh {
4;
-2.0;-2.0;0.0;, 0.0;2.0;0.0;, 2.0;-2.0;0.0;, 3.0;1.0;0.0;;
2; 3;0,1,2;, 3;2,1,3;;
MeshNormals { 1; 0.0;0.0;-1.0;; 2; 3;0,0,0;, 3;0,0,0;; }
MeshTextureCoords { 4; 0.0;0.0;, 0.5;1.0;, 1.0;0.0;, 1.0;1.0;; }
MeshMaterialList { 2; 2; 0,1;;
Material { 0.7;0.4;0.2;0.8;; 4.0; 0.1;0.2;0.3;; 0.0;0.0;0.0;; TextureFilename { "checker.bmp"; } }
Material { 0.2;0.7;0.4;0.6;; 2.0; 0.0;0.0;0.0;; 0.1;0.0;0.0;; }
}
}
''', encoding='ascii')
    varied = (root / 'bound.x').read_text(encoding='ascii')
    varied = varied.replace('-2.0;-2.0;0.0', '-2.625;-1.4375;0.3125')
    varied = varied.replace('0.0;2.0;0.0', '0.875;2.25;-0.625')
    varied = varied.replace('0.0;0.0;-1.0', '0.37;-0.62;-0.41')
    (root / 'bound-varied.x').write_text(varied, encoding='ascii')


if __name__ == '__main__':
    import sys
    write(sys.argv[1])
