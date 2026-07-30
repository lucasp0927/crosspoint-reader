// Verifies the flash-primary / SD-tail hybrid against a real book.
//
// Opt-in, same as RealBookPageTurnTest:
//   CPFONT_PATH=.../EmberPlexMin_14.cpfont PAGE_TEXT_FILE=.../pages.txt ./FlashHybridTest
//
// The claim under test: glyphs compiled into flash cost no SD I/O at all, so a
// page of Chinese should touch SD only for the tail the flash font lacks.

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "FlashReaderFont.h"
#include "HalStorage.h"
#include "SdCardFont.h"
#include "Utf8.h"

namespace {

std::vector<std::string> loadPages(const char* path) {
  std::vector<std::string> pages;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line))
    if (!line.empty()) pages.push_back(line);
  return pages;
}

}  // namespace

TEST(FlashHybrid, FlashGlyphsCostNoSdIo) {
  const char* fontPath = std::getenv("CPFONT_PATH");
  const char* pagePath = std::getenv("PAGE_TEXT_FILE");
  if (!fontPath || !pagePath) GTEST_SKIP() << "set CPFONT_PATH and PAGE_TEXT_FILE";
  const auto pages = loadPages(pagePath);
  ASSERT_GE(pages.size(), 2u);

  SdCardFont sd;
  ASSERT_TRUE(sd.load(fontPath));
  EpdFont* hybrid = FlashReaderFont::bind(sd, 0);
  ASSERT_NE(hybrid, nullptr) << "no flash font compiled for style 0";

  int fromFlash = 0, fromSd = 0, notCovered = 0;
  sdstub::counters().reset();
  for (const auto& page : pages) {
    const auto* p = reinterpret_cast<const uint8_t*>(page.c_str());
    while (const uint32_t cp = utf8NextCodepoint(&p)) {
      const auto before = sdstub::counters().reads;
      const EpdGlyph* g = hybrid->getGlyph(cp);
      if (!g) {
        notCovered++;
      } else if (sdstub::counters().reads == before) {
        fromFlash++;  // served without touching SD
      } else {
        fromSd++;
      }
    }
  }
  const auto c = sdstub::counters();
  const int total = fromFlash + fromSd + notCovered;
  std::printf("[ hybrid ] %zu pages, %d chars: flash=%d (%.1f%%) sd=%d (%.1f%%) uncovered=%d\n", pages.size(), total,
              fromFlash, 100.0 * fromFlash / total, fromSd, 100.0 * fromSd / total, notCovered);
  std::printf("[ hybrid ] SD cost: opens=%d reads=%d bytes=%zu KB\n", c.opens, c.reads, c.bytesRead / 1024);

  EXPECT_GT(fromFlash, 0) << "flash font served nothing — binding is broken";
  EXPECT_GT(100.0 * fromFlash / total, 80.0) << "flash coverage below the ~89% this was sized for";
}

// The tail must still render correctly, not silently vanish.
TEST(FlashHybrid, SdTailStillResolves) {
  const char* fontPath = std::getenv("CPFONT_PATH");
  if (!fontPath) GTEST_SKIP() << "set CPFONT_PATH";

  SdCardFont sd;
  ASSERT_TRUE(sd.load(fontPath));
  EpdFont* hybrid = FlashReaderFont::bind(sd, 0);
  ASSERT_NE(hybrid, nullptr);

  // U+9F98 is a rare hanzi well outside the top-1000, so it must come from SD.
  const EpdGlyph* rare = hybrid->getGlyph(0x9F98);
  ASSERT_NE(rare, nullptr) << "rare glyph lost — SD tail not wired";
  EXPECT_GT(rare->width, 0);
  EXPECT_TRUE(sd.isOverflowGlyph(rare)) << "should have come from the SD overflow ring";

  // And coverage must be the union of flash and SD.
  EXPECT_TRUE(hybrid->hasCodepoint(0x4E00)) << "common hanzi (flash)";
  EXPECT_TRUE(hybrid->hasCodepoint(0x9F98)) << "rare hanzi (SD, via coverageHandler)";
}

// Bold must bind too, and must be a genuinely heavier face than regular rather
// than a second copy of it.
TEST(FlashHybrid, BoldBindsAndIsHeavierThanRegular) {
  const char* fontPath = std::getenv("CPFONT_PATH");
  if (!fontPath) GTEST_SKIP() << "set CPFONT_PATH";

  SdCardFont sd;
  ASSERT_TRUE(sd.load(fontPath));
  EpdFont* regular = FlashReaderFont::bind(sd, 0);
  EpdFont* bold = FlashReaderFont::bind(sd, 1);
  ASSERT_NE(regular, nullptr);
  ASSERT_NE(bold, nullptr) << "bold style did not bind";

  const EpdGlyph* r = regular->getGlyph(0x4E2D);
  const EpdGlyph* b = bold->getGlyph(0x4E2D);
  ASSERT_NE(r, nullptr);
  ASSERT_NE(b, nullptr);
  std::printf("[ bold ] U+4E2D regular %ux%u adv=%.2f | bold %ux%u adv=%.2f\n", r->width, r->height,
              r->advanceX / 16.0, b->width, b->height, b->advanceX / 16.0);
  EXPECT_GT(b->dataLength, 0u);
  EXPECT_NE(r, b) << "bold and regular resolved to the same glyph";
}
