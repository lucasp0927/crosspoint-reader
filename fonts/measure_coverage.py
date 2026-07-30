#!/usr/bin/env python3
"""Measure flash-font CJK coverage against Lucas's real library.

Answers: of all CJK characters actually rendered while reading these books, what
fraction is served from flash (free) vs the SD overflow ring (a read)?  And how
much better could a re-picked top-N be?
"""
import collections
import glob
import html
import io
import re
import sys
import zipfile

HEADER = "/Users/lucaspeng/Development/crosspoint-reader/lib/EpdFont/builtinFonts/emberplex_14_regular.h"
BOOKDIR = "/Users/lucaspeng/Desktop/book"

CJK = re.compile(r"[㐀-䶿一-鿿豈-﫿]")
TAG = re.compile(r"<[^>]+>")
SCRIPTSTYLE = re.compile(r"<(script|style)\b.*?</\1>", re.S | re.I)


def flash_codepoints():
    """Covered set, read off the recorded fontconvert command in the header."""
    with open(HEADER, encoding="utf-8", errors="replace") as f:
        for line in f:
            if "--additional-intervals" in line:
                cps = set()
                for lo, hi in re.findall(r"--additional-intervals (0x[0-9a-fA-F]+),(0x[0-9a-fA-F]+)", line):
                    cps.update(range(int(lo, 16), int(hi, 16) + 1))
                return cps
    raise SystemExit("no --additional-intervals line found in header")


def book_text(path):
    out = []
    try:
        with zipfile.ZipFile(path) as z:
            for name in z.namelist():
                if not re.search(r"\.(x?html?|xml)$", name, re.I):
                    continue
                if "toc" in name.lower() or "nav" in name.lower():
                    continue
                try:
                    raw = z.read(name).decode("utf-8", "replace")
                except Exception:
                    continue
                raw = SCRIPTSTYLE.sub(" ", raw)
                out.append(html.unescape(TAG.sub(" ", raw)))
    except zipfile.BadZipFile:
        return ""
    return "".join(out)


def main():
    flash = flash_codepoints()
    print(f"flash font covers {len(flash)} codepoints "
          f"({sum(1 for c in flash if 0x4e00 <= c <= 0x9fff)} in CJK unified)\n")

    books = sorted(glob.glob(f"{BOOKDIR}/**/*.epub", recursive=True))
    library = collections.Counter()
    rows = []

    for path in books:
        text = book_text(path)
        freq = collections.Counter(CJK.findall(text))
        total = sum(freq.values())
        if total < 5000:          # English books: not enough CJK to be meaningful
            continue
        library.update(freq)
        hit = sum(n for ch, n in freq.items() if ord(ch) in flash)
        # Best possible top-1500 chosen for THIS book alone.
        own = sum(n for _, n in freq.most_common(1500))
        rows.append((path.split("/")[-1], total, len(freq), hit / total, own / total))

    print(f"{'book':<34} {'CJK chars':>10} {'distinct':>8} {'flash%':>7} {'own1500%':>9}")
    print("-" * 72)
    for name, total, distinct, cov, own in rows:
        print(f"{name[:33]:<34} {total:>10,} {distinct:>8,} {cov:>6.2%} {own:>9.2%}")

    ltotal = sum(library.values())
    lhit = sum(n for ch, n in library.items() if ord(ch) in flash)
    print("-" * 72)
    print(f"{'LIBRARY (CJK books)':<34} {ltotal:>10,} {len(library):>8,} {lhit/ltotal:>6.2%}")

    # How good could a library-wide re-pick be, at several sizes?
    print("\nBest-possible coverage if top-N were re-picked from these books:")
    cum = 0
    ranked = library.most_common()
    marks = {500: None, 1000: None, 1500: None, 2000: None, 2500: None, 3000: None}
    for i, (_, n) in enumerate(ranked, 1):
        cum += n
        if i in marks:
            marks[i] = cum / ltotal
    for n in sorted(marks):
        if marks[n] is not None:
            print(f"  top-{n:<5} {marks[n]:>7.2%}")

    # Which characters does the current font miss most often?
    misses = [(ch, n) for ch, n in library.most_common() if ord(ch) not in flash]
    missed_total = sum(n for _, n in misses)
    print(f"\nmissed occurrences: {missed_total:,} of {ltotal:,} ({missed_total/ltotal:.2%}) "
          f"across {len(misses):,} distinct chars")
    print("top 40 most-missed:", "".join(ch for ch, _ in misses[:40]))
    top100 = sum(n for _, n in misses[:100])
    print(f"adding just the top 100 missed chars would recover "
          f"{top100/missed_total:.1%} of the misses ({top100/ltotal:.2%} of all CJK)")


main()
