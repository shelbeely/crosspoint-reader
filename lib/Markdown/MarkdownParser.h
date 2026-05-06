#pragma once

// Streaming block-level Markdown parser for CrossPoint Reader.
//
// Design notes:
// - Pure C++; no Arduino, ESP-IDF, or framework dependency. Host-buildable
//   so it can be unit tested under `test/markdown/` with the standard
//   c++20 toolchain.
// - Block-level parser is incremental: callers feed a buffer and the parser
//   consumes one complete block per call, reporting bytes consumed. This
//   matches the chunk-streaming pattern used by `lib/Txt` and lets the
//   reader activity index pages by file offset.
// - Inline span parser is a single pass over a string_view producing a
//   small vector of (text, style, href) runs.
// - All allocations are bounded by the size of the buffer the caller
//   supplied; the parser does not buffer the whole file.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace md {

enum class BlockKind : uint8_t {
  Empty = 0,        // buffer ran out before a block was completed; caller should refill
  Paragraph,
  Heading1,
  Heading2,
  Heading3,
  Heading4,
  CodeBlock,        // fenced (```), one Block per line of code
  BulletItem,       // -, *, or +
  NumberedItem,     // "1. ", "23. ", etc.
  Quote,            // line starting with ">"
  HorizontalRule,   // "---" / "***" / "___"
  ImageStub,        // standalone "![alt](src)"
};

enum SpanStyle : uint8_t {
  STYLE_NONE = 0,
  STYLE_BOLD = 1 << 0,
  STYLE_ITALIC = 1 << 1,
  STYLE_CODE = 1 << 2,    // inline `code`
  STYLE_LINK = 1 << 3,    // [text](href)
};

struct InlineSpan {
  std::string text;       // owned copy — buffer behind it may be reused/freed
  uint8_t style = STYLE_NONE;
  std::string href;       // populated only when (style & STYLE_LINK)
};

struct Block {
  BlockKind kind = BlockKind::Empty;
  uint8_t indent = 0;     // list nesting level; 0 = top-level
  uint16_t number = 0;    // ordered-list value for NumberedItem; 0 otherwise
  std::vector<InlineSpan> spans;  // inline runs for text-bearing blocks
};

// Parse inline spans from a single line of body text.
// Recognised inline syntax: **bold**, __bold__, *italic*, _italic_, `code`,
// [text](url), ![alt](src) (rendered as "[image: alt]" plain span).
// Output is appended to outSpans. Adjacent runs with the same style are
// merged into one span.
void parseInline(std::string_view line, std::vector<InlineSpan>& outSpans);

// Stateful streaming parser. Holds only the small amount of state that
// must persist across buffer boundaries (currently: whether we are
// inside a fenced code block).
class BlockParser {
 public:
  BlockParser() = default;

  // Reset to start-of-document state.
  void reset();

  // Parse one block from buffer[0..len).
  // - If the buffer ends in the middle of a block and isEof is false, the
  //   parser returns 0 to signal "need more data" without modifying outBlock.
  // - If isEof is true, the parser will close any open block at end-of-buffer.
  // - Otherwise returns bytes consumed and writes the parsed block to outBlock.
  // - Trailing blank lines between blocks are treated as separators and
  //   silently consumed.
  // - Returns 0 with outBlock.kind = Empty when there is nothing more to do.
  size_t parseBlock(const char* buffer, size_t len, bool isEof, Block& outBlock);

  bool inCodeFence() const { return _inCodeFence; }

 private:
  // Implementation helper — parses everything except the in-code-fence path.
  size_t parseBlockHelper(const char* buffer, size_t len, bool isEof, Block& outBlock);

  bool _inCodeFence = false;
};

}  // namespace md
