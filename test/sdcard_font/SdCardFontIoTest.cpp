// Counts the SD operations it takes to serve glyphs from a .cpfont.
//
// Motivation: with an SD CJK font selected, the reader is fast but the book
// list is slow. The reader wraps rendering in a FontCacheManager::PrewarmScope,
// which batch-loads a page's glyphs; no UI activity does. Unprewarmed glyphs go
// through SdCardFont::onGlyphMiss, which opens the .cpfont again for EVERY
// glyph and caches only OVERFLOW_CAPACITY (8) of them.
//
// That cost is not a timing question, it is an operation-count question, so it
// can be pinned down here rather than guessed at on device.

#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "EpdFontData.h"
#include "HalStorage.h"  // stub — see stubs/HalStorage.h
#include "SdCardFont.h"

namespace {

constexpr uint32_t kFirstCp = 0x4E00;
// Enough to hold two realistic CJK pages. MAX_PAGE_GLYPHS is 512.
constexpr uint32_t kGlyphCount = 1024;
constexpr uint8_t kGlyphW = 24, kGlyphH = 24;
// 2 bits per pixel, 4 pixels per byte — matches the packing in
// fontconvert_sdcard.py and GfxRenderer's decode.
constexpr uint16_t kBitmapLen = (kGlyphW * kGlyphH + 3) / 4;

// Build a minimal but structurally valid single-style v4 .cpfont. Uses the real
// EpdGlyph / EpdUnicodeInterval structs so the fixture cannot drift from the
// layout SdCardFont::load() expects (both are static_assert'd there).
std::string writeFixture() {
  const std::string path = std::string(testing::TempDir()) + "sdcardfont_fixture.cpfont";

  std::vector<EpdUnicodeInterval> intervals{{kFirstCp, kFirstCp + kGlyphCount - 1, 0}};
  std::vector<EpdGlyph> glyphs;
  glyphs.reserve(kGlyphCount);
  for (uint32_t i = 0; i < kGlyphCount; i++) {
    EpdGlyph g{};
    g.width = kGlyphW;
    g.height = kGlyphH;
    g.advanceX = kGlyphW << 4;  // 12.4 fixed point
    g.left = 0;
    g.top = kGlyphH;
    g.dataLength = kBitmapLen;
    g.dataOffset = i * kBitmapLen;
    glyphs.push_back(g);
  }
  std::vector<uint8_t> bitmaps(kGlyphCount * kBitmapLen, 0xA5);

  const uint32_t dataOffset = 32 /*header*/ + 32 /*one TOC entry*/;

  std::vector<uint8_t> buf;
  auto put = [&buf](const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    buf.insert(buf.end(), b, b + n);
  };

  // Header: magic, version u16, flags u16, styleCount u8, 19 reserved
  buf.insert(buf.end(), {'C', 'P', 'F', 'O', 'N', 'T', 0, 0});
  const uint16_t version = CPFONT_VERSION, flags = 1 /*2-bit*/;
  put(&version, 2);
  put(&flags, 2);
  buf.push_back(1);           // styleCount
  buf.resize(32, 0);          // reserved

  // Style TOC entry, field order per SdCardFont::load()
  const uint8_t styleId = 0;
  put(&styleId, 1);
  buf.resize(buf.size() + 3, 0);  // pad to u32 alignment
  const uint32_t ivCount = static_cast<uint32_t>(intervals.size());
  const uint32_t glCount = kGlyphCount;
  put(&ivCount, 4);
  put(&glCount, 4);
  const uint8_t advanceY = 30;
  put(&advanceY, 1);
  const int16_t ascender = 24, descender = -6;
  put(&ascender, 2);
  put(&descender, 2);
  const uint16_t kernL = 0, kernR = 0;
  put(&kernL, 2);
  put(&kernR, 2);
  const uint8_t kernLc = 0, kernRc = 0, ligc = 0;
  put(&kernLc, 1);
  put(&kernRc, 1);
  put(&ligc, 1);
  put(&dataOffset, 4);
  buf.resize(32 + 32, 0);  // trailing pad of the TOC entry

  put(intervals.data(), intervals.size() * sizeof(EpdUnicodeInterval));
  put(glyphs.data(), glyphs.size() * sizeof(EpdGlyph));
  put(bitmaps.data(), bitmaps.size());

  // gtest_discover_tests runs every TEST as its own process, so under `ctest -j`
  // several of them build this fixture at once. Write to a pid-unique file and
  // rename into place: rename(2) is atomic, so a concurrent reader sees either
  // the old complete file or the new one, never a half-written one.
  const std::string tmp = path + "." + std::to_string(getpid());
  {
    std::ofstream out(tmp, std::ios::binary);
    out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
  }
  std::rename(tmp.c_str(), path.c_str());
  return path;
}

const std::string& fixturePath() {
  static const std::string p = writeFixture();
  return p;
}

// UTF-8 encode a run of codepoints — prewarm() takes text, not codepoints.
std::string pageText(uint32_t firstCp, int count) {
  std::string s;
  s.reserve(count * 3);
  for (int i = 0; i < count; i++) {
    const uint32_t cp = firstCp + i;
    s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return s;
}

// A page of Chinese: mostly-unique glyphs, unlike Latin where 26 letters recur.
constexpr int kPageChars = 300;

}  // namespace

TEST(SdCardFontIo, FixtureLoads) {
  SdCardFont font;
  ASSERT_TRUE(font.load(fixturePath().c_str()));
  EXPECT_EQ(font.styleCount(), 1);
  EXPECT_TRUE(font.hasStyle(0));
}

// Loading is a one-off cost, paid once per font instance. Recorded so a change
// to load() shows up here rather than being blamed on the render path.
TEST(SdCardFontIo, LoadCost) {
  SdCardFont font;
  sdstub::counters().reset();
  ASSERT_TRUE(font.load(fixturePath().c_str()));
  const auto c = sdstub::counters();
  std::printf("[ load ] opens=%d seeks=%d reads=%d bytes=%zu\n", c.opens, c.seeks, c.reads, c.bytesRead);
  EXPECT_LE(c.opens, 2) << "load() should not reopen the file repeatedly";
}

// The UI path: no prewarm, so every glyph goes through onGlyphMiss. The handle
// is cached, so the whole string costs a single open no matter how long it is.
// Before the cache this was one open per glyph.
TEST(SdCardFontIo, UnprewarmedGlyphsShareOneFileHandle) {
  SdCardFont font;
  ASSERT_TRUE(font.load(fixturePath().c_str()));
  EpdFont* ef = font.getEpdFont(0);
  ASSERT_NE(ef, nullptr);

  constexpr int kTitleLen = 12;
  sdstub::counters().reset();
  for (int i = 0; i < kTitleLen; i++) {
    ASSERT_NE(ef->getGlyph(kFirstCp + i), nullptr);
  }
  const auto c = sdstub::counters();
  std::printf("[ title x1 ] %d glyphs -> opens=%d seeks=%d reads=%d\n", kTitleLen, c.opens, c.seeks, c.reads);

  EXPECT_EQ(c.opens, 1) << "the .cpfont handle must be reused across glyph misses";
  // Two reads per glyph (metadata, then bitmap) is the irreducible part.
  EXPECT_EQ(c.reads, 2 * kTitleLen);
}

// A title that fits inside the overflow ring is fully cached: the second pass
// (drawText after getTextWidth) costs no I/O at all.
TEST(SdCardFontIo, ShortTitleFitsInOverflowRing) {
  SdCardFont font;
  ASSERT_TRUE(font.load(fixturePath().c_str()));
  EpdFont* ef = font.getEpdFont(0);
  ASSERT_NE(ef, nullptr);

  constexpr int kShort = 6;  // < OVERFLOW_CAPACITY (8)
  sdstub::counters().reset();
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < kShort; i++) {
      ASSERT_NE(ef->getGlyph(kFirstCp + i), nullptr);
    }
  }
  const auto c = sdstub::counters();
  std::printf("[ short x2 ] %d glyphs, measure+draw -> opens=%d reads=%d\n", kShort, c.opens, c.reads);

  EXPECT_EQ(c.reads, 2 * kShort) << "second pass should be free within ring capacity";
}

// Measuring and drawing each walk the string (GfxRenderer::getTextWidth ->
// getTextDimensions, then drawText), so a title is faulted in twice. A row that
// fits the ring is read once and the second pass is free. At the old capacity
// of 8 this cost 4 reads per glyph instead of 2.
TEST(SdCardFontIo, TypicalTitleSurvivesMeasureToDraw) {
  SdCardFont font;
  ASSERT_TRUE(font.load(fixturePath().c_str()));
  EpdFont* ef = font.getEpdFont(0);
  ASSERT_NE(ef, nullptr);

  constexpr int kTitleLen = 12;
  static_assert(kTitleLen <= 32, "a typical list row must fit OVERFLOW_CAPACITY");

  sdstub::counters().reset();
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < kTitleLen; i++) {
      ASSERT_NE(ef->getGlyph(kFirstCp + i), nullptr);
    }
  }
  const auto c = sdstub::counters();
  std::printf("[ title x2 ] measure+draw -> opens=%d reads=%d (ideal reads: %d)\n", c.opens, c.reads, 2 * kTitleLen);

  EXPECT_EQ(c.opens, 1);
  EXPECT_EQ(c.reads, 2 * kTitleLen) << "the draw pass must be served from the ring";
}

// The worst real pattern, and the reason ring capacity matters more than the
// measure+draw doubling suggests. GfxRenderer.cpp:1606 truncates an over-long
// list row with:
//
//   while (!item.empty() && getTextWidth(fontId, (item + ellipsis)) >= maxWidth)
//     item.pop_back();
//
// Each iteration re-walks the whole (shrinking) string, so a row needing an
// ellipsis costs O(n^2) glyph lookups. If the ring cannot hold the row, every
// one of those lookups is an SD read.
TEST(SdCardFontIo, EllipsisTruncationLoopIsQuadratic) {
  SdCardFont font;
  ASSERT_TRUE(font.load(fixturePath().c_str()));
  EpdFont* ef = font.getEpdFont(0);
  ASSERT_NE(ef, nullptr);

  constexpr int kTitleLen = 20;   // an over-long Chinese title
  constexpr int kFinalLen = 12;   // what it truncates down to

  sdstub::counters().reset();
  for (int len = kTitleLen; len >= kFinalLen; len--) {  // the while loop
    for (int i = 0; i < len; i++) ef->getGlyph(kFirstCp + i);
  }
  for (int i = 0; i < kFinalLen; i++) ef->getGlyph(kFirstCp + i);  // then drawText
  const auto c = sdstub::counters();

  const int distinct = kTitleLen;
  std::printf("[ ellipsis ] %d->%d chars: opens=%d reads=%d (only %d distinct glyphs)\n", kTitleLen, kFinalLen,
              c.opens, c.reads, distinct);

  EXPECT_EQ(c.opens, 1);
  std::printf("[ ellipsis ] reads/ideal = %.1fx\n", c.reads / static_cast<double>(2 * distinct));
  // The row fits the ring, so the quadratic re-measuring is served from RAM and
  // each distinct glyph is read exactly once (metadata + bitmap). At the old
  // capacity of 8 this was 312 reads — 7.8x ideal.
  EXPECT_EQ(c.reads, 2 * distinct) << "truncation loop must not re-read from SD";
}

// Whole-screen scale: what one library list redraw costs.
TEST(SdCardFontIo, BookListScaleCost) {
  SdCardFont font;
  ASSERT_TRUE(font.load(fixturePath().c_str()));
  EpdFont* ef = font.getEpdFont(0);
  ASSERT_NE(ef, nullptr);

  constexpr int kRows = 8, kCharsPerRow = 10;
  sdstub::counters().reset();
  for (int pass = 0; pass < 2; pass++) {  // measure + draw
    for (int r = 0; r < kRows; r++) {
      for (int i = 0; i < kCharsPerRow; i++) {
        ef->getGlyph(kFirstCp + (r * kCharsPerRow + i) % kGlyphCount);
      }
    }
  }
  const auto c = sdstub::counters();
  std::printf("[ list ] %d rows x %d chars, measure+draw -> opens=%d seeks=%d reads=%d\n", kRows, kCharsPerRow,
              c.opens, c.seeks, c.reads);

  // Was 160 opens before the handle cache — one per glyph, per pass.
  EXPECT_EQ(c.opens, 1) << "a whole book-list redraw must cost a single file open";
}

// --- Page-turn behaviour (the reader path, which DOES prewarm) ---------------
//
// prewarmStyle() has an all-or-nothing "idle prewarm hit": it scans every
// requested codepoint against the resident mini intervals, and if even one is
// missing it sets covered=false and rebuilds the entire page from SD. For Latin
// that is nearly free (26 letters recur, so page N+1 is almost always a subset
// of what is already resident). A Chinese page is ~300 mostly-distinct hanzi, so
// consecutive pages overlap very little.

TEST(SdCardFontPageTurn, ColdPageCostsFullLoad) {
  SdCardFont font;
  ASSERT_TRUE(font.load(fixturePath().c_str()));
  const std::string page = pageText(kFirstCp, kPageChars);

  sdstub::counters().reset();
  font.prewarm(page.c_str(), 0x01);
  const auto c = sdstub::counters();
  std::printf("[ page cold ] %d chars -> opens=%d seeks=%d reads=%d bytes=%zu\n", kPageChars, c.opens, c.seeks,
              c.reads, c.bytesRead);
  EXPECT_GT(c.reads, 0);
}

// The good case: the idle prewarm of page N+1 already loaded exactly this page,
// so the turn itself is free.
TEST(SdCardFontPageTurn, IdlePrewarmHitIsFree) {
  SdCardFont font;
  ASSERT_TRUE(font.load(fixturePath().c_str()));
  const std::string page = pageText(kFirstCp, kPageChars);

  font.prewarm(page.c_str(), 0x01);  // idle prewarm of page N+1
  font.clearCache();                 // scope exit — retention keeps the data
  sdstub::counters().reset();
  font.prewarm(page.c_str(), 0x01);  // the actual turn
  const auto c = sdstub::counters();
  std::printf("[ page warm ] identical page -> reads=%d\n", c.reads);
  EXPECT_EQ(c.reads, 0) << "a fully-covered page turn must not touch SD";
}

// The defect: ONE new codepoint discards the whole resident page.
TEST(SdCardFontPageTurn, OneNewGlyphForcesFullRebuild) {
  SdCardFont font;
  ASSERT_TRUE(font.load(fixturePath().c_str()));

  font.prewarm(pageText(kFirstCp, kPageChars).c_str(), 0x01);
  font.clearCache();

  // Page N+1: same glyphs plus a single new one.
  const std::string nextPage = pageText(kFirstCp, kPageChars) + pageText(kFirstCp + kPageChars, 1);
  sdstub::counters().reset();
  font.prewarm(nextPage.c_str(), 0x01);
  const auto c = sdstub::counters();
  std::printf("[ page +1 ] %d resident + 1 new -> reads=%d (incremental ideal: 2)\n", kPageChars, c.reads);
  EXPECT_GT(c.reads, 2) << "documents the all-or-nothing rebuild";
}

// Realistic consecutive Chinese pages.
//
// Overlap measured on a real book (迷霧之子 首部曲, 392k chars, 3112 distinct):
// at 16pt/242 chars per page the median page holds 139 distinct glyphs of which
// 86 (62%) are new versus the previous page, and ZERO of 1620 page pairs were
// fully covered by their predecessor. Chinese prose barely reuses glyphs across
// pages, unlike Latin.
//
// Two consequences worth keeping straight:
//  - The subset check can essentially never be satisfied by the previous page.
//    A hit only happens because the idle prewarm loaded this exact page, which
//    is why IdlePrewarmHitIsFree is the case that matters.
//  - Incremental top-up would therefore save only ~1.6x (278 -> 172 reads),
//    not the order of magnitude a high-overlap guess suggests.
constexpr int kRealNewFraction = 62;  // percent of a page that is new, measured

// Since upstream #3071 prewarm is accumulative: the rebuild unions the
// codepoints already resident with the request (up to MAX_PAGE_GLYPHS), so a
// page turn re-reads the previous page's glyphs as well as the new ones —
// old ∪ new, not the new fraction, and not just the current page either.
TEST(SdCardFontPageTurn, RealisticOverlapRebuildsUnion) {
  SdCardFont font;
  ASSERT_TRUE(font.load(fixturePath().c_str()));

  font.prewarm(pageText(kFirstCp, kPageChars).c_str(), 0x01);
  font.clearCache();

  const int shift = kPageChars * kRealNewFraction / 100;
  const std::string nextPage = pageText(kFirstCp + shift, kPageChars);
  sdstub::counters().reset();
  font.prewarm(nextPage.c_str(), 0x01);
  const auto c = sdstub::counters();
  std::printf("[ page real ] %d%% new -> reads=%d (incremental would read %d)\n", kRealNewFraction, c.reads,
              2 * shift);
  EXPECT_EQ(c.reads, 2 * (kPageChars + shift)) << "old and new page glyphs are all re-read, not just the new ones";
}

// The silent stall: below MINI_RETAIN_MIN_FREE_HEAP (40KB) resetStyleMiniData
// frees the arenas outright, so the next turn is a cold load. No log line marks
// this on device, which is why the stalls look random.
TEST(SdCardFontPageTurn, HeapFloorDropsCacheAndForcesColdReload) {
  SdCardFont font;
  ASSERT_TRUE(font.load(fixturePath().c_str()));
  const std::string page = pageText(kFirstCp, kPageChars);

  font.prewarm(page.c_str(), 0x01);
  sdstub::freeHeap() = 30 * 1024;  // under the floor
  font.clearCache();
  sdstub::freeHeap() = 200 * 1024;

  sdstub::counters().reset();
  font.prewarm(page.c_str(), 0x01);
  const auto c = sdstub::counters();
  std::printf("[ page heap ] same page after heap-floor drop -> reads=%d\n", c.reads);
  EXPECT_GT(c.reads, 0) << "cache was dropped, so the identical page reloads";
}
