#pragma once

#include <cstddef>
#include <cstdint>

#include "components/themes/BaseTheme.h"

// Codepoint table for the subsetted Material Symbols Rounded icon font.
//
// The icon font is a single subsetted EpdFont containing only the glyphs
// listed in lib/EpdFont/scripts/convert-builtin-fonts.sh.  Each glyph lives
// in the Unicode Private Use Area; this header maps the project's UIIcon
// enum (defined in BaseTheme.h) to the corresponding Material Symbols
// codepoint and provides a small UTF-8 encoder so the icon can be drawn
// with GfxRenderer::drawText(MATERIAL_SYMBOLS_*_FONT_ID, ...).
//
// Themes that do not use this font (Classic, Lyra, RoundedRaff) are not
// affected; they continue to draw 1-bit bitmaps from src/components/icons/.
namespace MaterialIcons {

// Material Symbols Rounded codepoints.  Each constant is the canonical
// glyph name from Google's codepoints file; keep this list in sync with
// MATERIAL_SYMBOLS_CODEPOINTS in convert-builtin-fonts.sh.
constexpr uint32_t kFolder = 0xE2C7;
constexpr uint32_t kDescription = 0xE873;
constexpr uint32_t kImage = 0xE3F4;
constexpr uint32_t kMenuBook = 0xEA19;
constexpr uint32_t kDraft = 0xE66D;
constexpr uint32_t kHistory = 0xE8B3;
constexpr uint32_t kSettings = 0xE8B8;
constexpr uint32_t kSwapVert = 0xE8D5;
constexpr uint32_t kLibraryBooks = 0xE02F;
constexpr uint32_t kWifi = 0xE63E;
constexpr uint32_t kWifiTethering = 0xE1E2;

// Reserve set, available for future UI work without touching the font build.
constexpr uint32_t kArrowBack = 0xE5C4;
constexpr uint32_t kCheck = 0xE5CA;
constexpr uint32_t kClose = 0xE5CD;
constexpr uint32_t kChevronRight = 0xE5CC;
constexpr uint32_t kChevronLeft = 0xE5CB;
constexpr uint32_t kBatteryFull = 0xE1A5;
constexpr uint32_t kKeyboardBackspace = 0xE317;
constexpr uint32_t kKeyboardReturn = 0xE31B;
constexpr uint32_t kKeyboardCapslock = 0xE318;
constexpr uint32_t kSync = 0xE627;

// Lookup table indexed by UIIcon enum order.  static constexpr ⇒ flash
// resident, zero DRAM cost.  Entries are validated by static_assert below.
static constexpr uint32_t kIconCodepoints[] = {
    kFolder,         // UIIcon::Folder
    kDescription,    // UIIcon::Text
    kImage,          // UIIcon::Image
    kMenuBook,       // UIIcon::Book
    kDraft,          // UIIcon::File
    kHistory,        // UIIcon::Recent
    kSettings,       // UIIcon::Settings
    kSwapVert,       // UIIcon::Transfer
    kLibraryBooks,   // UIIcon::Library
    kWifi,           // UIIcon::Wifi
    kWifiTethering,  // UIIcon::Hotspot
};

// Compile-time guard: extending UIIcon without adding a codepoint must fail
// to build rather than silently render the wrong glyph.
static_assert(sizeof(kIconCodepoints) / sizeof(kIconCodepoints[0]) == static_cast<size_t>(UIIcon::Hotspot) + 1,
              "kIconCodepoints must have one entry per UIIcon enum value");

// Resolve a UIIcon to its Material Symbols codepoint.  Returns 0 (.notdef)
// for any out-of-range value so the caller cannot dereference past the
// table.  Constexpr so it folds away at the call site.
constexpr uint32_t codepointFor(UIIcon icon) {
  const auto i = static_cast<size_t>(icon);
  return i < (sizeof(kIconCodepoints) / sizeof(kIconCodepoints[0])) ? kIconCodepoints[i] : 0;
}

// Encode a single Unicode codepoint into the caller-supplied UTF-8 buffer.
// `buf` must be at least 5 bytes (4 UTF-8 bytes + null terminator); the
// returned char* is `buf` and is always null-terminated.  All Material
// Symbols glyphs are in the BMP PUA, so 3 bytes are sufficient in practice,
// but we accept the full 4-byte range for safety.
inline char* encodeUtf8(uint32_t cp, char* buf) {
  if (cp < 0x80) {
    buf[0] = static_cast<char>(cp);
    buf[1] = '\0';
  } else if (cp < 0x800) {
    buf[0] = static_cast<char>(0xC0 | (cp >> 6));
    buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
    buf[2] = '\0';
  } else if (cp < 0x10000) {
    buf[0] = static_cast<char>(0xE0 | (cp >> 12));
    buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
    buf[3] = '\0';
  } else {
    buf[0] = static_cast<char>(0xF0 | (cp >> 18));
    buf[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = static_cast<char>(0x80 | (cp & 0x3F));
    buf[4] = '\0';
  }
  return buf;
}

// Convenience: encode a UIIcon directly.  Output buffer must be at least 5
// bytes; lives on the caller's stack — no heap, no globals.
inline char* encodeIcon(UIIcon icon, char* buf) { return encodeUtf8(codepointFor(icon), buf); }

}  // namespace MaterialIcons
