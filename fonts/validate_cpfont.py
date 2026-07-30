#!/usr/bin/env python3
"""Validate a .cpfont against every check SdCardFont::load() performs.

Mirrors lib/EpdFont/SdCardFont.cpp:521-712 so a bad file is caught on the host
instead of failing to load on the device. Also reports the always-resident
heap cost of the interval tables.
"""
import struct
import sys
from pathlib import Path

MAGIC = b"CPFONT\x00\x00"
HEADER_SIZE = 32
STYLE_TOC_ENTRY_SIZE = 32
CPFONT_VERSION = 4
MAX_STYLES = 4
MAX_INTERVALS = 4096
MAX_GLYPHS = 65536
MAX_KERN_ENTRIES = 4096
UINT16_MAX = 0xFFFF

# Sizes asserted in SdCardFont.cpp:14-17. EpdGlyph is NOT packed: it carries
# 2 alignment padding bytes before dataOffset, hence 16 rather than 14.
SZ_INTERVAL = 12   # EpdUnicodeInterval: u32 first, last, offset
SZ_GLYPH = 16      # EpdGlyph: <BBHhhH2xI
GLYPH_FMT = "<BBHhhH2xI"
SZ_KERNENT = 3     # EpdKernClassEntry: packed u16 + u8
SZ_LIG = 8         # EpdLigaturePair: u32 x2
STYLE_NAMES = {0: "regular", 1: "bold", 2: "italic", 3: "bolditalic"}


def fail(msg):
    print(f"  FAIL: {msg}")
    return False


def validate(path):
    blob = Path(path).read_bytes()
    print(f"\n=== {Path(path).name}  ({len(blob)/1024/1024:.2f} MB) ===")
    ok = True

    if blob[:8] != MAGIC:
        return fail(f"bad magic {blob[:8]!r}")
    version, flags = struct.unpack_from("<HH", blob, 8)
    style_count = blob[12]
    is2bit = bool(flags & 1)
    if version != CPFONT_VERSION:
        ok = fail(f"version {version} != firmware CPFONT_VERSION {CPFONT_VERSION}")
    if not (1 <= style_count <= MAX_STYLES):
        return fail(f"invalid styleCount {style_count}")
    print(f"  magic OK | version {version} | is2Bit {is2bit} | styles {style_count}")

    total_iv_ram = 0
    for i in range(style_count):
        off = HEADER_SIZE + i * STYLE_TOC_ENTRY_SIZE
        (sid, ivc, glc, advY, asc, desc, kL, kR,
         kLc, kRc, ligc, dataOff) = struct.unpack_from("<B3xIIBhhHHBBBI4x", blob, off)
        name = STYLE_NAMES.get(sid, str(sid))
        print(f"\n  style[{sid}] {name}: {ivc} intervals, {glc} glyphs, "
              f"advY={advY} asc={asc} desc={desc} kernL={kL} kernR={kR} "
              f"cls={kLc}x{kRc} ligs={ligc}")

        if sid >= MAX_STYLES:
            ok = fail(f"styleId {sid} >= MAX_STYLES")
            continue
        # SdCardFont.cpp:586-595
        if ivc > MAX_INTERVALS:
            ok = fail(f"intervalCount {ivc} > MAX_INTERVALS {MAX_INTERVALS}")
        if glc > MAX_GLYPHS:
            ok = fail(f"glyphCount {glc} > MAX_GLYPHS {MAX_GLYPHS}")
        if kL > MAX_KERN_ENTRIES or kR > MAX_KERN_ENTRIES:
            ok = fail(f"kern entries {kL}/{kR} > MAX_KERN_ENTRIES {MAX_KERN_ENTRIES}")
        if advY > 255:
            ok = fail(f"advanceY {advY} overflows uint8")

        # Interval layout validation -- SdCardFont.cpp:626-655
        can_bmp16 = glc <= UINT16_MAX
        expected_off = 0
        prev_last = 0
        bad = 0
        for j in range(ivc):
            first, last, ivoff = struct.unpack_from("<III", blob, dataOff + j * SZ_INTERVAL)
            if first > last:
                ok = fail(f"interval {j}: first > last"); bad += 1; break
            span = last - first + 1
            if (j > 0 and first <= prev_last) or span > glc \
               or ivoff != expected_off or ivoff > glc - span:
                ok = fail(f"interval {j} layout invalid "
                          f"(first=U+{first:04X} last=U+{last:04X} off={ivoff} "
                          f"expected={expected_off})")
                bad += 1
                break
            if first > UINT16_MAX or last > UINT16_MAX or ivoff > UINT16_MAX:
                can_bmp16 = False
            expected_off += span
            prev_last = last
        if bad == 0:
            print(f"    interval layout: OK (sorted, non-overlapping, offsets contiguous)")
        if expected_off != glc and bad == 0:
            ok = fail(f"interval spans total {expected_off} != glyphCount {glc}")

        ram = ivc * (6 if can_bmp16 else 12)
        total_iv_ram += ram
        print(f"    lookup path: {'compact 6-byte BmpInterval16' if can_bmp16 else '12-byte full'}"
              f"  -> resident heap {ram/1024:.2f} KB")

        # File layout -- computeStyleFileOffsets (SdCardFont.cpp:492-500)
        glyphs_off = dataOff + ivc * SZ_INTERVAL
        kl_off = glyphs_off + glc * SZ_GLYPH
        kr_off = kl_off + kL * SZ_KERNENT
        km_off = kr_off + kR * SZ_KERNENT
        lig_off = km_off + kLc * kRc
        bmp_off = lig_off + ligc * SZ_LIG
        if bmp_off > len(blob):
            ok = fail(f"bitmap section starts at {bmp_off}, past EOF {len(blob)}")

        # Every glyph's bitmap must lie inside the file
        max_end = 0
        for g in range(glc):
            w, h, adv, left, top, dlen, doff = struct.unpack_from(
                GLYPH_FMT, blob, glyphs_off + g * SZ_GLYPH)
            max_end = max(max_end, doff + dlen)
        if bmp_off + max_end > len(blob):
            ok = fail(f"glyph bitmap overruns EOF: {bmp_off + max_end} > {len(blob)}")
        else:
            print(f"    bitmap section: {max_end/1024:.0f} KB, ends "
                  f"{len(blob) - (bmp_off + max_end)} B before EOF")
    return ok, total_iv_ram


def lookup_demo(path, samples):
    """Binary-search a few codepoints exactly as findGlobalGlyphIndex does."""
    blob = Path(path).read_bytes()
    sid, ivc, glc, *_rest = struct.unpack_from("<B3xII", blob, HEADER_SIZE)[:3] + (0,)
    dataOff = struct.unpack_from("<I", blob, HEADER_SIZE + 24)[0]
    ivs = [struct.unpack_from("<III", blob, dataOff + j * SZ_INTERVAL) for j in range(ivc)]
    glyphs_off = dataOff + ivc * SZ_INTERVAL
    print(f"\n  glyph lookup spot-check (style 0):")
    for cp, label in samples:
        lo, hi, found = 0, len(ivs) - 1, None
        while lo <= hi:
            mid = (lo + hi) // 2
            f, l, o = ivs[mid]
            if cp < f: hi = mid - 1
            elif cp > l: lo = mid + 1
            else: found = o + (cp - f); break
        if found is None:
            print(f"    U+{cp:04X} {label:22s} NOT COVERED")
        else:
            w, h, adv, left, top, dlen, doff = struct.unpack_from(
                GLYPH_FMT, blob, glyphs_off + found * SZ_GLYPH)
            print(f"    U+{cp:04X} {label:22s} idx={found:5d} {w}x{h}px "
                  f"adv={adv/16:.2f}px bitmap={dlen}B")


if __name__ == "__main__":
    files = sorted(Path(sys.argv[1]).glob("*.cpfont"))
    if not files:
        sys.exit("no .cpfont files found")
    all_ok = True
    for f in files:
        res = validate(f)
        if res is False or (isinstance(res, tuple) and not res[0]):
            all_ok = False
    lookup_demo(files[0], [
        (0x0041, "'A' latin"),
        (0x0065, "'e' latin"),
        (0x00E9, "'e-acute'"),
        (0x2014, "em dash"),
        (0x201C, "left dquote"),
        (0x4E00, "CJK yi"),
        (0x6C34, "CJK shui"),
        (0x9F98, "CJK long"),
        (0x3002, "ideographic stop"),
        (0xFF0C, "fullwidth comma"),
        (0x3105, "bopomofo b"),
        (0xFFFD, "replacement"),
    ])
    print(f"\n{'='*60}\n{'ALL FILES VALID' if all_ok else 'VALIDATION FAILED'}\n{'='*60}")
