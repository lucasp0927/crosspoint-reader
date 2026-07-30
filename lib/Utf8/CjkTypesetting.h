#pragma once
#include <cstdint>

// CJK typesetting rules, consolidated in one place so layout (ParsedText line
// breaking) and rendering (GfxRenderer vertical text) apply the same policy
// and future rule changes are single-file edits.
//
// References:
//  - W3C "Requirements for Chinese Text Layout" (clreq), §3 line composition:
//    prohibition rules for line start / line end (行首行尾禁則, 避頭點).
//  - Taiwan MOE《重訂標點符號手冊》: vertical punctuation is centered in the
//    character frame; 專名號/書名號(甲式) run along the LEFT side of a
//    vertical column.
//  - clreq §A: in vertical flow, Western text and wide "bridging" punctuation
//    (brackets, dashes, ellipses) rotate 90° clockwise; Han/kana stay upright.
//
// Everything here is a pure codepoint-class lookup — no allocation, no state —
// and is exercised by the host test suite and the headless page renderer.

namespace cjkTypesetting {

// 行首禁則 (forbidden line start / 避頭點): a line or vertical column must not
// begin with these. The line breaker refuses to break BEFORE them, which also
// keeps multi-glyph sequences like —— and …… unsplit (a break inside the pair
// would be a break before the second glyph).
//
// ASCII closers/points are included because the rule is only consulted at
// CJK-adjacent break opportunities (hasCjkBreakOpportunityBetween); plain
// Latin word breaking never reaches it.
inline bool isForbiddenLineStart(const uint32_t cp) {
  switch (cp) {
    case '.':
    case ',':
    case ':':
    case ';':
    case '!':
    case '?':
    case ')':
    case ']':
    case '}':
    case '%':     // unit suffix: "50%" must not strand the sign
    case 0x00B0:  // ° (degree, unit suffix)
    case 0x00B7:  // · interpunct (間隔號, foreign-name separator)
    case 0x00BB:  // »
    case 0x2019:  // ’
    case 0x201D:  // ”
    case 0x2013:  // – en dash (ranges)
    case 0x2014:  // — em dash: keeps —— glued to preceding text and unsplit
    case 0x2015:  // ― horizontal bar (dash variant some EPUBs use)
    case 0x2025:  // ‥
    case 0x2026:  // … ellipsis: keeps …… glued and unsplit
    case 0x2027:  // ‧ hyphenation point (used as interpunct in TC names)
    case 0x2032:  // ′ (minutes / feet, unit suffix)
    case 0x2033:  // ″ (seconds / inches, unit suffix)
    case 0x3001:  // 、
    case 0x3002:  // 。
    case 0x3005:  // 々 iteration mark
    case 0x3009:  // 〉
    case 0x300B:  // 》
    case 0x300D:  // 」
    case 0x300F:  // 』
    case 0x3011:  // 】
    case 0x3015:  // 〕
    case 0x3017:  // 〗
    case 0x3019:  // 〙
    case 0x301B:  // 〛
    case 0x2500:  // ─ box drawing horizontal: Taiwanese EPUBs use ── as the em
    case 0x2501:  // ━ dash because it joins into a continuous rule (迷霧之子
                  //   uses U+2500 ×1146; U+2014 never appears)
    case 0x2E3A:  // ⸺ two-em dash
    case 0x2E3B:  // ⸻ three-em dash
    case 0x301C:  // 〜 wave dash
    case 0x30FB:  // ・ katakana middle dot (interpunct)
    case 0x30FC:  // ー prolonged sound mark
    case 0xFE50:  // ﹐ small comma
    case 0xFE51:  // ﹑ small ideographic comma
    case 0xFE52:  // ﹒ small full stop
    case 0xFE54:  // ﹔ small semicolon
    case 0xFE55:  // ﹕ small colon
    case 0xFE56:  // ﹖ small question mark
    case 0xFE57:  // ﹗ small exclamation mark
    case 0xFF01:  // ！
    case 0xFF05:  // ％ (unit suffix)
    case 0xFF09:  // ）
    case 0xFF0C:  // ，
    case 0xFF0E:  // ．
    case 0xFF1A:  // ：
    case 0xFF1B:  // ；
    case 0xFF1F:  // ？
    case 0xFF3D:  // ］
    case 0xFF5D:  // ｝
    case 0xFF5E:  // ～ fullwidth tilde ("3～5" ranges)
      return true;
    default:
      return false;
  }
}

// 行尾禁則 (forbidden line end): a line or vertical column must not end with
// these openers. The line breaker refuses to break AFTER them.
inline bool isForbiddenLineEnd(const uint32_t cp) {
  switch (cp) {
    case '(':
    case '[':
    case '{':
    case 0x00AB:  // «
    case 0x2018:  // ‘
    case 0x201C:  // “
    case 0x3008:  // 〈
    case 0x300A:  // 《
    case 0x300C:  // 「
    case 0x300E:  // 『
    case 0x3010:  // 【
    case 0x3014:  // 〔
    case 0x3016:  // 〖
    case 0x3018:  // 〘
    case 0x301A:  // 〚
    case 0xFF08:  // （
    case 0xFF3B:  // ［
    case 0xFF5B:  // ｛
      return true;
    default:
      return false;
  }
}

// Vertical presentation class for a codepoint in top-to-bottom flow. The
// .cpfont format carries no OpenType `vert` substitutions, so vertical forms
// are synthesized from the horizontal glyphs:
//  - Rotate: scripts that read sideways in vertical text (Latin, digits) and
//    wide punctuation whose glyph is drawn for horizontal flow (brackets,
//    dashes, ellipses, tildes, prolonged marks).
//  - CenteredStop: ideographic stop/comma/interpunct, whose ink sits toward a
//    corner of the em box in the horizontal glyph. Traditional Chinese
//    (Taiwan) vertical typesetting centers them in the character frame;
//    Japanese/Simplified style would place them top-right (possible future
//    setting — the classification stays, only the placement policy changes).
//  - Upright: everything else (Han, kana, fullwidth forms, ！？：；).
enum class VerticalForm : uint8_t { Upright, Rotate, CenteredStop };

inline VerticalForm verticalFormFor(const uint32_t cp) {
  if (cp < 0x2E80) return VerticalForm::Rotate;  // ASCII, Latin-1, general punctuation (– — … ‥)
  switch (cp) {
    case 0x3001:  // 、
    case 0x3002:  // 。
    case 0x30FB:  // ・
    case 0xFF0C:  // ，
    case 0xFF0E:  // ．
    case 0xFF61:  // ｡
    case 0xFF64:  // ､
      return VerticalForm::CenteredStop;
    case 0x3008:  // 〈
    case 0x3009:  // 〉
    case 0x300A:  // 《
    case 0x300B:  // 》
    case 0x300C:  // 「
    case 0x300D:  // 」
    case 0x300E:  // 『
    case 0x300F:  // 』
    case 0x3010:  // 【
    case 0x3011:  // 】
    case 0x3014:  // 〔
    case 0x3015:  // 〕
    case 0x3016:  // 〖
    case 0x3017:  // 〗
    case 0x301C:  // 〜
    case 0x30FC:  // ー
    case 0xFF08:  // （
    case 0xFF09:  // ）
    case 0xFF0D:  // －
    case 0xFF1D:  // ＝
    case 0xFF3B:  // ［
    case 0xFF3D:  // ］
    case 0xFF5B:  // ｛
    case 0xFF5D:  // ｝
    case 0xFF5E:  // ～
      return VerticalForm::Rotate;
    default:
      return VerticalForm::Upright;
  }
}

// X position (relative to the column cell's left edge) of the rotated-text
// baseline in a vertical column: rotated glyphs hang their ascender side to
// the right of this line, descenders to the left, which centers the rotated
// em box on the column. Shared by glyph rendering and decoration drawing so
// underlines land exactly where the rotated baseline is.
inline int verticalBaselineX(const int columnPitch, const int ascender, const int descenderDepth) {
  return (columnPitch - (ascender + descenderDepth)) / 2 + descenderDepth;
}

// Decoration line X offsets (from the column cell's left edge) for vertical
// text, per Taiwan convention: 專名號/underline runs along the LEFT side of
// the column — which is also the below-baseline side of rotated Latin, so one
// rule serves both scripts. Strikethrough runs through the column center.
inline int verticalUnderlineX(const int columnPitch, const int ascender, const int descenderDepth) {
  return verticalBaselineX(columnPitch, ascender, descenderDepth) - 2;
}
inline int verticalStrikethroughX(const int columnPitch) { return columnPitch / 2; }

}  // namespace cjkTypesetting
