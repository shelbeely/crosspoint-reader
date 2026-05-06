#include "MarkdownParser.h"

#include <cctype>
#include <cstring>

namespace md {

namespace {

// Find the next newline ('\n') in buffer[pos..len). Returns len if none found.
size_t findEol(const char* buffer, size_t pos, size_t len) {
  for (size_t i = pos; i < len; ++i) {
    if (buffer[i] == '\n') return i;
  }
  return len;
}

// Trim trailing CR from a string_view (handles CRLF line endings).
std::string_view stripCr(std::string_view s) {
  if (!s.empty() && s.back() == '\r') s.remove_suffix(1);
  return s;
}

bool isBlank(std::string_view line) {
  for (char c : line) {
    if (c != ' ' && c != '\t' && c != '\r') return false;
  }
  return true;
}

// Count leading spaces (tabs counted as 4) for list indent detection.
size_t leadingIndent(std::string_view line) {
  size_t n = 0;
  for (char c : line) {
    if (c == ' ') n += 1;
    else if (c == '\t') n += 4;
    else break;
  }
  return n;
}

std::string_view ltrim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  return s;
}

std::string_view rtrim(std::string_view s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

bool isHorizontalRule(std::string_view line) {
  std::string_view s = ltrim(rtrim(line));
  if (s.size() < 3) return false;
  char c = s.front();
  if (c != '-' && c != '*' && c != '_') return false;
  size_t count = 0;
  for (char ch : s) {
    if (ch == c) ++count;
    else if (ch == ' ' || ch == '\t') continue;
    else return false;
  }
  return count >= 3;
}

bool isCodeFence(std::string_view line) {
  std::string_view s = ltrim(line);
  return s.size() >= 3 && s[0] == '`' && s[1] == '`' && s[2] == '`';
}

// Returns true if line begins with one of "- ", "* ", "+ " (with optional
// leading spaces). Also returns the indent and the position of the first
// non-marker character.
bool matchBullet(std::string_view line, uint8_t& outIndent, size_t& outContentStart) {
  size_t indent = leadingIndent(line);
  if (indent >= line.size()) return false;
  char c = line[indent];
  if (c != '-' && c != '*' && c != '+') return false;
  if (indent + 1 >= line.size() || line[indent + 1] != ' ') return false;
  outIndent = static_cast<uint8_t>(indent / 2);  // 2-space = 1 nesting level
  outContentStart = indent + 2;
  return true;
}

// Returns true if line begins with "<digits>. " (with optional leading spaces).
bool matchNumbered(std::string_view line, uint8_t& outIndent, uint16_t& outNumber, size_t& outContentStart) {
  size_t indent = leadingIndent(line);
  size_t i = indent;
  if (i >= line.size() || !isdigit(static_cast<unsigned char>(line[i]))) return false;
  uint32_t value = 0;
  size_t digits = 0;
  while (i < line.size() && isdigit(static_cast<unsigned char>(line[i]))) {
    value = value * 10 + static_cast<uint32_t>(line[i] - '0');
    ++i;
    ++digits;
    if (digits > 5) return false;  // cap absurd numbers
  }
  if (i >= line.size() || line[i] != '.') return false;
  ++i;
  if (i >= line.size() || line[i] != ' ') return false;
  outIndent = static_cast<uint8_t>(indent / 2);
  outNumber = static_cast<uint16_t>(value);
  outContentStart = i + 1;
  return true;
}

// Returns heading level (1..4) if line is "# Heading" / "## ..." / etc, else 0.
uint8_t matchHeading(std::string_view line, size_t& outContentStart) {
  size_t i = 0;
  uint8_t hashes = 0;
  while (i < line.size() && line[i] == '#') {
    ++hashes;
    ++i;
    if (hashes > 4) break;
  }
  if (hashes == 0 || hashes > 4) return 0;
  if (i >= line.size() || line[i] != ' ') return 0;
  outContentStart = i + 1;
  return hashes;
}

// Standalone image: "![alt](src)" with optional surrounding whitespace.
bool matchImageStub(std::string_view line, std::string& outAlt) {
  std::string_view s = rtrim(ltrim(line));
  if (s.size() < 5 || s[0] != '!' || s[1] != '[') return false;
  size_t closeBracket = s.find("](", 2);
  if (closeBracket == std::string_view::npos) return false;
  if (s.back() != ')') return false;
  outAlt.assign(s.data() + 2, closeBracket - 2);
  return true;
}

}  // namespace

void BlockParser::reset() { _inCodeFence = false; }

size_t BlockParser::parseBlock(const char* buffer, size_t len, bool isEof, Block& outBlock) {
  outBlock = Block{};

  if (len == 0) return 0;

  // Inside a fenced code block: emit one line per call until closing fence.
  if (_inCodeFence) {
    size_t eol = findEol(buffer, 0, len);
    if (eol == len && !isEof) return 0;  // need more data
    std::string_view raw(buffer, eol);
    std::string_view line = stripCr(raw);
    size_t consumed = (eol < len) ? eol + 1 : eol;

    if (isCodeFence(line)) {
      _inCodeFence = false;
      // Closing fence is consumed silently; recurse to get the next block.
      return consumed + parseBlockHelper(buffer + consumed, len - consumed, isEof, outBlock);
    }
    outBlock.kind = BlockKind::CodeBlock;
    outBlock.spans.push_back(InlineSpan{std::string(line), STYLE_CODE, ""});
    return consumed;
  }

  return parseBlockHelper(buffer, len, isEof, outBlock);
}

}  // namespace md

namespace md {

// Helper used by BlockParser::parseBlock. Body of all the non-fence paths.
size_t BlockParser::parseBlockHelper(const char* buffer, size_t len, bool isEof, Block& outBlock) {
  size_t pos = 0;

  // Skip blank lines between blocks.
  while (pos < len) {
    size_t eol = findEol(buffer, pos, len);
    if (eol == len && !isEof) return 0;
    std::string_view raw(buffer + pos, eol - pos);
    if (!isBlank(stripCr(raw))) break;
    pos = (eol < len) ? eol + 1 : eol;
  }
  if (pos >= len) return pos;  // only blank lines remained

  // Read the first non-blank line.
  size_t lineStart = pos;
  size_t eol = findEol(buffer, lineStart, len);
  if (eol == len && !isEof) return 0;
  std::string_view first = stripCr(std::string_view(buffer + lineStart, eol - lineStart));
  size_t consumedToEol = (eol < len) ? eol + 1 : eol;

  // 1. Fenced code start.
  if (isCodeFence(first)) {
    _inCodeFence = true;
    // The opening fence line is consumed; recurse to emit the first
    // code-block line (or close immediately if EOF).
    pos = consumedToEol;
    return pos + parseBlock(buffer + pos, len - pos, isEof, outBlock);
  }

  // 2. Horizontal rule.
  if (isHorizontalRule(first)) {
    outBlock.kind = BlockKind::HorizontalRule;
    return consumedToEol;
  }

  // 3. ATX heading.
  size_t hContent = 0;
  if (uint8_t level = matchHeading(first, hContent); level > 0) {
    outBlock.kind = static_cast<BlockKind>(static_cast<uint8_t>(BlockKind::Heading1) + (level - 1));
    std::string_view body = first.substr(hContent);
    // Strip trailing "###" closing marker if present.
    body = rtrim(body);
    while (!body.empty() && body.back() == '#') body.remove_suffix(1);
    body = rtrim(body);
    parseInline(body, outBlock.spans);
    return consumedToEol;
  }

  // 4. Standalone image stub.
  std::string altText;
  if (matchImageStub(first, altText)) {
    outBlock.kind = BlockKind::ImageStub;
    InlineSpan s;
    s.text = "[image: " + altText + "]";
    s.style = STYLE_NONE;
    outBlock.spans.push_back(std::move(s));
    return consumedToEol;
  }

  // 5. Blockquote: consume one line per call (kept simple v1).
  if (!first.empty() && first[0] == '>') {
    outBlock.kind = BlockKind::Quote;
    std::string_view body = first.substr(1);
    if (!body.empty() && body.front() == ' ') body.remove_prefix(1);
    parseInline(body, outBlock.spans);
    return consumedToEol;
  }

  // 6. Bullet list item.
  uint8_t indent = 0;
  size_t contentStart = 0;
  if (matchBullet(first, indent, contentStart)) {
    outBlock.kind = BlockKind::BulletItem;
    outBlock.indent = indent;
    parseInline(first.substr(contentStart), outBlock.spans);
    return consumedToEol;
  }

  // 7. Numbered list item.
  uint16_t number = 0;
  if (matchNumbered(first, indent, number, contentStart)) {
    outBlock.kind = BlockKind::NumberedItem;
    outBlock.indent = indent;
    outBlock.number = number;
    parseInline(first.substr(contentStart), outBlock.spans);
    return consumedToEol;
  }

  // 8. Paragraph — gather contiguous non-blank, non-special lines.
  outBlock.kind = BlockKind::Paragraph;
  std::string accum(first);

  pos = consumedToEol;
  while (pos < len) {
    size_t lookEol = findEol(buffer, pos, len);
    if (lookEol == len && !isEof) {
      // Need more data to determine paragraph end. Be conservative: emit
      // what we have so far and let the caller refill on the next call.
      // Note: this means a paragraph may end early if it spans buffer
      // boundaries. For the reader's chunked I/O this is acceptable —
      // worst case is an unintended paragraph break.
      break;
    }
    std::string_view next = stripCr(std::string_view(buffer + pos, lookEol - pos));
    if (isBlank(next)) {
      pos = (lookEol < len) ? lookEol + 1 : lookEol;
      break;
    }
    // Stop at any line that would itself start a different block kind.
    size_t dummyContent = 0;
    uint8_t dummyIndent = 0;
    uint16_t dummyNumber = 0;
    if (isHorizontalRule(next) || isCodeFence(next) || matchHeading(next, dummyContent) > 0 ||
        (!next.empty() && next[0] == '>') || matchBullet(next, dummyIndent, dummyContent) ||
        matchNumbered(next, dummyIndent, dummyNumber, dummyContent)) {
      break;
    }
    accum.push_back(' ');
    accum.append(next);
    pos = (lookEol < len) ? lookEol + 1 : lookEol;
  }

  parseInline(accum, outBlock.spans);
  return pos;
}

namespace {

// Append text to outSpans, merging with the previous span if styles match.
void emit(std::vector<InlineSpan>& outSpans, std::string_view text, uint8_t style, std::string_view href = {}) {
  if (text.empty()) return;
  if (!outSpans.empty() && outSpans.back().style == style && outSpans.back().href == href) {
    outSpans.back().text.append(text.data(), text.size());
    return;
  }
  InlineSpan s;
  s.text.assign(text.data(), text.size());
  s.style = style;
  s.href.assign(href.data(), href.size());
  outSpans.push_back(std::move(s));
}

// Try to match a delimiter run starting at i. Returns the number of
// delimiter chars (1, 2) if matched, else 0. Sets outStyle.
size_t matchDelim(std::string_view s, size_t i, uint8_t& outStyle) {
  if (i >= s.size()) return 0;
  char c = s[i];
  if (c == '*' || c == '_') {
    if (i + 1 < s.size() && s[i + 1] == c) {
      outStyle = STYLE_BOLD;
      return 2;
    }
    outStyle = STYLE_ITALIC;
    return 1;
  }
  return 0;
}

// Find a closing delimiter run of exactly `runLen` chars `c` in s[from..),
// returning its start position or std::string_view::npos.
size_t findClosing(std::string_view s, size_t from, char c, size_t runLen) {
  size_t i = from;
  while (i < s.size()) {
    if (s[i] == c) {
      size_t run = 0;
      while (i + run < s.size() && s[i + run] == c) ++run;
      if (run == runLen) return i;
      // Skip past mismatched-length runs to avoid pathological backtracking.
      i += run;
    } else {
      ++i;
    }
  }
  return std::string_view::npos;
}

}  // namespace

void parseInline(std::string_view line, std::vector<InlineSpan>& outSpans) {
  size_t i = 0;
  uint8_t baseStyle = STYLE_NONE;
  // We support one level of nesting: an outer **bold** or *italic* run with
  // possible inner `code` / link. Nested bold-inside-italic etc. is not
  // recognised; the inner delimiter is rendered literally. This keeps the
  // parser O(n) and simple, matches typical issue/PR body content.

  while (i < line.size()) {
    char c = line[i];

    // Inline code: `text`
    if (c == '`') {
      size_t close = line.find('`', i + 1);
      if (close == std::string_view::npos) {
        emit(outSpans, line.substr(i, 1), baseStyle);
        ++i;
        continue;
      }
      emit(outSpans, line.substr(i + 1, close - i - 1), static_cast<uint8_t>(baseStyle | STYLE_CODE));
      i = close + 1;
      continue;
    }

    // Image: ![alt](src) — rendered as "[image: alt]" plain text inline.
    if (c == '!' && i + 1 < line.size() && line[i + 1] == '[') {
      size_t closeBracket = line.find("](", i + 2);
      size_t closeParen = (closeBracket == std::string_view::npos) ? std::string_view::npos
                                                                    : line.find(')', closeBracket + 2);
      if (closeBracket != std::string_view::npos && closeParen != std::string_view::npos) {
        std::string_view alt = line.substr(i + 2, closeBracket - (i + 2));
        std::string rendered = "[image: ";
        rendered.append(alt.data(), alt.size());
        rendered.push_back(']');
        emit(outSpans, rendered, baseStyle);
        i = closeParen + 1;
        continue;
      }
    }

    // Link: [text](href)
    if (c == '[') {
      size_t closeBracket = line.find("](", i + 1);
      size_t closeParen = (closeBracket == std::string_view::npos) ? std::string_view::npos
                                                                    : line.find(')', closeBracket + 2);
      if (closeBracket != std::string_view::npos && closeParen != std::string_view::npos) {
        std::string_view text = line.substr(i + 1, closeBracket - (i + 1));
        std::string_view href = line.substr(closeBracket + 2, closeParen - (closeBracket + 2));
        emit(outSpans, text, static_cast<uint8_t>(baseStyle | STYLE_LINK), href);
        i = closeParen + 1;
        continue;
      }
    }

    // Emphasis: *italic*, **bold**, _italic_, __bold__
    uint8_t delimStyle = 0;
    if (size_t runLen = matchDelim(line, i, delimStyle); runLen > 0) {
      size_t close = findClosing(line, i + runLen, c, runLen);
      if (close != std::string_view::npos && close > i + runLen) {
        emit(outSpans, line.substr(i + runLen, close - (i + runLen)),
             static_cast<uint8_t>(baseStyle | delimStyle));
        i = close + runLen;
        continue;
      }
    }

    // Plain character (or unmatched delimiter rendered literally).
    size_t runEnd = i + 1;
    while (runEnd < line.size() && line[runEnd] != '`' && line[runEnd] != '*' && line[runEnd] != '_' &&
           line[runEnd] != '[' && line[runEnd] != '!') {
      ++runEnd;
    }
    emit(outSpans, line.substr(i, runEnd - i), baseStyle);
    i = runEnd;
  }
}

}  // namespace md
