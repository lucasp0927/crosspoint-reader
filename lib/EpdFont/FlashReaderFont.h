#pragma once

#include <EpdFont.h>
#include <EpdFontData.h>
#include <SdCardFont.h>

#include <cstdint>

/// A reader font whose common glyphs live in flash, with an SD font serving the
/// tail one glyph at a time.
///
/// Motivation: on a CJK book every glyph the SD font serves costs an SD read,
/// and that dominates everything else. Measured on a real 392k-character
/// Traditional Chinese novel, laying out 300 pages cost ~1.9MB of SD reads even
/// after the read path was made metadata-only. Glyphs compiled into flash cost
/// neither RAM nor SD I/O: the bitmaps, glyph table and interval table are
/// `static const`, so they are memory-mapped and read at cache speed.
///
/// 1000 CJK characters plus the full Latin set covers ~89% of the text in the
/// books this was measured against, for 271KB of flash and 0 bytes of RAM.
///
/// No renderer changes are needed — the pieces already exist:
///   * EpdFont::getGlyph()      falls through to glyphMissHandler on a miss
///   * EpdFont::hasCodepoint()  unions its intervals with coverageHandler
///   * GfxRenderer::getGlyphBitmap() routes overflow glyphs to the SD buffer
///     and everything else to fontData->bitmap
///
/// The only thing missing is that a builtin EpdFontData is `static const`, so
/// the handlers cannot be written into it. This keeps a mutable copy per style
/// (~80 bytes) pointing at the same flash arrays.
class FlashReaderFont {
 public:
  /// Point size the compiled-in glyphs were rasterised at. A font loaded at any
  /// other size cannot use this and must fall back to the SD font entirely.
  static uint8_t pointSize();

  /// True if a flash font exists for `style` at `pointSize()`.
  static bool hasStyle(uint8_t style);

  /// Bind the flash font for `style` to `sdFont`, which will serve any codepoint
  /// the flash font lacks. Returns nullptr if there is no flash font for that
  /// style. The returned EpdFont is owned here and outlives the call.
  ///
  /// `sdFont` must outlive the returned EpdFont — SdCardFontManager owns both
  /// and frees them together in unloadAll().
  static EpdFont* bind(SdCardFont& sdFont, uint8_t style);

  /// Drop all bindings. Call before the SD fonts they point at are destroyed.
  static void reset();
};
