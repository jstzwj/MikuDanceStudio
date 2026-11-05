#!/usr/bin/env python3
"""Regenerate res/gen_resources(.x64)?.res from the native-format sources in res/.

Sources of truth (all committed to the repo, no original exe needed):
  res/assets/      native-format leaf resources (png / bmp / ico / fx / x)
  res/templates/   dialogs.rc / menus.rc - the .rc source text for the 48
                   RT_DIALOG (600..817) and 2 RT_MENU (SAMPLE02[E])
                   templates; compiled with rc.exe (Windows SDK) at gen /
                   verify time, re-emitting the original template bytes
                   verbatim (scripts/dialog_rc.py verifies that round-trip)
  res/mmd_manifest.xml / res/mmd_manifest_x64.xml

The .res files are COFF object resources assembled here directly (only the
two template .rc files go through rc.exe), in the exact record order of the
original MikuMikuDance.exe .rsrc tree so that the linked resources stay
byte-identical to the original binary:
  PNG(string) XFILE(string) BITMAP ICON MENU DIALOG RCDATA GROUP_ICON MANIFEST

Embedding is byte-exact per leaf:
  .png / .fx / .x / .xml                   -> file bytes verbatim
  dialogs.rc / menus.rc                    -> compiled by rc.exe; the emitted
                                              template blobs are byte-ident
                                              ical to the original ones
  .bmp                                     -> file bytes minus the 14-byte
                                              BITMAPFILEHEADER (RT_BITMAP is
                                              a bare DIB)
  .ico                                     -> split into RT_ICON 1..6 (image
                                              blobs) + RT_GROUP_ICON 100
                                              (ICONDIR + first 12 bytes of
                                              each ICONDIRENTRY + WORD id)

Usage (gen/verify need rc.exe from a Windows 10 SDK, or RC_EXE set):
  python scripts/gen_resources.py gen      # rewrite res/gen_resources*.res
  python scripts/gen_resources.py verify   # gen + byte-compare, no writes
"""
import argparse
import os
import struct
import sys

import dialog_rc  # sibling script: rc.exe location + .rc compile + .res parse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "res")

RT_BITMAP, RT_ICON, RT_MENU, RT_DIALOG = 2, 3, 4, 5
RT_RCDATA, RT_GROUP_ICON, RT_MANIFEST = 10, 14, 24
LANG_JP, LANG_EN = 0x411, 0x409

APP_ICON = "assets/app.ico"

# (type, name, lang, path relative to res/) - order defines the .res record
# order and MUST match the original .rsrc tree.  Dialog/menu template paths
# are expanded by glob in record_specs() below.
PNG_MAP = [
    (102, "hud_sprites.png"),        # viewport HUD sprite sheet (OverlayTexture)
    (103, "toon00.png"),             # default toon ramp, slot 0
    (104, "toon01.png"),             # data/toonNN.bmp fallbacks, slots 1..10
    (105, "toon02.png"),
    (106, "toon03.png"),
    (107, "toon04.png"),
    (108, "toon05.png"),
    (109, "toon06.png"),
    (110, "toon07.png"),
    (111, "toon08.png"),
    (112, "toon09.png"),
    (113, "toon10.png"),
    (114, "accessory_helper.png"),   # accessory placement helper texture
]


_templates = None


def templates():
    """Compile res/templates/**/*.rc once -> {(rt, name): template blob}.

    Lazy so icon generation can reuse the resource helpers without an SDK;
    gen/verify compile through rc.exe - see scripts/dialog_rc.py for why
    the emitted bytes are the original ones verbatim.
    """
    global _templates
    if _templates is None:
        rcexe = dialog_rc.find_rcexe()
        if not rcexe:
            sys.exit("error: compiling res/templates/**/*.rc needs rc.exe "
                     "from a Windows 10 SDK (or set RC_EXE)")
        _templates = {(typ[1], name[1]): blob
                      for (typ, name), blob
                      in dialog_rc.compile_templates(rcexe).items()}
    return _templates


def dialog_ids():
    return sorted(n for t, n in templates() if t == RT_DIALOG)


def record_specs(manifest):
    """Yield (type, name, lang, res-relative path) in .rsrc tree order.

    A None path marks a template record: its blob comes from compiling the
    res/templates/**/*.rc sources (see templates()).
    """
    for rid, fname in PNG_MAP:
        yield ("PNG", rid, LANG_JP, f"assets/{fname}")
    yield ("XFILE", 115, LANG_JP, "assets/axis.x")
    yield (RT_BITMAP, 101, LANG_JP, "assets/sidebar_icon_sheet.bmp")
    yield (RT_BITMAP, 119, LANG_JP, "assets/playing_indicator.bmp")
    for i in range(1, 7):                     # RT_ICON leaves from app.ico
        yield (RT_ICON, i, LANG_JP, APP_ICON)
    for nm in ("SAMPLE02", "SAMPLE02E"):
        yield (RT_MENU, nm, LANG_JP, None)
    for did in dialog_ids():
        yield (RT_DIALOG, did, LANG_JP, None)
    yield (RT_RCDATA, 117, LANG_JP, "assets/skin_effect_sm2.fx")
    yield (RT_RCDATA, 118, LANG_JP, "assets/skin_effect_sm3.fx")
    yield (RT_GROUP_ICON, 100, LANG_JP, APP_ICON)
    yield (RT_MANIFEST, 1, LANG_EN, manifest)


# --------------------------------------------------------------------------
# native-format loaders
# --------------------------------------------------------------------------

def load_bitmap(path):
    """RT_BITMAP = bare DIB; the .bmp on disk adds a BITMAPFILEHEADER."""
    data = open(path, "rb").read()
    if data[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")
    blob = data[14:]
    bi_size, _, _, _, bpp = struct.unpack_from("<IiiHH", blob, 0)
    clr_used = struct.unpack_from("<I", blob, 32)[0]
    palette = clr_used or (1 << bpp if bpp <= 8 else 0)
    off_bits = 14 + bi_size + 4 * palette
    if off_bits > len(data):
        raise ValueError(f"{path}: bfOffBits {off_bits} beyond EOF {len(data)}")
    return blob


def load_ico_images(path):
    """Return [(id, image_bytes)] and the RT_GROUP_ICON blob for app.ico."""
    data = open(path, "rb").read()
    reserved, ico_type, count = struct.unpack_from("<HHH", data, 0)
    if reserved != 0 or ico_type != 1:
        raise ValueError(f"{path}: not an ICO")
    entries = []
    group = bytearray(data[:6])              # ICONDIR == GRPICONDIR prefix
    for i in range(count):
        e = data[6 + i * 16:6 + i * 16 + 16]
        size, offset = struct.unpack_from("<II", e, 8)
        rid = i + 1                          # file order == RT_ICON id order
        entries.append((rid, data[offset:offset + size]))
        group += e[:12] + struct.pack("<H", rid)
    return entries, bytes(group)


# --------------------------------------------------------------------------
# COFF .res writer (record layout as emitted by rc.exe / the original dump)
# --------------------------------------------------------------------------

def res_field(val):
    """One type/name field of a .res record: ordinal or padded UTF-16 string."""
    if isinstance(val, int):
        return struct.pack("<HH", 0xFFFF, val)
    enc = val.encode("utf-16le") + b"\x00\x00"
    return enc + b"\x00" * ((-len(enc)) % 4)


def render_res(records):
    out = bytearray()
    # 32-byte zero sentinel record rc.exe leads the file with
    out += struct.pack("<II", 0, 32)
    out += struct.pack("<HH", 0xFFFF, 0) * 2
    out += struct.pack("<IHHII", 0, 0, 0, 0, 0)
    for t, n, lang, blob in records:
        head = bytearray()
        head += res_field(t)
        head += res_field(n)
        head += b"\x00" * ((-len(head)) % 4)
        head += struct.pack("<IHHII", 0, 0x1030, lang, 0, 0)
        out += struct.pack("<II", len(blob), 8 + len(head))
        out += head + blob
        out += b"\x00" * ((-len(blob)) % 4)
    return bytes(out)


def build_records(manifest):
    ico_images, ico_group = load_ico_images(os.path.join(RES, APP_ICON))
    images = {rid: blob for rid, blob in ico_images}
    tmpl = templates()
    records = []
    for t, n, lang, rel in record_specs(manifest):
        if rel is None:                       # template from the .rc sources
            blob = tmpl[(t, n)]
        else:
            path = os.path.join(RES, rel)
            if t == RT_BITMAP:
                blob = load_bitmap(path)
            elif t == RT_ICON:
                blob = images[n]
            elif t == RT_GROUP_ICON:
                blob = ico_group
            else:
                blob = open(path, "rb").read()
        records.append((t, n, lang, blob))
    return records


def run_gen(verify_only=False):
    ok = True
    for manifest, out in (("mmd_manifest.xml", "gen_resources.res"),
                          ("mmd_manifest_x64.xml", "gen_resources_x64.res")):
        want = render_res(build_records(manifest))
        out_path = os.path.join(RES, out)
        have = open(out_path, "rb").read()
        if want == have:
            print(f"{out}: byte-identical ({len(have)} bytes)")
        elif verify_only:
            print(f"{out}: MISMATCH "
                  f"(first diff at {next(i for i in (a == b for a, b in zip(want, have)) if not i) or min(len(want), len(have))})")
            ok = False
        else:
            open(out_path, "wb").write(want)
            print(f"{out}: rewritten ({len(want)} bytes)")
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("cmd", choices=["gen", "verify"])
    args = ap.parse_args()
    return 0 if run_gen(verify_only=(args.cmd == "verify")) else 1


if __name__ == "__main__":
    sys.exit(main())
