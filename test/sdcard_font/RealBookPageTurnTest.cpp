// Replays real pages from a real book through the real .cpfont on the host.
//
// The synthetic fixture in SdCardFontIoTest pins mechanism; this pins magnitude.
// It is opt-in because it needs artefacts that are not in the repo:
//
//   CPFONT_PATH=/path/to/EmberPlexMin_16.cpfont \
//   PAGE_TEXT_FILE=/path/to/pages.txt \
//   ./RealBookPageTurnTest
//
// pages.txt is one page of UTF-8 text per line, paginated at the character count
// the device fits on screen at that point size.
//
// What this CAN establish: the exact SD cost of a page turn, whether consecutive
// pages hit the prewarm subset check, and what the 40KB heap floor costs when it
// fires. What it CANNOT: whether the floor actually fires on device — that
// depends on total system heap (framebuffer, EPUB parser, section cache, WiFi),
// none of which exists here. The device already logs that every 10s from
// main.cpp:469 via getMinFreeHeap().

#include <algorithm>
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

#include "HalStorage.h"
#include "SdCardFont.h"
#include "Utf8.h"

namespace {

const char* envOr(const char* k) {
  const char* v = std::getenv(k);
  return (v && *v) ? v : nullptr;
}

std::vector<std::string> loadPages(const char* path) {
  std::vector<std::string> pages;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty()) pages.push_back(line);
  }
  return pages;
}

struct Fixture {
  const char* font;
  std::vector<std::string> pages;
  bool ok() const { return font && pages.size() >= 2; }
};

Fixture realBook() {
  Fixture f{envOr("CPFONT_PATH"), {}};
  if (const char* p = envOr("PAGE_TEXT_FILE")) f.pages = loadPages(p);
  return f;
}

}  // namespace

TEST(RealBookPageTurn, CostOfEachPageTurn) {
  const Fixture fx = realBook();
  if (!fx.ok()) GTEST_SKIP() << "set CPFONT_PATH and PAGE_TEXT_FILE";

  SdCardFont font;
  ASSERT_TRUE(font.load(fx.font)) << fx.font;

  // Sequential reading WITHOUT the idle prewarm: each turn prewarms the page it
  // is about to show, having only the previous page resident.
  std::vector<int> coldReads;
  size_t bytes = 0;
  for (size_t i = 0; i < fx.pages.size(); i++) {
    sdstub::counters().reset();
    font.prewarm(fx.pages[i].c_str(), 0x01);
    coldReads.push_back(sdstub::counters().reads);
    bytes += sdstub::counters().bytesRead;
    font.clearCache();
  }
  const int total = std::accumulate(coldReads.begin(), coldReads.end(), 0);
  std::vector<int> sorted = coldReads;
  std::sort(sorted.begin(), sorted.end());
  std::printf("[ real: no idle prewarm ] %zu pages | median %d reads/turn | total %d reads, %zu KB\n",
              fx.pages.size(), sorted[sorted.size() / 2], total, bytes / 1024);
  EXPECT_GT(total, 0);
}

TEST(RealBookPageTurn, IdlePrewarmMakesTurnsFree) {
  const Fixture fx = realBook();
  if (!fx.ok()) GTEST_SKIP() << "set CPFONT_PATH and PAGE_TEXT_FILE";

  SdCardFont font;
  ASSERT_TRUE(font.load(fx.font));

  // The intended flow: after showing page N, idle time prewarms page N+1; the
  // turn itself then re-prewarms the same page and should touch nothing.
  int turnReads = 0, idleReads = 0;
  for (size_t i = 0; i + 1 < fx.pages.size(); i++) {
    sdstub::counters().reset();
    font.prewarm(fx.pages[i + 1].c_str(), 0x01);  // idle prewarm
    idleReads += sdstub::counters().reads;
    font.clearCache();

    sdstub::counters().reset();
    font.prewarm(fx.pages[i + 1].c_str(), 0x01);  // the turn
    turnReads += sdstub::counters().reads;
    font.clearCache();
  }
  std::printf("[ real: with idle prewarm ] idle %d reads, turns %d reads\n", idleReads, turnReads);
  EXPECT_EQ(turnReads, 0) << "a covered page turn must cost nothing";
}

TEST(RealBookPageTurn, HeapFloorCostsAFullPage) {
  const Fixture fx = realBook();
  if (!fx.ok()) GTEST_SKIP() << "set CPFONT_PATH and PAGE_TEXT_FILE";

  SdCardFont font;
  ASSERT_TRUE(font.load(fx.font));

  font.prewarm(fx.pages[1].c_str(), 0x01);  // idle prewarm paid for page 1
  sdstub::freeHeap() = 30 * 1024;           // ... then heap dipped under the floor
  font.clearCache();
  sdstub::freeHeap() = 200 * 1024;

  sdstub::counters().reset();
  font.prewarm(fx.pages[1].c_str(), 0x01);  // the turn now reloads from scratch
  const auto c = sdstub::counters();
  std::printf("[ real: heap floor ] work discarded, turn costs %d reads / %zu KB\n", c.reads, c.bytesRead / 1024);
  EXPECT_GT(c.reads, 0);
}

// --- Advance-table behaviour during section pagination -----------------------
//
// Layout only needs advanceX, and buildAdvanceTable() batches those into a
// compact table. But ADVANCE_CACHE_LIMIT is 768 entries while a Traditional
// Chinese novel uses ~3100 distinct characters, and mergeIntoAdvanceTable()
// fills it by sorted-merge with the TAIL DROPPED — so it keeps the lowest
// codepoints and, once full, returns immediately and never adapts.
//
// Every miss then falls through GfxRenderer.cpp:1898 to font.getGlyph(cp),
// which loads glyph metadata AND the bitmap from SD to read two bytes of
// advance, and burns an overflow ring slot doing it. On device this showed up
// as ~6ms per character and a 4711ms max loop during pagination.
TEST(RealBookAdvanceTable, PaginationCostAfterCacheSaturates) {
  const Fixture fx = realBook();
  if (!fx.ok()) GTEST_SKIP() << "set CPFONT_PATH and PAGE_TEXT_FILE";

  SdCardFont font;
  ASSERT_TRUE(font.load(fx.font));
  EpdFont* ef = font.getEpdFont(0);
  ASSERT_NE(ef, nullptr);

  // Fill the advance table the way pagination does: page by page.
  for (const auto& page : fx.pages) font.buildAdvanceTable(page.c_str(), 0x01);

  // Now lay the pages out, mirroring GfxRenderer's measure loop exactly.
  int hits = 0, misses = 0;
  sdstub::counters().reset();
  for (const auto& page : fx.pages) {
    const auto* p = reinterpret_cast<const uint8_t*>(page.c_str());
    while (const uint32_t cp = utf8NextCodepoint(&p)) {
      if (font.getAdvance(cp, 0) != 0) {
        hits++;
      } else {
        misses++;
        // Mirror GfxRenderer.cpp:1898 exactly: metadata-only first, getGlyph
        // only if the font does not cover the codepoint.
        if (font.fetchAdvanceFromSd(cp, 0) == 0) ef->getGlyph(cp);
      }
    }
  }
  const auto c = sdstub::counters();
  const double missPct = 100.0 * misses / (hits + misses);
  std::printf("[ advance ] %zu pages | hits=%d misses=%d (%.1f%% miss)\n", fx.pages.size(), hits, misses, missPct);
  std::printf("[ advance ] miss cost: %d reads, %zu KB read to obtain %d bytes of advance\n", c.reads,
              c.bytesRead / 1024, misses * 2);
  EXPECT_GT(misses, 0) << "documents advance-cache saturation on a CJK book";
}
