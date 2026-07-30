#!/usr/bin/env python3
"""Build the --intervals argument for a Latin-primary + CJK-fallback .cpfont.

The interval table is the only part of a .cpfont that stays resident in RAM:
SdCardFont::load() allocates it per style (SdCardFont.cpp:665) while glyph
metadata and bitmaps page in from SD. On a 380KB device that table is the whole
RAM budget of a font, so this script exists to make it small.

Two merge modes:

  strict  Merge across a gap only when every codepoint in it is drawable by the
          primary or the fallback. Anything else would be re-split by the
          converter's validation pass (fontconvert_sdcard.py:605), so we'd pay
          filler glyph bitmaps for zero RAM saving. Interval count bottoms out
          at whatever the fallback's cmap holes allow.

  sparse  Merge across any gap up to --gap. Requires the converter's
          --sparse-intervals flag: undrawable codepoints become zero-size glyphs
          (16B of on-SD metadata, no bitmap, nothing resident) instead of
          splitting the interval. Collapses the table by ~100x and makes the
          output files SMALLER, because the filler is blank rather than
          rasterized. Cost: a codepoint inside a merged range that no font can
          draw renders as nothing rather than as the replacement glyph.

Usage: gen_intervals.py --primary A.ttf[,B.ttf] --fallback C.ttf[,D.ttf]
                        [--mode sparse|strict] [--gap N] --out FILE
"""
import argparse
import re
import sys
from pathlib import Path

from fontTools.ttLib import TTFont

FONTS = Path(__file__).resolve().parent
RANGE_FILE = FONTS / "zh-hant-5000+punct.unicode-range.txt"

# --latin-subset reading: the blocks a Latin+CJK reading font actually needs.
#
# This is a RAM decision, not a size one. Kerning is extracted from the primary
# font filtered to the codepoints in the intervals (fontconvert_sdcard.py:755),
# and the resulting kern class tables ARE resident once loaded
# (SdCardFont.cpp:245). Carrying a big Latin cmap therefore costs permanent
# heap: Inter's full 2849 codepoints cost 9.91 KB/style against 3.42 KB for
# these blocks -- 13 KB across two styles, on a 380KB device.
#
# Kept: Latin through Ext-B, the combining diacriticals the renderer has
# explicit support for (GfxRenderer combiningMark::), Greek, and the punctuation
# / currency / symbol blocks a book actually uses.
# Dropped: Cyrillic, IPA, Vietnamese (Latin Ext-Additional), other scripts.
# Codepoints in the CJK range file are always kept regardless of this setting.
# Blocks real Traditional Chinese ebooks use that the CJK range file omits.
# Added after auditing two books: 迷霧之子 首部曲 (392k chars) and Rewire (138k).
# U+2500 alone accounted for 1307 occurrences across the two — publishers use the
# box-drawing horizontal as a scene-break rule, and nothing in the top-5000 list
# or the Latin blocks below covers it.
#
# These are ADDITIVE, unlike READING_BLOCKS/MINIMAL_BLOCKS which only filter the
# primary font's cmap down. That distinction matters: the primary here is Amazon
# Ember, which has none of these; they come from the CJK fallback. Filtering
# could never reach them, so they are unioned into `want` and intersected with
# what some font can actually draw, which also keeps them from becoming blank
# sparse slots.
BOOK_BLOCKS = [
    (0x2150, 0x218F),  # number forms — Ⅰ Ⅱ Ⅲ Ⅴ chapter numerals
    (0x2500, 0x257F),  # box drawing — ─ scene-break rules, ╱ ╲
    (0xFE30, 0xFE6F),  # CJK compatibility forms — ︰ ﹕ ﹗ vertical/small punctuation
]

READING_BLOCKS = [
    (0x0000, 0x024F),  # ASCII, Latin-1, Latin Ext-A, Latin Ext-B
    (0x0300, 0x036F),  # combining diacriticals
    (0x0370, 0x03FF),  # Greek
    (0x2000, 0x206F),  # general punctuation
    (0x20A0, 0x20CF),  # currency
    (0x2100, 0x214F),  # letterlike symbols
    (0x2190, 0x21FF),  # arrows
    (0x2200, 0x22FF),  # math operators
    (0x25A0, 0x25FF),  # geometric shapes (incl. U+25A1, our U+FFFD stand-in)
]

# --latin-subset minimal: the floor for an English + Chinese reader.
#
# Same motive as READING_BLOCKS, pushed further: on Ember this is 4.03 -> 1.76 KB
# of resident kern tables across two styles. That matters more than it looks,
# because the UI CJK fallback holds THREE extra font instances resident (8/10/12
# pt, SdCardFontSystem.cpp:31) on top of the reader size, so the saving is paid
# out four times over. It also buys back headroom for the KOReader sync TLS
# handshake, which needs 35KB free and a 20KB contiguous block
# (KOReaderSyncClient.cpp:46).
#
# Keeps ASCII and all of Latin-1, so the accents English actually borrows
# (cafe/naive/Zurich) still render. Drops Latin Ext-A/B, combining diacriticals,
# Greek, arrows, math and letterlike symbols.
MINIMAL_BLOCKS = [
    (0x0000, 0x00FF),  # ASCII + Latin-1 Supplement
    (0x2000, 0x206F),  # general punctuation (quotes, dashes, ellipsis)
    (0x25A1, 0x25A1),  # WHITE SQUARE — the U+FFFD stand-in
    (0x0100, 0x017F),  # Latin Ext-A — č ū ė in transliterated names
    (0x0370, 0x03FF),  # Greek — α θ in technical prose
]

SUBSETS = {"reading": READING_BLOCKS, "minimal": MINIMAL_BLOCKS}

# Firmware limits: SdCardFont.cpp:586-588 and the compact-interval condition at
# SdCardFont.cpp:660. Exceeding UINT16_MAX glyphs drops the 6-byte
# BmpInterval16 path and doubles resident RAM to 12 bytes per interval.
MAX_INTERVALS = 4096
UINT16_MAX = 0xFFFF


def cmap(path):
    f = TTFont(path, lazy=True)
    s = set(f.getBestCmap().keys())
    f.close()
    return s


def split_fonts(spec):
    """Split a comma-separated font list, tolerating commas inside filenames.

    Google Fonts ships multi-axis variable fonts as e.g.
    "Inter-VariableFont_opsz,wght.ttf", so a naive split produces two paths that
    do not exist. Split, then greedily rejoin fragments until each piece names a
    real file.
    """
    parts = spec.split(",")
    out, buf = [], ""
    for part in parts:
        buf = f"{buf},{part}" if buf else part
        if (FONTS / buf).exists():
            out.append(buf)
            buf = ""
    if buf:
        sys.exit(f"no such font under {FONTS}: {buf}")
    return out


def cmap_all(paths, label):
    """Intersect the cmaps of every style of one role.

    The interval table is shared by all styles in the file, so a codepoint
    present in only one weight would desync them. Intersect, don't union.
    """
    sets = [cmap(p) for p in paths]
    common = set.intersection(*sets)
    for p, s in zip(paths, sets):
        if s != common:
            print(f"  note: {Path(p).name} has {len(s - common)} codepoints the other "
                  f"{label} style(s) lack; excluded to keep styles in sync", file=sys.stderr)
    return common


def parse_range_file(path):
    cps = set()
    raw = path.read_text()
    for tok in (x.strip() for x in raw.replace("\n", "").split(",") if x.strip()):
        m = re.match(r"^U\+([0-9A-Fa-f]+)(?:-([0-9A-Fa-f]+))?$", tok)
        if not m:
            sys.exit(f"malformed token in {path.name}: {tok}")
        a = int(m.group(1), 16)
        b = int(m.group(2), 16) if m.group(2) else a
        cps.update(range(a, b + 1))
    return cps


def merge(want, avail, gap, sparse):
    s = sorted(want)
    out = [[s[0], s[0]]]
    for c in s[1:]:
        hole = range(out[-1][1] + 1, c)
        if len(hole) <= gap and (sparse or all(h in avail for h in hole)):
            out[-1][1] = c
        else:
            out.append([c, c])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--primary", required=True, help="Comma-separated Latin font styles.")
    ap.add_argument("--fallback", required=True, help="Comma-separated CJK fallback styles.")
    ap.add_argument("--mode", choices=["strict", "sparse"], default="strict")
    ap.add_argument("--latin-subset", choices=["full", "reading", "minimal"], default="full",
                    help="'reading' or 'minimal' restrict the primary font to the matching "
                         "block list, which cuts the resident kern class tables (see the "
                         "comments there). Codepoints in the CJK range file are kept either way.")
    ap.add_argument("--gap", type=int, default=8)
    ap.add_argument("--styles", type=int, default=2, help="Style count, for the RAM report.")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    primary = cmap_all([FONTS / p for p in split_fonts(args.primary)], "primary")
    fallback = cmap_all([FONTS / p for p in split_fonts(args.fallback)], "fallback")
    sparse = args.mode == "sparse"

    req = parse_range_file(RANGE_FILE)
    if args.latin_subset != "full":
        blocks = SUBSETS[args.latin_subset]
        keep = {c for lo, hi in blocks for c in range(lo, hi + 1)} | req | {0xFFFD}
        dropped = len(primary - keep)
        primary &= keep
        print(f"  latin-subset={args.latin_subset}: primary trimmed to {len(primary)} "
              f"codepoints ({dropped} dropped)", file=sys.stderr)

    # avail must be computed AFTER the trim: a codepoint dropped from the
    # primary is only still drawable if the fallback has it, and strict-mode
    # merging keys off exactly that.
    avail = primary | fallback
    # U+FFFD is appended unconditionally by resolve_intervals()
    # (fontconvert_sdcard.py:117); include it so our count matches the output.
    want = req | primary | {0xFFFD}
    # Additive, and intersected with avail so an undrawable codepoint in these
    # ranges is left out rather than becoming an invisible blank slot.
    book = {c for lo, hi in BOOK_BLOCKS for c in range(lo, hi + 1)} & avail
    added = len(book - want)
    want |= book
    print(f"  book blocks: +{added} codepoints (box drawing, number forms, CJK compat forms)",
          file=sys.stderr)
    if not sparse:
        # Strict mode cannot express an undrawable codepoint, so drop them up
        # front rather than letting the converter re-split around them.
        want &= avail

    undrawable = sorted(want - avail)
    if undrawable:
        print(f"  {len(undrawable)} requested codepoint(s) no font can draw -> blank: "
              + " ".join(f"U+{c:04X}" for c in undrawable[:12])
              + (" ..." if len(undrawable) > 12 else ""), file=sys.stderr)

    merged = merge(want, avail, args.gap, sparse)
    glyphs = sum(b - a + 1 for a, b in merged)
    arg = ",".join(f"(0x{a:04x}-0x{b:04x})" for a, b in merged)
    Path(args.out).write_text(arg)

    bmp16 = glyphs <= UINT16_MAX and merged[-1][1] <= UINT16_MAX
    per_style = len(merged) * (6 if bmp16 else 12)
    print(f"  mode={args.mode} gap<={args.gap}: {len(merged)} intervals, {glyphs} glyphs "
          f"({glyphs - len(want)} filler)", file=sys.stderr)
    print(f"  resident: {per_style/1024:.2f} KB/style, {per_style*args.styles/1024:.2f} KB "
          f"for {args.styles} styles ({'6B compact' if bmp16 else '12B FULL'} path)", file=sys.stderr)

    if len(merged) > MAX_INTERVALS:
        sys.exit(f"ERROR: {len(merged)} intervals exceeds MAX_INTERVALS ({MAX_INTERVALS}), "
                 f"SdCardFont.cpp:586")
    if not bmp16:
        sys.exit(f"ERROR: {glyphs} glyphs exceeds UINT16_MAX; the compact 6-byte interval "
                 f"path would be lost and resident RAM would double")


if __name__ == "__main__":
    main()
