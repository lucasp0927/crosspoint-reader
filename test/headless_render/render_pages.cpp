// Headless page renderer: runs a real EPUB through the real layout engine
// (Section → ChapterHtmlSlimParser → ParsedText → Page) and the real
// GfxRenderer, on the host, and dumps each rendered page's framebuffer as a
// PGM image. The only fakes are the HAL stubs in stubs/ — every layout and
// glyph decision is made by the same code that runs on the X4.
//
//   render_pages --epub <host path to .epub> --fonts <host dir with .cpfont>
//                --family EmberPlexMin --size 14 --spine 2 --pages 0-4
//                --out <dir> [--sdroot <dir>] [--margin 5] [--statusbar 19]
//                [--list]
//
// Viewport math mirrors EpubReaderActivity.cpp:1092-1117 (portrait, default
// settings: screenMargin=5, status bar text lane = 19px).

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

// Stubs first (Arduino/HAL), then the real engine.
#include "Epub.h"
#include "Epub/Page.h"
#include "Epub/ReaderRenderSpec.h"
#include "Epub/Section.h"
#include "Epub/blocks/ImageBlock.h"
#include "GfxRenderer.h"
#include "HalDisplay.h"
#include "HalStorage.h"
#include "SdCardFontManager.h"
#include "SdCardFontRegistry.h"

namespace fs = std::filesystem;

// Global the firmware defines in main.cpp; cover converters reference it.
HalDisplay display;

namespace {

struct Args {
  std::string epub;
  std::string fontsDir;
  std::string family = "EmberPlexMin";
  int size = 14;
  int spine = -1;
  int pageFrom = 0;
  int pageTo = 0;
  std::string out = "render_out";
  std::string sdroot = "./sd_root";
  int margin = 5;      // SETTINGS.screenMargin default (SCREEN_MARGIN_MIN)
  int statusBar = 19;  // UITheme::getStatusBarHeight() with default settings
  bool list = false;
  bool extraParagraphSpacing = true;  // SETTINGS default
  bool embeddedStyle = true;          // SETTINGS.embeddedStyle default (book CSS honoured)
  int alignment = 0;                  // JUSTIFIED
  bool vertical = false;              // vertical CJK mode (columns top-to-bottom, right-to-left)
};

bool parseArgs(int argc, char** argv, Args& a) {
  for (int i = 1; i < argc; i++) {
    const std::string k = argv[i];
    auto next = [&](std::string& dst) {
      if (i + 1 >= argc) return false;
      dst = argv[++i];
      return true;
    };
    std::string v;
    if (k == "--epub" && next(v)) {
      a.epub = v;
    } else if (k == "--fonts" && next(v)) {
      a.fontsDir = v;
    } else if (k == "--family" && next(v)) {
      a.family = v;
    } else if (k == "--size" && next(v)) {
      a.size = atoi(v.c_str());
    } else if (k == "--spine" && next(v)) {
      a.spine = atoi(v.c_str());
    } else if (k == "--pages" && next(v)) {
      const auto dash = v.find('-');
      if (dash == std::string::npos) {
        a.pageFrom = a.pageTo = atoi(v.c_str());
      } else {
        a.pageFrom = atoi(v.substr(0, dash).c_str());
        a.pageTo = atoi(v.substr(dash + 1).c_str());
      }
    } else if (k == "--out" && next(v)) {
      a.out = v;
    } else if (k == "--sdroot" && next(v)) {
      a.sdroot = v;
    } else if (k == "--margin" && next(v)) {
      a.margin = atoi(v.c_str());
    } else if (k == "--statusbar" && next(v)) {
      a.statusBar = atoi(v.c_str());
    } else if (k == "--list") {
      a.list = true;
    } else if (k == "--vertical") {
      a.vertical = true;
    } else if (k == "--no-extra-spacing") {
      a.extraParagraphSpacing = false;
    } else if (k == "--no-embedded-style") {
      a.embeddedStyle = false;
    } else if (k == "--align" && next(v)) {
      a.alignment = atoi(v.c_str());
    } else {
      fprintf(stderr, "unknown/incomplete arg: %s\n", k.c_str());
      return false;
    }
  }
  return !a.epub.empty();
}

// Replace any existing link/file at `link` with a symlink to `target`.
bool relink(const fs::path& target, const fs::path& link) {
  std::error_code ec;
  fs::create_directories(link.parent_path(), ec);
  fs::remove(link, ec);
  fs::create_symlink(fs::absolute(target), link, ec);
  if (ec) {
    fprintf(stderr, "symlink %s -> %s failed: %s\n", link.c_str(), target.c_str(), ec.message().c_str());
    return false;
  }
  return true;
}

// Dump the logical portrait view (480x800) of the physical framebuffer
// (800x480, 1bpp, MSB-first, bit cleared = black) as an 8-bit PGM.
// Inverse of rotateCoordinates(Portrait): phys = (y, panelHeight-1-x).
bool dumpPgm(const GfxRenderer& renderer, const HalDisplay& display, const std::string& path) {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const uint8_t* fb = display.getFrameBuffer();
  const int panelHeight = HalDisplay::DISPLAY_HEIGHT;
  const int widthBytes = HalDisplay::DISPLAY_WIDTH_BYTES;

  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  fprintf(f, "P5\n%d %d\n255\n", w, h);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const int phyX = y;
      const int phyY = panelHeight - 1 - x;
      const uint8_t byte = fb[phyY * widthBytes + (phyX / 8)];
      const bool white = byte & (1 << (7 - (phyX % 8)));
      fputc(white ? 255 : 0, f);
    }
  }
  fclose(f);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Args a;
  if (!parseArgs(argc, argv, a)) {
    fprintf(stderr,
            "usage: render_pages --epub <file.epub> [--fonts <dir>] [--family N] [--size 14]\n"
            "                    [--spine N | --list] [--pages A-B] [--out dir] [--sdroot dir]\n"
            "                    [--margin 5] [--statusbar 19] [--no-extra-spacing] [--align 0]\n"
            "                    [--vertical] [--no-embedded-style]\n");
    return 2;
  }

  // Sandbox root must be latched before the first Storage call.
  setenv("HOST_SD_ROOT", fs::absolute(a.sdroot).c_str(), 1);
  fs::create_directories(a.sdroot);
  fs::create_directories(a.out);

  // Map the book and fonts into the sandbox at device-style paths.
  if (!relink(a.epub, fs::path(a.sdroot) / "books" / "book.epub")) return 1;
  if (!a.fontsDir.empty()) {
    if (!relink(a.fontsDir, fs::path(a.sdroot) / ".fonts" / a.family)) return 1;
  }

  GfxRenderer renderer(display);
  renderer.begin();
  renderer.setOrientation(GfxRenderer::Portrait);

  // Load the SD font family exactly the way the firmware does.
  int fontId = 0;
  SdCardFontManager fontManager;
  if (!a.fontsDir.empty()) {
    SdCardFontFamilyInfo family;
    family.name = a.family;
    for (const auto& e : fs::directory_iterator(fs::path(a.sdroot) / ".fonts" / a.family)) {
      const std::string name = e.path().filename().string();
      const auto us = name.rfind('_');
      const auto dot = name.rfind(".cpfont");
      if (us == std::string::npos || dot == std::string::npos || dot <= us) continue;
      SdCardFontFileInfo fi;
      fi.path = std::string("/.fonts/") + a.family + "/" + name;
      fi.pointSize = static_cast<uint8_t>(atoi(name.substr(us + 1, dot - us - 1).c_str()));
      fi.style = 0;
      family.files.push_back(fi);
    }
    if (family.files.empty()) {
      fprintf(stderr, "no .cpfont files found in %s\n", a.fontsDir.c_str());
      return 1;
    }
    if (!fontManager.loadFamily(family, renderer, static_cast<uint8_t>(a.size))) {
      fprintf(stderr, "loadFamily failed\n");
      return 1;
    }
    fontId = fontManager.getFontId(a.family);
    printf("font: %s at %upt -> id %d\n", a.family.c_str(), fontManager.currentPointSize(), fontId);
  }

  auto epub = std::make_shared<Epub>("/books/book.epub", "/.crosspoint");
  if (!epub->load(true)) {
    fprintf(stderr, "epub load failed\n");
    return 1;
  }
  printf("book: \"%s\" by \"%s\", %d spine items\n", epub->getTitle().c_str(), epub->getAuthor().c_str(),
         epub->getSpineItemsCount());

  // Same lazy-extraction hook EpubReaderActivity.cpp:164 installs. Section
  // builds only header-probe images, so without this every image renders as an
  // empty placeholder here regardless of whether the device would show it.
  ImageBlock::setExtractor(epub.get(), [](void* ctx, const char* src, const char* dest) {
    return static_cast<Epub*>(ctx)->extractItemToFile(src, dest);
  });

  if (a.list || a.spine < 0) {
    for (int i = 0; i < epub->getSpineItemsCount(); i++) {
      printf("  spine %2d: %s\n", i, epub->getSpineItem(i).href.c_str());
    }
    if (a.spine < 0) return 0;
  }

  // Viewport: EpubReaderActivity.cpp:1092-1117 with default settings.
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  top += a.margin;
  left += a.margin;
  right += a.margin;
  bottom += std::max(a.margin, a.statusBar);
  const uint16_t viewportWidth = renderer.getScreenWidth() - left - right;
  const uint16_t viewportHeight = renderer.getScreenHeight() - top - bottom;
  printf("viewport: %ux%u (margins T%d R%d B%d L%d)\n", viewportWidth, viewportHeight, top, right, bottom, left);

  ReaderRenderSpec spec;
  spec.fontId = fontId;
  spec.lineCompression = 1.0f;
  spec.extraParagraphSpacing = a.extraParagraphSpacing;
  spec.paragraphAlignment = static_cast<uint8_t>(a.alignment);
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.hyphenationEnabled = false;
  spec.embeddedStyle = a.embeddedStyle;
  spec.imageRendering = 0;
  spec.focusReadingEnabled = false;
  spec.verticalMode = a.vertical;

  if (a.vertical) {
    // Mirror what the reader will do for vertical pages: block offsets grow
    // from the right edge across spec.viewportWidth; columns are one layout
    // line pitch wide.
    renderer.setVerticalText(true, viewportWidth, renderer.getLineHeight(fontId, spec.lineCompression));
  }

  Section section(epub, a.spine, renderer);
  if (!section.loadSectionFile(spec)) {
    printf("no cached section, building...\n");
    if (!section.createSectionFile(spec)) {
      fprintf(stderr, "section build failed\n");
      return 1;
    }
    if (!section.loadSectionFile(spec)) {
      fprintf(stderr, "section reload after build failed\n");
      return 1;
    }
  }
  printf("spine %d: %u pages\n", a.spine, section.pageCount);

  const int last = std::min<int>(a.pageTo, section.pageCount > 0 ? section.pageCount - 1 : 0);
  for (int p = a.pageFrom; p <= last; p++) {
    auto page = section.loadPage(p);
    if (!page) {
      fprintf(stderr, "loadPage(%d) failed\n", p);
      return 1;
    }
    renderer.clearScreen();
    page->render(renderer, fontId, left, top);

    char suffix[64];
    snprintf(suffix, sizeof(suffix), "/spine%02d_page%03d.pgm", a.spine, p);
    const std::string name = a.out + suffix;
    if (!dumpPgm(renderer, display, name)) {
      fprintf(stderr, "dump failed: %s\n", name.c_str());
      return 1;
    }
    printf("wrote %s\n", name.c_str());
  }
  return 0;
}
