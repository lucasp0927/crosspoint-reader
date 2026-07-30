#!/usr/bin/env python3
"""Derive the static faces build-cjk-family.sh feeds to the converter.

Idempotent: skips any output that already exists. Delete the outputs to rebuild.

Latin: instances static Regular/Bold out of the variable files.
  Bitter's wght DEFAULT IS 100 (Thin), so rasterizing the variable file directly
  yields hairlines -- ink for "Hnbo" at 14pt: 34370 Thin vs 114461 Regular.
  Inter defaults to wght 400, so only its opsz needs pinning: opsz 14 is the
  text-optimized end of its 14-32 range (looser tracking, sturdier forms), which
  is what body text on e-ink wants. Both are instanced rather than left variable
  so FreeType cannot silently pick a default we did not intend.
  Amazon Ember already ships as statics, so it needs nothing here.

CJK: builds a "+rescue" composite per style.
  The converter accepts ONE fallback per style, so a CJK font's coverage holes
  have to be patched into the font itself rather than chained at convert time.
  Also points U+FFFD at U+25A1: resolve_intervals() always appends U+FFFD
  (fontconvert_sdcard.py:117) and none of these CJK faces actually has it, so
  without this every unrenderable character would rasterize to a 0x0 blank and
  silently vanish instead of showing a box.
"""
import re
import sys
from pathlib import Path

from fontTools import subset
from fontTools.merge import Merger
from fontTools.ttLib import TTFont
from fontTools.ttLib.scaleUpem import scale_upem
from fontTools.varLib import instancer

F = Path(__file__).resolve().parent
RANGE_FILE = F / "zh-hant-5000+punct.unicode-range.txt"
FALLBACK_BOX = 0x25A1  # WHITE SQUARE — stands in for the missing U+FFFD

VARIABLE_FACES = {
    "Bitter": (F / "Bitter-VariableFont_wght.ttf", {"wght": None}),
    "Inter": (F / "Inter-VariableFont_opsz,wght.ttf", {"opsz": 14, "wght": None}),
}
WEIGHTS = {"Regular": 400, "Bold": 700}

# {cjk key: {style: (source, rescue donor)}}
# jf-openhuninn has a single weight, so both styles share one composite and the
# donor is NotoSansTC-Regular for both — pairing a bold donor with a regular
# base would look inconsistent. IBM Plex TC has real weights, so each style gets
# its own composite with a weight-matched donor.
CJK_FAMILIES = {
    "huninn": {
        "Regular": ("jf-openhuninn-2.1.ttf", "NotoSansTC-Regular.ttf"),
        "Bold": ("jf-openhuninn-2.1.ttf", "NotoSansTC-Regular.ttf"),
    },
    "ibm": {
        "Regular": ("IBMPlexSansTC-Regular.ttf", "NotoSansTC-Regular.ttf"),
        "Bold": ("IBMPlexSansTC-Bold.ttf", "NotoSansTC-Bold.ttf"),
    },
}


def composite_name(source):
    return f"{Path(source).stem}+rescue.ttf"


def requested_codepoints():
    cps = set()
    for tok in (x.strip() for x in RANGE_FILE.read_text().replace("\n", "").split(",") if x.strip()):
        m = re.match(r"^U\+([0-9A-Fa-f]+)(?:-([0-9A-Fa-f]+))?$", tok)
        a = int(m.group(1), 16)
        b = int(m.group(2), 16) if m.group(2) else a
        cps.update(range(a, b + 1))
    return cps


def instantiate(family, label, weight):
    src, axes = VARIABLE_FACES[family]
    out = F / f"{family}-{label}.static.ttf"
    if out.exists():
        print(f"  {out.name}: exists, skipping")
        return
    if not src.exists():
        print(f"  {src.name}: not present, skipping {family}")
        return
    coords = {k: (weight if v is None else v) for k, v in axes.items()}
    f = TTFont(src)
    inst = instancer.instantiateVariableFont(f, coords, inplace=False, updateFontNames=True)
    inst.save(out)
    print(f"  {out.name}: {coords} ({out.stat().st_size/1024:.0f} KB)")


def build_composite(source, donor, req):
    out = F / composite_name(source)
    if out.exists():
        print(f"  {out.name}: exists, skipping")
        return
    if not (F / source).exists():
        print(f"  {source}: not present, skipping")
        return

    base = TTFont(F / source, lazy=True)
    have = set(base.getBestCmap())
    upem = base["head"].unitsPerEm
    base.close()
    gap = sorted(req - have)
    print(f"  {source}: lacks {len(gap)} requested codepoints", end="")

    if gap and (F / donor).exists():
        tmp = F / ".rescue.tmp.ttf"
        # Drop layout tables: only outlines and cmap matter to the converter,
        # and stale GPOS/GSUB referencing dropped glyphs would break the merge.
        subset.main([str(F / donor),
                     "--unicodes=" + ",".join(f"U+{c:04X}" for c in gap),
                     "--output-file=" + str(tmp),
                     "--no-hinting", "--desubroutinize",
                     "--drop-tables+=GPOS,GSUB,GDEF",
                     "--notdef-outline", "--recalc-bounds"])
        r = TTFont(tmp)
        if r["head"].unitsPerEm != upem:
            scale_upem(r, upem)
            r.save(tmp)
        r.close()
        # Base is listed first so it wins any shared codepoint; by construction
        # the subset holds only codepoints it lacks, so there are none.
        merged = Merger().merge([str(F / source), str(tmp)])
        tmp.unlink()
        print(f", rescuing from {donor}")
    else:
        merged = TTFont(F / source)
        print(" (no donor available)" if gap else "")

    for table in merged["cmap"].tables:
        if table.isUnicode() and FALLBACK_BOX in table.cmap and 0xFFFD not in table.cmap:
            table.cmap[0xFFFD] = table.cmap[FALLBACK_BOX]
    merged.save(str(out))

    g = TTFont(out, lazy=True)
    cm = set(g.getBestCmap())
    g.close()
    print(f"  {out.name}: {len(cm)} codepoints (was {len(have)}), "
          f"{out.stat().st_size/1024/1024:.2f} MB | requested coverage "
          f"{len(req & cm)}/{len(req)} ({100*len(req & cm)/len(req):.2f}%), "
          f"{len(req - cm)} in no available font")


if __name__ == "__main__":
    only = sys.argv[1] if len(sys.argv) > 1 else None
    req = requested_codepoints()
    for family in VARIABLE_FACES:
        for label, weight in WEIGHTS.items():
            instantiate(family, label, weight)
    seen = set()
    for key, styles in CJK_FAMILIES.items():
        if only and key != only:
            continue
        for source, donor in styles.values():
            if source in seen:
                continue
            seen.add(source)
            build_composite(source, donor, req)
