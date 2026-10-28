#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
strcheck2.py -- MikuMikuDance x64 original .rdata string coverage audit.

Rebuilds the former build/strcheck.py audit (deleted): extracts every
meaningful C string (ASCII + Shift-JIS) from the x64 original's .rdata,
then checks whether the C++ port (src/, include/) still carries it.

Matching strategy (ordered, first hit wins):
  raw           -- byte substring windows (8..16 chars, step 4) of the
                   target found in escape-decoded code (comments removed
                   with a literal-aware state machine); whole string when
                   shorter than 8 chars.
  concat        -- windows matched after gluing adjacent string literals
                   ("abc" "def"), so splits shorter than the window hit.
  fmt / fmt+    -- same windowing after normalizing printf format
  concat          specifiers to \x01 on both sides; kills %s/%d drift.
  wide          -- Japanese targets: Unicode codepoint sequence found in
                   numeric wchar_t arrays ({0xFF76, 0xFF92, ...}) for the
                   SendMessageW paths.
  comment-only  -- found solely inside comments (developer annotated the
                   original string but no code literal carries it).

Usage:
  python tools/strcheck2.py [--exe PATH] [--repo PATH] [--json OUT]

Defaults point at the canonical x64 binary and this repository.
The generated report defaults to out/audits/strcheck2_result.json.
Exit code 0 always; this is an audit, not a gate.
"""

import argparse
import json
import os
import re
import struct
import sys

DEFAULT_EXE = r"C:\Users\jstzw\Documents\github\OpenMMD\MikuMikuDanceE_v932x64\MikuMikuDance.exe"
DEFAULT_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SOURCE_EXT = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc", ".inl", ".rc", ".def"}

# ---------------------------------------------------------------------------
# PE parsing
# ---------------------------------------------------------------------------

def load_sections(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"MZ":
        raise ValueError("not a PE file")
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\x00\x00":
        raise ValueError("bad PE signature")
    coff = e_lfanew + 4
    machine, nsec, _, _, _, opt_size, _ = struct.unpack_from("<HHIIIHH", data, coff)
    opt = coff + 20
    if machine != 0x8664:
        raise ValueError("expected x64 PE (machine=0x%x)" % machine)
    image_base = struct.unpack_from("<Q", data, opt + 24)[0]  # PE32+
    secs = []
    sec_off = opt + opt_size
    for i in range(nsec):
        off = sec_off + 40 * i
        name = data[off:off + 8].rstrip(b"\x00").decode("ascii", "replace")
        vsize, vaddr, rsize, rptr = struct.unpack_from("<IIII", data, off + 8)
        secs.append({"name": name, "va": image_base + vaddr, "vsize": vsize,
                     "rptr": rptr, "rsize": rsize})
    # DataDirectory RVAs to exclude from string auditing (imports, debug, ...)
    num_dd = struct.unpack_from("<I", data, opt + 108)[0]
    dd_off = opt + 112
    dd_names = ["export", "import", "resource", "exception", "security",
                "reloc", "debug", "copyright", "globalptr", "tls", "loadcfg",
                "bound", "iat", "delay", "clr", "reserved"]
    excl = []
    imp_rva = imp_size = 0
    for i in range(min(num_dd, 16)):
        rva, size = struct.unpack_from("<II", data, dd_off + 8 * i)
        if dd_names[i] == "import":
            imp_rva, imp_size = rva, size
        if rva and size and dd_names[i] in ("import", "debug", "iat", "delay", "bound", "clr"):
            excl.append((image_base + rva, image_base + rva + size, dd_names[i]))

    def rva2off(rva):
        for s in secs:
            if s["va"] - image_base <= rva < s["va"] - image_base + s["rsize"]:
                return s["rptr"] + (rva - (s["va"] - image_base))
        return None

    # Walk IMAGE_IMPORT_DESCRIPTORs: collect dll-name VAs and every
    # hint/name VA (+2 skips the hint word) so import names are excluded
    # even though they live outside the small descriptor array.
    if imp_rva and imp_size:
        off = rva2off(imp_rva)
        if off is not None:
            while True:
                oft, _ts, _fwd, name_rva, ft = struct.unpack_from("<IIIII", data, off)
                if oft == 0 and name_rva == 0 and ft == 0:
                    break
                if name_rva:
                    excl.append((image_base + name_rva,
                                 image_base + name_rva + 24, "impdll"))
                thunk_rva = oft or ft
                toff = rva2off(thunk_rva)
                while toff is not None:
                    th = struct.unpack_from("<Q", data, toff)[0]
                    if th == 0:
                        break
                    if not (th & (1 << 63)):  # by-name import
                        hn_rva = th & 0xFFFFFFFF
                        excl.append((image_base + hn_rva + 2,
                                     image_base + hn_rva + 2 + 32, "impname"))
                    toff += 8
                off += 20
    return data, secs, excl


# ---------------------------------------------------------------------------
# .rdata C-string scanning (ASCII + Shift-JIS)
# ---------------------------------------------------------------------------

def is_sjis_lead(b):
    return 0x81 <= b <= 0x9F or 0xE0 <= b <= 0xEF


def is_sjis_trail(b):
    return (0x40 <= b <= 0x7E) or (0x80 <= b <= 0xFC)


def is_hw_kana(b):
    return 0xA1 <= b <= 0xDF


def scan_strings(data, sec, excl, min_ascii=5, min_jp=2):
    """Yield (va, raw_bytes, kind, text) for NUL-terminated strings."""
    blob = data[sec["rptr"]:sec["rptr"] + sec["rsize"]]
    base = sec["va"]
    n = len(blob)

    def excluded(va):
        for lo, hi, _nm in excl:
            if lo <= va < hi:
                return True
        return False

    i = 0
    out = []
    while i < n:
        b = blob[i]
        if b == 0:
            i += 1
            continue
        if b >= 0x80:
            # Shift-JIS string starting here (may contain ASCII inside)
            j = i
            raw = bytearray()
            jp = 0
            while j < n:
                c = blob[j]
                if c == 0:
                    break
                if is_sjis_lead(c) and j + 1 < n and is_sjis_trail(blob[j + 1]):
                    raw += blob[j:j + 2]
                    j += 2
                    jp += 1
                elif is_hw_kana(c):
                    raw.append(c)
                    j += 1
                    jp += 1
                elif 0x20 <= c <= 0x7E or c in (0x0A, 0x09):
                    raw.append(c)
                    j += 1
                else:
                    break
            if jp >= 1 and j < n and blob[j] == 0 and len(raw) >= 3:
                try:
                    text = raw.decode("cp932")
                except UnicodeDecodeError:
                    text = None
                if text:
                    wide = sum(1 for ch in text if ord(ch) > 0x7F)
                    if wide >= min_jp or (wide >= 1 and len(text) >= 4):
                        if not excluded(base + i):
                            out.append((base + i, bytes(raw), "sjis", text))
                        i = j + 1
                        continue
            i += 1
            continue
        # ASCII run
        j = i
        raw = bytearray()
        while j < n:
            c = blob[j]
            if 0x20 <= c <= 0x7E or c in (0x0A, 0x09):
                raw.append(c)
                j += 1
            else:
                break
        if len(raw) >= min_ascii and (j >= n or blob[j] == 0):
            if not excluded(base + i):
                out.append((base + i, bytes(raw), "ascii", raw.decode("ascii")))
        i = j if j > i else i + 1
    return out


# ---------------------------------------------------------------------------
# Port source corpus
# ---------------------------------------------------------------------------

ESC_RE = re.compile(rb"""\\(?:x([0-9A-Fa-f]{1,2})|([0-7]{1,3})|([nrtabfv'"\\?]))""")
SIMPLE_ESC = {b"n": b"\n", b"r": b"\r", b"t": b"\t", b"a": b"\a",
              b"b": b"\b", b"f": b"\f", b"v": b"\v"}


def unescape_bytes(raw: bytes) -> bytes:
    """Decode C escape sequences to raw bytes (\\xNN, octal, \\n ...)."""
    def repl(m):
        hx, oct_, simple = m.group(1), m.group(2), m.group(3)
        if hx is not None:
            return bytes([int(hx, 16)])
        if oct_ is not None:
            return bytes([int(oct_, 8) & 0xFF])
        return SIMPLE_ESC.get(simple, simple)
    return ESC_RE.sub(repl, raw)


def strip_comments(raw: bytes):
    """Remove // and /* */ comments while respecting string/char literals.

    Linear state machine (no regex backtracking). Returns (code, comments).
    """
    out = bytearray()
    cmt = bytearray()
    i, n = 0, len(raw)
    NORMAL, STR, CHR, LINE, BLOCK = 0, 1, 2, 3, 4
    st = NORMAL
    while i < n:
        c = raw[i]
        if st == NORMAL:
            if c == 0x22:  # "
                st = STR
                out.append(c)
                i += 1
            elif c == 0x27:  # '
                st = CHR
                out.append(c)
                i += 1
            elif c == 0x2F and i + 1 < n and raw[i + 1] == 0x2F:  # //
                st = LINE
                i += 2
            elif c == 0x2F and i + 1 < n and raw[i + 1] == 0x2A:  # /*
                st = BLOCK
                i += 2
            else:
                out.append(c)
                i += 1
        elif st in (STR, CHR):
            q = 0x22 if st == STR else 0x27
            if c == 0x5C:  # backslash: keep escape pair verbatim
                out += raw[i:i + 2]
                i += 2
            elif c == q:
                st = NORMAL
                out.append(c)
                i += 1
            else:
                out.append(c)
                i += 1
        elif st == LINE:
            if c == 0x0A:
                st = NORMAL
                out.append(c)
            else:
                cmt.append(c)
            i += 1
        else:  # BLOCK
            if c == 0x2A and i + 1 < n and raw[i + 1] == 0x2F:
                st = NORMAL
                i += 2
            else:
                cmt.append(c)
                i += 1
    return bytes(out), bytes(cmt)


def glue_literals(code: bytes) -> bytes:
    """Join adjacent string literals: "abc" <ws>* "def" -> "abcdef".

    Operates on comment-free text; linear scan. The joining quote pair is
    replaced with nothing so window searches see the full string.
    """
    out = bytearray()
    i, n = 0, len(code)
    while i < n:
        c = code[i]
        if c == 0x22:
            out.append(c)
            i += 1
            while i < n:
                d = code[i]
                if d == 0x5C:  # escape
                    out += code[i:i + 2]
                    i += 2
                    continue
                out.append(d)
                i += 1
                if d == 0x22:  # closing quote
                    # look ahead: whitespace then another quote?  Stay in
                    # the inner loop so a whole chain of literals glues
                    # without re-interpreting closing quotes as openers.
                    k = i
                    while k < n and code[k] in b" \t\r\n":
                        k += 1
                    if k < n and code[k] == 0x22:
                        i = k + 1  # skip the separator and opening quote
                        continue
                    break
        else:
            out.append(c)
            i += 1
    return bytes(out)


HEX16_RE = re.compile(rb"0x([0-9A-Fa-f]{4})\b")


def build_corpus(roots):
    decoded, glued, comments, wide = [], [], [], []
    files = []
    for repo in roots:
        for root, _dirs, names in os.walk(repo):
            if os.sep + ".git" in root:
                continue
            for nm in names:
                if os.path.splitext(nm)[1].lower() in SOURCE_EXT:
                    files.append(os.path.join(root, nm))
    for path in sorted(files):
        with open(path, "rb") as f:
            raw = f.read()
        code, cmt = strip_comments(raw)
        dec = unescape_bytes(code)
        rel = os.path.relpath(path, os.path.dirname(os.path.dirname(path)))
        tag = b"\n@FILE@" + rel.encode("utf-8", "replace").replace(b"\\", b"/") + b"\n"
        decoded.append(tag + dec)
        glued.append(tag + unescape_bytes(glue_literals(code)))
        comments.append(tag + unescape_bytes(cmt))
        wide.append((rel, [int(m.group(1), 16) for m in HEX16_RE.finditer(dec)]))
    return b"".join(decoded), b"".join(glued), b"".join(comments), wide


FMT_RE = re.compile(rb"%[-#+ 0]*[0-9]*(?:\.[0-9]+)?(?:l|h|hh|ll|I64|w)*[sdcxXufeEgG%]")


def normalize_fmt(b: bytes) -> bytes:
    return FMT_RE.sub(b"\x01", b)


# ---------------------------------------------------------------------------
# Matching
# ---------------------------------------------------------------------------

def windows(b: bytes, lo=8, hi=16, step=4):
    L = len(b)
    if L < lo:
        return [b]
    out = []
    for w in (hi, lo):
        if w > L:
            continue
        if w == L:
            if b not in out:
                out.append(b)
            continue
        for s in range(0, L - w + 1, step):
            win = b[s:s + w]
            if win not in out:
                out.append(win)
        if out:
            return out
    return [b]


def find_raw(target: bytes, corpus: bytes):
    for w in windows(target):
        if w in corpus:
            return w
    return None


def find_wide(cps, wide_corpus):
    if len(cps) < 2:
        return None
    n = min(4, len(cps))
    wins = [cps[i:i + n] for i in range(0, len(cps) - n + 1)] if len(cps) > n else [cps]
    for rel, seqs in wide_corpus:
        if not seqs:
            continue
        for win in wins:
            L = len(win)
            for i in range(len(seqs) - L + 1):
                if seqs[i:i + L] == win:
                    return rel
    return None


# ---------------------------------------------------------------------------
# Categorisation for triage (content heuristics; refined by xrefs later)
# ---------------------------------------------------------------------------

BULLET_HINTS = [
    b"continuousphysics", b"Overflow in AABB", b"btAssert", b"Bullet",
    b"STATICPLANE", b"SPHERE", b"CapsuleShape", b"ConeTwist", b"GIMPACT",
    b"Thanks.", b"Platform, version of OS", b"ConvexHullShape",
    b"soft body", b"SoftBody", b"dbvt", b"contact point", b"damping",
    b"deactivation", b"activation", b"constraint", b"friction", b"restitution",
]
CRT_HINTS = [
    b"bad allocation", b"Microsoft Visual C++ Runtime",
    b"pure virtual function call", b"bad array new length", b"FlsAlloc",
    b"stdio.h", b"rtcapi", b"_CrtDbgReport",
]


def categorize(raw: bytes):
    low = raw.lower()
    for h in CRT_HINTS:
        if h.lower() in low:
            return "crt"
    for h in BULLET_HINTS:
        if h.lower() in low:
            return "bullet"
    return "mmd"


def esc(b) -> str:
    if isinstance(b, str):
        b = b.encode("cp932", "replace")
    out = []
    for ch in b:
        if ch == 0x5C:
            out.append("\\\\")  # keep the backslash unambiguous
        elif 0x20 <= ch <= 0x7E:
            out.append(chr(ch))
        else:
            out.append("\\x%02X" % ch)
    return "".join(out)


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="MMD x64 .rdata string coverage audit")
    ap.add_argument("--exe", default=DEFAULT_EXE)
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--json", default=os.path.join(
        DEFAULT_REPO, "out", "audits", "strcheck2_result.json"))
    ap.add_argument("--min-ascii", type=int, default=5)
    ap.add_argument("--rebase", type=lambda x: int(x, 0), default=0x7FF68B420000,
                    help="IDA rebase delta (IDA VA = file VA + delta); "
                         "the x64 IDB session runs at 0x7ff7cb420000 while "
                         "the file prefers 0x140000000 "
                         "(0x7ff7cb420000 - 0x140000000 = 0x7FF68B420000)")
    args = ap.parse_args()

    data, secs, excl = load_sections(args.exe)
    rdata = [s for s in secs if s["name"] == ".rdata"]
    if not rdata:
        print("no .rdata", file=sys.stderr)
        return 2
    rdata = rdata[0]
    for lo, hi, nm in excl:
        print("excluding data directory %-8s 0x%X..0x%X" % (nm, lo, hi), file=sys.stderr)

    strings = scan_strings(data, rdata, excl, min_ascii=args.min_ascii)
    print("extracted %d C-strings from .rdata" % len(strings), file=sys.stderr)

    decoded, glued, comments, wide_corpus = build_corpus(
        [os.path.join(args.repo, "src"), os.path.join(args.repo, "include")])

    decoded_fmt = normalize_fmt(decoded)
    glued_fmt = normalize_fmt(glued)
    comments_fmt = normalize_fmt(comments)

    # dedupe by content keeping all VAs
    by_text = {}
    for va, raw, kind, text in strings:
        e = by_text.setdefault(raw, {"vas": [], "kind": kind, "text": text})
        e["vas"].append(va)

    results = []
    for raw, e in sorted(by_text.items(), key=lambda kv: kv[1]["vas"][0]):
        status, how = "MISSING", ""
        utf8 = e["text"].encode("utf-8") if e["kind"] == "sjis" else None
        if find_raw(raw, decoded):
            status, how = "FOUND", "raw"
        elif find_raw(raw, glued):
            status, how = "FOUND", "concat"
        elif find_raw(normalize_fmt(raw), decoded_fmt):
            status, how = "FOUND", "fmt"
        elif find_raw(normalize_fmt(raw), glued_fmt):
            status, how = "FOUND", "fmt+concat"
        elif utf8 is not None and find_raw(utf8, decoded):
            status, how = "FOUND", "utf8"
        elif utf8 is not None and find_raw(utf8, glued):
            status, how = "FOUND", "utf8+concat"
        elif e["kind"] == "sjis":
            hit = find_wide([ord(c) for c in e["text"]], wide_corpus)
            if hit:
                status, how = "FOUND", "wide:" + hit
        if status == "MISSING":
            if find_raw(raw, comments):
                status, how = "COMMENT-ONLY", "comment"
            elif find_raw(normalize_fmt(raw), comments_fmt):
                status, how = "COMMENT-ONLY", "fmt@comment"
            elif utf8 is not None and find_raw(utf8, comments):
                status, how = "COMMENT-ONLY", "utf8@comment"
        results.append({
            "vas": ["0x%X" % v for v in e["vas"]],
            "ida_vas": ["0x%X" % (v + args.rebase) for v in e["vas"]],
            "kind": e["kind"],
            "raw": esc(raw),
            "text": e["text"],
            "category": categorize(raw),
            "status": status,
            "how": how,
        })

    missing = [r for r in results if r["status"] != "FOUND"]
    found = len(results) - len(missing)
    print("total unique: %d   found: %d   missing/comment-only: %d"
          % (len(results), found, len(missing)))
    print()
    for cat in ("mmd", "bullet", "crt"):
        miss_cat = [r for r in missing if r["category"] == cat]
        if not miss_cat:
            continue
        print("=== MISSING [%s] (%d) ===" % (cat, len(miss_cat)))
        for r in miss_cat:
            label = r["text"] if r["kind"] == "ascii" else "%s   <%s>" % (r["text"], r["raw"][:48])
            if len(label) > 120:
                label = label[:117] + "..."
            print("  %-18s %-12s %s" % (",".join(r["vas"]), r["status"], label))
        print()

    os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
    with open(args.json, "w", encoding="utf-8") as f:
        json.dump(results, f, ensure_ascii=False, indent=1)
    print("details -> %s" % args.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
