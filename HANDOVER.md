# Handover — `lucas_patch` work since 2026-07-30, rebased onto 1.6.0

Written 2026-09-12. Branch: **`lucas_patch-rebase-1.6.0`** (this branch), which is
`lucas_patch` replayed onto the official **1.6.0** release tag (`54337e6d`,
2026-09-05) plus the compatibility fixes that replay needed. The pre-rebase
branch is untouched on `origin/lucas_patch` (head `cb5d34e5`) and locally as
`lucas_patch-backup-pre-1.5`.

Companion document: `CHANGES-LOCAL.md` (the CJK SD-font performance work, with
measurements). This file covers everything after it and the 1.6.0 rebase.

---

## 1. What the branch carries (13 commits over upstream)

All targeting the **Xteink X4 (ESP32-C3, no PSRAM, buttons only)** reading
Traditional Chinese EPUBs with SD-card fonts. Commit order as rebased:

| # | Commit | Date | What |
|---|---|---|---|
| 1 | `perf: cut SD font I/O for CJK reading` | 07-30 | Everything in `CHANGES-LOCAL.md` §1a–1e: cached `.cpfont` handle, overflow ring 8→24, metadata-only advance fetch, **flash-resident 14pt CJK font** (`FlashReaderFont`, `emberplex_14_{regular,bold}.h`), `--sparse-intervals` converter flag, `test/sdcard_font` host tests, `fonts/` build tooling. |
| 2 | `perf: render each grayscale plane in one pass` | 07-30 | §1f: `renderPlaneToBuffer` targets the whole plane once instead of six 80-row bands. gray_render 95→51 ms. |
| 3 | `feat: vertical CJK reading mode` | 07-30 | Top-to-bottom, right-to-left layout. Parser lays out in transposed space; `PageLine`/`TextBlock` map back at render via `GfxRenderer::setVerticalText()`; `drawTextVertical()` renders Han upright, Latin rotated, vertical punctuation forms. New setting `SETTINGS.verticalReading`, `ReaderRenderSpec::verticalMode`. Dictionary word-select is disabled in vertical mode. |
| 4 | `test: add headless page renderer` | 07-30 | `test/headless_render/render_pages`: compiles the real layout engine + `GfxRenderer` natively, renders pages of a real EPUB with a real `.cpfont` to PGM. This is the main verification tool (see §4). |
| 5 | `feat: consolidate CJK typesetting rules; extend kinsoku per clreq` | 07-30 | `lib/Utf8/CjkTypesetting.h` is the single home for 行首/行尾禁則. Adds dashes (incl. U+2500/U+2501 used as em dashes by Taiwanese publishers), ellipses, interpuncts, unit suffixes. Both line breakers (DP and greedy/hyphenating) refuse forbidden boundaries. |
| 6 | `feat: vertical text decorations, sup/sub, and focus split` | 07-30 | Underline/strike/sup/sub and focus-reading bold split in vertical columns. |
| 7 | `chore: add desktop simulator environment` | 07-30 | `[env:simulator]` in `platformio.ini` (SDL2), pulling the official simulator from GitHub. |
| 8 | `fix: place images correctly in vertical CJK mode` | 07-30 | Images size against real screen extents and map to screen space (`PageImage::verticalScreenPos`). Found via the simulator: covers rendered blank. |
| 9 | `fix: add missing <cmath> and <cstdint> includes` | 08-06 | Newer GCC. |
| 10 | `fix: break vertical-mode image pages on the block axis` | 08-06 | Image page-break check compared the block-axis accumulator against the inline span. |
| 11 | `feat: show branch and short SHA on the boot splash` | 08-06 | `CROSSPOINT_BUILD_ID` for non-dev envs; `git_branch.py` truncates long branch names. |
| 12 | `test: render real images in the headless page renderer` | 09-12 | |
| 13 | `docs: document section.bin versions` | 09-12 | `docs/file-formats.md`. |

Measured end state on the X4 (from `CHANGES-LOCAL.md`, pre-rebase): min free heap
24 KB → 122 KB, max loop 4.7 s → 0.4 s, 98% of glyphs served without SD I/O.

---

## 2. The 1.6.0 rebase (2026-09-12)

Old base `e00f5958` (2026-08-07) → new base tag `1.6.0`. 126 upstream commits in
between; `upstream/develop` is a further 18 commits past 1.6.0 (not taken — the
request was the official release).

### 2.1 Upstream changes relevant to the X4 / CJK work

Worth knowing about (all now in this branch):

- **Font I/O for UI text** — `#3026` batch-prewarms SD-fallback glyphs in
  `getTextWidth`/`drawText` (`ensureSdGlyphsResident`), and `#3071` ("Slow CJK
  TOC & Lists") makes `prewarm` accumulative and adds a multi-string prewarm.
  These attack the same problem as `CHANGES-LOCAL.md` §1a/1b from the other
  side; they coexist. Note the hybrid flash font is deliberately **not**
  registered via `registerSdCardFont`, so `ensureSdGlyphsResident` never
  touches it — the flash path is unchanged.
- **`#3126`** keeps the glyph arena usable under heap pressure (arena retry).
- **`#3144`** cut built-in font flash by 323 KB (sparse kerning, Zopfli). This
  changed `EpdFontData` — see §2.3.
- **`#2959`** image decode overflow / viewport clipping; **`#2654`** tables as
  columns; **`#3102`** ruby groups; **`#3221`** paragraph spacing off; all in the
  parser the vertical mode shares. Rendering verified identical (§4).
- **Reader refactor `#3025`** consolidated `EpubReaderActivity`/`TxtReader`/
  `XtcReader` into a `ReaderActivity` base. The vertical-text reset moved there.
- **Image pages**: upstream dropped the "blank image area + double FAST refresh"
  technique in favour of one base refresh. Commit 8's change to that block is
  therefore gone, and so is its vertical-aware `getImageBoundingBox(...,
  &renderer)`: with the blanking gone the function has no caller anywhere
  (reader, headless renderer, tests), so commit 8 now leaves upstream's inline
  `Page::getImageBoundingBox` untouched and keeps only the sizing/placement
  fix (see §2.4).
- **X4 Classic board `#3279`, X4 Pro / PaperMono `#2983`**, USB mass storage,
  touch/frontlight/toolbar features: not applicable to the X4, but they add
  `[env:x4c*]`, `[env:x4pro*]`, `[env:papermono*]` blocks to `platformio.ini`.
- **Power**: `#3191` power-button wake detection, `#2998` GPIO13 guard for C3
  boards, `#3341` SD shutdown on deep sleep. The big light-sleep change `#2525`
  was reverted in `#3077` (battery-drain fix kept).
- **Section cache format** went 35 → 45 upstream (footnote href 96→256, link
  rects for touch, ruby, tables, sup/sub on links…). Ours renumbered on top:
  **v46** verticalMode header byte, **v47** kinsoku, **v48** vertical image
  breaks. `SECTION_FILE_VERSION = 48`. All existing `.crosspoint/` section
  caches will rebuild on first open.
- Language-specific fonts `#3146` is only a change to the download catalog
  (`sd-fonts.yaml`); no code impact on our SD font path.

### 2.2 Conflicts and how they were resolved

| Commit | File | Resolution |
|---|---|---|
| 1 | `.gitignore`, `test/CMakeLists.txt` | Union (kept upstream's new test dirs + `sdcard_font`). |
| 2 | `EpubReaderActivity.cpp` | Comment-only conflict; upstream had deleted the old comment. Code change applied cleanly. |
| 3 | `Section.cpp` | Version renumbered 37 → **46**. |
| 3 | `GfxRenderer.h` | Union (`ensureSdGlyphsResident` + `vtext*` members). |
| 3 | `EpubReaderActivity.cpp` / `ReaderActivity.cpp` | `onExit` no longer exists in the EPUB activity; `renderer.setVerticalText(false)` now lives in `ReaderActivity::onExit()` next to the orientation reset (applies to all reader types, harmless for TXT/XTC). |
| 5 | `ParsedText.cpp` | Greedy breaker's backtrack now combines upstream's `TokenBoundary::allowsBreak()` check with our kinsoku checks. |
| 5, 10 | `Section.cpp` | Versions → **47**, **48**. |
| 7 | `platformio.ini` | `[env:simulator]` appended after upstream's new board envs. |
| 8 | `EpubReaderActivity.cpp` | Took upstream (image-blanking block removed upstream). |
| 11 | `scripts/git_branch.py` | Ours, with upstream's `sticky` env treated as a dev env alongside `default`. |
| 13 | `docs/file-formats.md` | Ours renumbered 46/47/48 above upstream's 36–45 entries; `EXPECTED_VERSION 48`. |

`git range-diff e00f5958..cb5d34e5 1.6.0..lucas_patch-rebase-1.6.0` shows only
the changes listed above plus the §2.4 cleanup.

### 2.3 Compatibility fixes made after the replay

1. **Flash font headers vs. `#3144`** — `EpdFontData` gained seven kerning
   pointers (`kernLeftCodepoints`/`ClassIds`, `kernRightCodepoints`/`ClassIds`
   before `kernMatrix`; `kernRowOffsets`/`kernSparseCols`/`kernSparseValues`
   after). The generated `emberplex_14_{regular,bold}.h` initialisers were
   patched with `nullptr` for all seven. The packed class maps + dense matrix
   form is still a supported representation (SD fonts use it), so behaviour is
   identical. **The source TTFs (Amazon Ember, proprietary) are not on this
   machine, so the headers could not be regenerated with the new
   `fontconvert.py`.** Regenerating would also shrink them (sparse kerning);
   the exact command is in the header comment of `FlashReaderFont.cpp`.
2. **`src/util/DictHtmlPages.cpp`** — upstream's dictionary HTML renderer
   constructs `ChapterHtmlSlimParser` directly; our ctor gained a
   `verticalMode` argument. Passes `false` (definitions are always horizontal).
3. **Headless renderer stubs** — `GfxRenderer.cpp` now includes the SDK's
   `BoardConfig.h` (bezel insets) and calls `HalDisplay::isInverted()` /
   `combinesGrayscaleBase()`. Added `test/headless_render/stubs/BoardConfig.h`
   (X4 default insets 9/3/3/3) and the two stub methods.
4. **`test/sdcard_font/RealBookPageTurnTest.cpp`** — missing `<algorithm>`
   (GCC 15).
5. **`SdCardFontPageTurn.RealisticOverlapRebuildsEverything`** →
   `…RebuildsUnion`: `#3071` made prewarm accumulative, so a 62%-new page turn
   now re-reads old ∪ new glyphs (972 reads) rather than exactly the current
   page (600). The test documents the new upstream behaviour; nothing in our
   code changed here. Worth re-measuring on device — this is more SD I/O per
   page turn for pure-SD fonts (the 14pt flash hybrid is unaffected).

### 2.4 Post-handover cleanup (2026-09-12, on the Mac)

A relevance pass of every commit against 1.6.0 and `upstream/develop`
(`472b5e48`) found all of them still needed except one:

- **Dropped** `chore: pin simulator to local checkout with O_RDWR fix` (was
  commit 9). The official simulator's `main` has carried the same fix since
  `9b1bece` (`HalStorage::openFileForWrite` opens `O_RDWR`), so
  `[env:simulator]` is back on commit 7's
  `simulator=https://github.com/crosspoint-reader/crosspoint-simulator`. The
  side-by-side `../crosspoint-simulator` checkout is no longer needed.
- **Trimmed commit 8**: the renderer-aware `Page::getImageBoundingBox`
  overload was dead (no callers anywhere — the earlier version of this file
  wrongly said the headless renderer used it). The header is back to
  upstream's inline version; `PageImage::verticalScreenPos` and the parser
  sizing fix stay.
- **Commit 9's `<cstdint>`** in `FsHelpers.cpp` is still required on the
  1.6.0 base. Upstream added the same include later (`#3353`, on `develop`),
  so it collapses into upstream on the next `develop` rebase.

---

## 3. Verification status

| Check | Result |
|---|---|
| `pio run -e default` (ESP32-C3) | **Builds clean**, no warnings in `lib/`/`src/`. RAM 56,660 B static (17.3%), flash 92.1% (6,036,487 B). Pre-rebase was 52,572 B / 92.7%; upstream's font shrink offsets our flash font. Re-verified after §2.4. |
| Headless render, 迷霧之子 首部曲 (spines 0, 2, 3, 12, 14, 25; pages 0–3; horizontal **and** vertical; AmazonEmberTC 14pt) | **All 34 pages pixel-identical** to the pre-rebase branch; page counts identical (e.g. spine 12: 55 horizontal / 65 vertical). Zero `[ERR]` lines. |
| Host suite `ctest` | 192/193 pass. The one not run is `ChapterHtmlSlimParserTest`, an upstream test that needs `expat.h` on the host (`libexpat1-dev`), which this box lacks. |
| Device test | **Not done.** Nothing here has been flashed. |
| Simulator | **Resolves, does not compile on macOS.** After §2.4 `pio run -e simulator` installs `simulator@1.0.0+sha.047527f` from GitHub, but compilation stops in upstream firmware code (`ActivityManager.o`, `HomeActivity.o`): Apple clang 21's libc++ rejects `std::vector<RecentBook>` while `RecentBook` is only forward-declared (`HomeActivity.h:9`/`:30`). libstdc++ tolerates it. Neither file is touched by this branch; not chased. |

Not verified and worth a device pass: page turn timings from `CHANGES-LOCAL.md`
(gray_render, min-free-heap), UI list scrolling with the new upstream
batch-prewarm, and whether upstream's accumulative prewarm changes heap
headroom on the 讓愛延續的七個方法 chapter-3 OOM (still an open upstream bug, see
`CHANGES-LOCAL.md` §4).

---

## 4. How to reproduce the verification

```bash
# Firmware
pio run -e default

# Host tests + headless renderer (needs .pio/libdeps/default from a pio run)
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test -j8 && ctest --test-dir build/test -j8

# Render Mistborn pages (copy the .cpfont files somewhere without macOS "._" siblings first)
build/test/headless_render/render_pages \
  --epub "…/koreader/Mistborn/迷霧之子 首部曲：最後帝國.epub" \
  --fonts …/fonts/AmazonEmberTC --family AmazonEmberTC --size 14 \
  --spine 12 --pages 0-3 --out out/v --sdroot out/sd --vertical
```

The books and fonts used live in `~/Development/xteink_sd/` (SD card copy) on
this machine.

---

## 5. Open items / next steps

1. **Flash on the X4 and read a few chapters** in both modes; delete
   `.crosspoint/` first (v48 invalidates all section caches anyway).
2. **Regenerate `emberplex_14_*.h`** with the 1.6.0 `fontconvert.py` on the
   machine that has the Ember/Plex TTFs, to pick up sparse kerning (smaller
   flash) and drop the hand-patched initialisers.
3. **Decide what to do with `lucas_patch`**: this branch is the rebased
   replacement. `git branch -f lucas_patch lucas_patch-rebase-1.6.0 && git push
   -f origin lucas_patch` once you are happy, or keep both.
4. **Upstream/develop** has 18 more commits past 1.6.0; a follow-up rebase
   should be small — a trial merge conflicts only in `test/CMakeLists.txt` and
   `.gitignore` (both keep-both). Upstream issue `#3475` (2026-09-09) asks for
   vertical writing mode; commits 3/6/8/10 are a candidate answer.
5. Consider upstreaming the kinsoku/`CjkTypesetting.h` work and the headless
   renderer — both are self-contained and upstream now ships its own
   `chapter_html_slim_parser` host tests that would benefit.
6. `SdCardFont.h` still includes `<HalStorage.h>` (header-level dependency of
   `lib/EpdFont` on `lib/hal`), noted in `CHANGES-LOCAL.md` §1d as a
   before-upstreaming cleanup.
