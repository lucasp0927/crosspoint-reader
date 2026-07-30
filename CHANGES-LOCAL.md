# Local changes — CJK reading performance

Personal fork work, not upstreamed. Base commit: **`2ceeeccd`** (develop, 2026-07-29).
Rebased from `f39d35d7` with zero conflicts — none of the 10 upstream commits touch
any of the six locally-modified files. Host suite 148/148, firmware builds clean
(static RAM 52,572 B, flash 92.7%). See "Rebase log" at the bottom.

Everything here targets one problem: reading Traditional Chinese EPUBs with an
SD-card font was slow, and the cause was SD I/O, not RAM. Measured end state on
an Xteink X4 reading 迷霧之子 首部曲:

| | before | after |
|---|---|---|
| Min free heap | 24,344 B | 121,848 B |
| Max loop duration | 4,711 ms | 392 ms |
| Glyphs served without SD I/O | 0% | 98.2% |
| SD reads per 300 pages | ~16,300 | 2,682 |
| Flash used (app0) | 84.1% | 92.7% |
| Static RAM | 52,020 B | 52,396 B |

---

## 1. Firmware changes (7 modified files, 4 new)

### 1a. `SdCardFont` — cache the `.cpfont` file handle
**Files:** `lib/EpdFont/SdCardFont.{h,cpp}`

`onGlyphMiss()` called `Storage.openFileForRead()` for **every glyph**. UI text is
never prewarmed (no activity outside reader/dictionary opens a `PrewarmScope`),
so a book list paid one SD file open per CJK character — twice over, because
`getTextWidth()` and `drawText()` each walk the string.

Added `overflowFile_` / `ensureOverflowFile()` / `closeOverflowFile()`. The handle
is opened lazily, reused across misses, dropped in `freeAll()`, and closed on any
I/O error so a failure can't leave a bad handle.

*Measured: book-list redraw 160 file opens → 1.*

**Rebase risk:** low. Self-contained in `onGlyphMiss` plus three new members.

### 1b. `OVERFLOW_CAPACITY` 8 → 24
**File:** `lib/EpdFont/SdCardFont.h`

Not for the measure/draw pair — for `GfxRenderer.cpp:1606`, which truncates
over-long list rows by re-measuring a shrinking string in a loop (O(n²) lookups).
At capacity 8 a 20-character title cost 312 reads for 20 distinct glyphs.

24 is the knee of the curve, measured: a 20-char title costs 40 reads at 24 or 32
(the ideal), but 180 at 16. Costs ~8.6 KB if all three UI fallback instances fill
their rings.

**Rebase risk:** trivial, one constant.

### 1c. Metadata-only advance fetch
**Files:** `lib/EpdFont/SdCardFont.{h,cpp}`, `lib/GfxRenderer/GfxRenderer.cpp`

`ADVANCE_CACHE_LIMIT` is 768 entries; a Traditional Chinese novel uses ~3,100
distinct characters, so ~20% of measured characters miss. Each miss fell through
`GfxRenderer.cpp:1898` to `getGlyph()`, which reads the **bitmap** too and burns
an overflow slot — to obtain 2 bytes of `advanceX`.

Added `SdCardFont::fetchAdvanceFromSd()`: reads the 16-byte glyph header only,
via the cached handle, touching neither bitmaps nor the ring. `GfxRenderer` calls
it before falling back to `getGlyph()` (kept for genuinely uncovered codepoints so
replacement-glyph behaviour is unchanged).

*Measured: laying out 300 pages, 1,945 KB → 224 KB of SD reads (8.7×).*

**Rebase risk:** low, but the `GfxRenderer.cpp:1898` call site may move.

### 1d. Flash-resident CJK font (the big one)
**New:** `lib/EpdFont/FlashReaderFont.{h,cpp}`,
`lib/EpdFont/builtinFonts/emberplex_14_{regular,bold}.h`
**Modified:** `lib/EpdFont/SdCardFontManager.cpp`, `SdCardFont.{h,cpp}`

Glyphs compiled into flash cost **neither RAM nor SD I/O** — bitmaps, glyph table
and intervals are `static const`, so they are memory-mapped and read at cache
speed. The device had ~1 MB of unused app0 space while making millions of SD
reads to save kilobytes of RAM.

Compiled in at 14pt (uncompressed 2-bit — a compressed font would need a DRAM
decompression buffer, defeating the point):
* **regular**: top 1,500 CJK + full Latin — 372 KB
* **bold**: top 500 CJK + full Latin — 174 KB

No renderer changes were needed; the architecture already supported this:
* `EpdFont::getGlyph()` falls through to `glyphMissHandler` on a miss
* `EpdFont::hasCodepoint()` unions its intervals with `coverageHandler`
* `GfxRenderer::getGlyphBitmap()` routes overflow glyphs to the SD buffer and
  everything else to `fontData->bitmap`

Two additions were required:
* `SdCardFont::attachAsGlyphTail()` — builtin `EpdFontData` is `static const`, so
  the miss hooks can't be written into it. `FlashReaderFont` keeps a mutable
  ~80-byte header copy per style pointing at the same flash arrays.
* `SdCardFontManager::loadFile()` — binds the flash font for styles it covers when
  `file.pointSize == FlashReaderFont::pointSize()`.

**Deliberate:** a hybrid font is **not** registered via `registerSdCardFont()`.
Registration would route both `prewarm` and the measurement fast path
(`GfxRenderer.cpp:89`) through the SD font, bypassing the flash glyph table and
re-paying the SD reads this exists to avoid. A side effect is that the ~55–62 KB
per-page mini bitmap arena is never allocated, which is where most of the heap
improvement came from.

**Rebase risk: highest.** Touches font selection. `FlashReaderFont::reset()` must
be called from `unloadAll()` before the `SdCardFont`s it points at are deleted.

**Caveats**
* Only applies at **14pt**; any other size falls back to the pure SD path.
* Italic has no flash backing.
* `SdCardFont.h` now includes `<HalStorage.h>`, so `lib/EpdFont` depends on
  `lib/hal` at header level (the `.cpp` already did). Fix before upstreaming —
  forward-declare `HalFile` behind a `unique_ptr` with an out-of-line destructor.

### 1f. Single-band plane render (grayscale AA latency)
**File:** `src/activities/reader/EpubReaderActivity.cpp`

`renderPlaneToBuffer()` sliced its render into `STRIP_ROWS`-tall (80) bands even
though its buffer already holds the **whole** 48 KB plane — so the page was walked
6 times per plane, 12 times per page. `glyphIntersectsStrip()` culled the bitmap
*decode* for out-of-band glyphs, but everything above it ran in full every pass:
TextBlock traversal plus `drawText`'s per-word measurement and shaping.

`beginStripTarget`'s assert permits a full-height band (`stripY0=0, stripRows=gh`
→ `0 <= gh - gh`), so the plane is now targeted in one pass. The strip-scratch
fallback tier still slices — there the buffer really is one band.

*Measured, 迷霧之子 spine 14, 16 FAST_REFRESH turns before/after:*

| | before | after |
|---|---|---|
| `gray_render` (LSB, hidden) | 95 ms | **51 ms** |
| `gray_write` (incl. un-hidden MSB render) | 141 ms | **96 ms** |
| `wait` | 409 ms | 454 ms |
| total | 832 ms | **790 ms** |
| BW→AA gap | 204 ms | **159 ms** |
| min free | 64,240 | 70,940 |

`wait` rising by the same amount `gray_render` fell is the confirmation: panel
refresh is a **constant ~505 ms** (51+454 vs 95+409), so this only converts busy
time into wait time — which is the point, since the wait is already overlapped.
Six walks cost 95 ms against one walk's 51 ms, so the five culled walks were
~44 ms (~9 ms each) of pure traversal overhead.

**Does not change time-to-first-pass** (~610 ms): everything here happens after
the BW refresh is issued. It only makes the AA arrive sooner.

**Rebase risk:** low, but it lives in upstream's async grayscale tier — re-check
if `renderPlaneToBuffer` or `STRIP_ROWS` is touched upstream.

### 1e. `--sparse-intervals` (build tooling)
**File:** `lib/EpdFont/scripts/fontconvert_sdcard.py`

The interval table is the only part of a `.cpfont` resident in RAM. Default
behaviour splits intervals around undrawable codepoints, which for a scattered
CJK subset yields thousands of intervals (tens of KB). `--sparse-intervals` keeps
intervals verbatim and lets undrawable codepoints become zero-size glyphs (16 B
on SD, nothing resident), collapsing the table ~100×.

*Verified: with the flag absent, output is **byte-identical** to the pre-edit
converter.*

**Rebase risk:** low, additive and default-off.

---

## 2. Host test harness (new)

`test/sdcard_font/` — 19 gtest cases (12 + 4 + 3), wired into `test/CMakeLists.txt`.
Full suite is 148 tests and passes.

`SdCardFont.cpp` compiles on macOS with two stubs (`stubs/Logging.h`, and a
`stubs/HalStorage.h` backed by `fopen` with operation counters plus a scripted
`ESP.getFreeHeap()`). This is what made the whole investigation possible: the cost
of serving a glyph is an **operation count**, which is deterministic and testable
on the host — no hardware, no emulator.

* `SdCardFontIoTest.cpp` — synthetic fixture, pins mechanism (file opens per
  glyph, ring capacity behaviour, ellipsis-loop cost)
* `RealBookPageTurnTest.cpp` — opt-in, replays real pages through a real `.cpfont`
* `FlashHybridTest.cpp` — opt-in, verifies flash/SD split and bold binding

Opt-in tests take `CPFONT_PATH` and `PAGE_TEXT_FILE` and skip cleanly without them.

```
cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test
```

**Note:** `gtest_discover_tests` runs each TEST as its own process, so the fixture
is written pid-uniquely and `rename()`d into place. Without that, `ctest -j` races.

---

## 3. Font build tooling (new, `fonts/`)

Not part of the firmware; regenerates the `.cpfont` and flash headers.

| file | purpose |
|---|---|
| `build-cjk-family.sh` | `./build-cjk-family.sh <Ember\|Bitter\|Inter> <huninn\|ibm>` |
| `prepare_faces.py` | instances variable fonts; builds `+rescue` CJK composites |
| `gen_intervals.py` | builds the `--intervals` argument; subset/merge policy |
| `validate_cpfont.py` | mirrors every check in `SdCardFont::load()` |

Traps these encode, all found the hard way:
* **Bitter's variable default is Thin** (wght 100) — rasterizing the variable file
  directly gives hairlines. Inter defaults to Regular but needs `opsz` pinned to 14.
* **Google ships `Inter-VariableFont_opsz,wght.ttf`** — the comma breaks
  comma-separated font-list parsing.
* **No CJK face here has U+FFFD**, so it is remapped to U+25A1; otherwise every
  unrenderable character silently vanishes.
* **`fontconvert.py` optionals must precede positionals** (`nargs='+'` greediness),
  and **zsh does not word-split unquoted `$VAR`** — use `${=IV}`.
* `build-sd-fonts.py` hardcodes NotoSans-Regular as fallback with no YAML
  override, which is why these call the converter directly.

Current font: **EmberPlexMin** (Amazon Ember + IBM Plex Sans TC), sizes 8/10/12/14/16/18,
`LATIN_SUBSET=minimal`, sparse gap 64 → 24 intervals, 0.28 KB resident per file.
Sizes 8/10/12 exist because `SdCardFontSystem.cpp:31` requires **exact** point-size
matches for the UI CJK fallback.

`log/capture.py` — serial capture. `pio device monitor` cannot be redirected; it
calls `termios.tcgetattr` on stdout and dies when that is not a TTY.

**Licensing:** Amazon Ember is proprietary (vendor `DAMA`). Do not commit or
publish the built fonts or the generated flash headers, which embed Ember outlines.
Bitter, Inter, IBM Plex, Noto and jf-openhuninn are all SIL OFL 1.1.

---

## 4. Known-unfixed

**Chapter 3 of 讓愛延續的七個方法 crashes** — `abort()` from `operator new` in
`ParsedText::addWord` → `ensureTokenCapacity` → `words.reserve()`
(`ParsedText.cpp:306`). CJK tokenises per character into `std::string`, so ~8,250
tokens → capacity 16,384 → 384 KB for the `words` vector alone against 272 KB of
total heap. **Upstream bug, not caused by anything here** — `lib/Epub/` is
unmodified, and this crashes on stock 1.5.0 too.

**UI CJK text is still unprewarmed.** No activity outside reader/dictionary opens
a `PrewarmScope`, so list rows page glyphs through the overflow ring. The handle
cache and larger ring reduced the cost; they did not remove it. Worth an upstream
issue.

---

## Rebase order

Independent and safe first: **1a → 1b → 1c → 1e → 1f**. Each is small, self-contained,
and covered by the host tests.

**1d last** — it is the one that touches font selection, and the one to re-verify
against a new `SdCardFontManager`. Regenerate the flash headers if the reader's
point size ever changes; the exact `fontconvert.py` command is recorded in
`FlashReaderFont.cpp`'s header comment.

---

## Rebase log

### `f39d35d7` → `2ceeeccd` (2026-07-29, 10 commits)

Clean fast-forward, no manual conflict resolution. Upstream touched `lib/Epub/`,
`lib/hal/HalGPIO.cpp`, `lib/KOReaderSync/`, `src/activities/reader/`,
`platformio.ini` and the `freeink-sdk` submodule — **no overlap** with
`lib/EpdFont/` or `lib/GfxRenderer/`, so 1a–1e carried over untouched and the
local diff came out byte-identical (437 lines).

Relevant to the CJK work here:

* **`52ff942a` / `fe37bf80` (ruby allocation removal)** — the win this fork
  benefits from most. `TextBlock::deserialize` used to materialize a
  `wordCount`-sized `std::vector<std::string>` per line even with no ruby.
  CJK tokenises per character, so a ~30-character line paid ~720 B of
  `std::string` headers plus a heap block, for every resident line. Now an
  all-empty ruby vector is never allocated. Complements 1d: 1d cut SD I/O and
  the bitmap arena, this cuts per-page DRAM in `lib/Epub/`, which is untouched
  by anything here.
* **`b508f75e` (`<br>` as paragraph separator)** — a `<br>` after text now
  strips container top/bottom margins, so `<br>`-per-paragraph books (common
  Chinese web-novel formatting) stop re-adding paragraph spacing at every line.
  More text per page.
* **`9163149a` (CJK word spaces)** — gap suppression narrowed to tokens glued in
  the source, fixing collapsed spaces between Hangul words. **No behaviour change
  for Chinese**: per-character CJK tokens are already glued
  (`attachToPrevious == true`), so they still take the gap-less path.
* **`SECTION_FILE_VERSION` 33 → 34** — v34 is binary-identical to v33; the bump
  exists because v33 word positions no longer match. **Every cached section
  re-lays-out on first open.** That re-layout is the SD-font-heavy path 1c and 1d
  target, so it should be fast here, but it is not free.

Not CJK-related but worth knowing: `57134320` moves X3/X4 I2C device detection
into the SDK (**submodule bump to `e514a868` is required**, else the gyro/battery
latch fix is missing), `5965dc67` drops duplicate wolfSSL defines that were
emitting a macro-redefinition warning per library TU, and `67e608e8` adds Text
Settings to the reader menu — see the caveat in 1d: changing size away from 14pt
in-reader silently drops to the pure SD path.

Still unverified on device after this rebase: everything in §4, plus whether the
ruby change measurably moves min-free-heap on a real book. The page-turn baseline
below is measured; **1f was added on top of it** and re-measured (see 1f).

### Measured page-turn baseline on `2ceeeccd` (2026-07-29)

11 consecutive page turns, 迷霧之子 spine 14, EmberPlexMin 14pt, Text AA on.
Steady state is remarkably consistent at ~832 ms:

```
prewarm=2ms  bw_render=77ms  display=23ms  gray_render=95ms
wait=409ms   gray_write=141ms  gray_display=63ms  cleanup=22ms
total=832ms  (tiled async, planes buffered: 1)
```

Heap at page-render time: Free 133,432 / MaxAlloc 90,100 / **Min Free 64,240**.

Reconstructed timeline — first pass lands at ~605 ms, AA ~205 ms after it:

| t | event |
|---|---|
| 2 ms | prewarm done (the flash font's whole contribution: **2 ms**) |
| 77 ms | `bw_render` done — SD glyph misses are paid *here* |
| 100 ms | BW refresh issued async; panel takes ~504 ms |
| 100–195 ms | LSB plane rendered into buffer, hidden under the refresh |
| 605 ms | **first pass visible** |
| 605–746 ms | `gray_write` — write LSB, render MSB (~95 ms, *not* hidden), write MSB |
| 809 ms | **AA visible** |

Roughly 495 ms of the 832 ms is panel physics (`wait` + `gray_display` +
`display`) and is not reachable from software.

**SD glyph misses cost ~6 ms each**, measured by correlating overflow-load count
against `bw_render` across the 11 turns: 0 loads → 69/76/77/78 ms, 2 → 80/88/91,
3 → 96, 4 → 101. Observed rate is only **3.2 loads/page**, far below the ~16/page
the 2.70% raw miss rate predicts, because the 24-slot ring (1b) absorbs repeats.

**Coverage, measured (`fonts/measure_coverage.py`, new):** the flash set covers
**97.30%** of CJK occurrences across the 6 CJK books (1.34 M characters). A
best-possible re-picked top-1500 gives 97.42% — **+0.12 pp, i.e. re-picking is
pointless**, the set is already derived from these books. Enlarging would help
coverage (top-2000 → 99.03%, top-2500 → 99.66%) at ~180 B per marginal CJK glyph
(+88 KB for 500), but at 3.2 loads/page × 6 ms it buys **~12 ms of a 605 ms
time-to-first-pass**. Not taken.

Note `FlashReaderFont.cpp:30`'s "~97.9%" is not library-wide coverage; it matches
迷霧之子 首部曲's *own* best-possible-1500 (97.92%). Measured actual is 97.30%.

**Two dead ends, recorded so they are not re-explored:**
* *Precomputing the AA planes into the flash font.* The font is already 2-bit
  anti-aliased; the grayscale step is not computing AA, it is thresholding that
  2-bit value three ways (`GfxRenderer.cpp:448-458`) because the panel needs
  1-bit planes. Storing three 1-bit planes instead costs +50% bitmap (~160 KB)
  and the `!is2Bit` branch at :462 is *also* a per-pixel `drawPixel` loop, so it
  would not even reach a faster path. The decode is ~12 ms of 832 ms.
* *A single combined refresh, or an extra one, to smooth the BW→AA transition.*
  Not available: `HalDisplay.h:76-80` `displayGrayscaleBase()` displays the BW
  frame as the **base frame for a grayscale overlay** — BW-first is how the panel
  reaches 4 levels, not an implementation choice. `preconditionGrayscale()`
  (`HalDisplay.h:69-73`) is the OEM's "AA-pre-BW(mid) settle pass" hook for
  exactly this, and is **a no-op on X4**.
