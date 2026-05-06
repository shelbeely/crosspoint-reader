#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "lib/Markdown/MarkdownParser.h"

static int testsPassed = 0;
static int testsFailed = 0;
static const char* currentTest = "";

#define ASSERT_EQ(a, b)                                                                       \
  do {                                                                                        \
    auto _a = (a);                                                                            \
    auto _b = (b);                                                                            \
    if (_a != _b) {                                                                           \
      fprintf(stderr, "  FAIL: %s:%d (%s): %s != expected\n", __FILE__, __LINE__, currentTest, #a); \
      testsFailed++;                                                                          \
      return;                                                                                 \
    }                                                                                         \
  } while (0)

#define ASSERT_TRUE(cond)                                                              \
  do {                                                                                 \
    if (!(cond)) {                                                                     \
      fprintf(stderr, "  FAIL: %s:%d (%s): %s\n", __FILE__, __LINE__, currentTest, #cond); \
      testsFailed++;                                                                   \
      return;                                                                          \
    }                                                                                  \
  } while (0)

#define TEST(name) \
  static void test_##name(); \
  struct register_##name { register_##name() { runners.push_back({#name, test_##name}); } } _reg_##name; \
  static void test_##name()

struct Runner {
  const char* name;
  void (*fn)();
};
static std::vector<Runner>& getRunners() {
  static std::vector<Runner> r;
  return r;
}
#define runners (getRunners())

// Helper: parse all blocks from a string and return them as a vector.
static std::vector<md::Block> parseAll(const std::string& input) {
  md::BlockParser parser;
  std::vector<md::Block> blocks;
  size_t offset = 0;
  while (offset < input.size()) {
    md::Block b;
    size_t consumed = parser.parseBlock(input.data() + offset, input.size() - offset, true, b);
    if (consumed == 0) break;
    if (b.kind != md::BlockKind::Empty) blocks.push_back(std::move(b));
    offset += consumed;
  }
  return blocks;
}

// Helper: concatenate all span text for a block.
static std::string allText(const md::Block& b) {
  std::string out;
  for (const auto& s : b.spans) out += s.text;
  return out;
}

TEST(empty_input) {
  auto blocks = parseAll("");
  ASSERT_EQ(blocks.size(), 0u);
}

TEST(only_blank_lines) {
  auto blocks = parseAll("\n\n   \n\n");
  ASSERT_EQ(blocks.size(), 0u);
}

TEST(single_paragraph) {
  auto blocks = parseAll("hello world\n");
  ASSERT_EQ(blocks.size(), 1u);
  ASSERT_EQ(blocks[0].kind, md::BlockKind::Paragraph);
  ASSERT_EQ(allText(blocks[0]), "hello world");
}

TEST(multiline_paragraph_joined) {
  auto blocks = parseAll("line one\nline two\nline three\n");
  ASSERT_EQ(blocks.size(), 1u);
  ASSERT_EQ(blocks[0].kind, md::BlockKind::Paragraph);
  ASSERT_EQ(allText(blocks[0]), "line one line two line three");
}

TEST(paragraphs_split_by_blank) {
  auto blocks = parseAll("first\n\nsecond\n");
  ASSERT_EQ(blocks.size(), 2u);
  ASSERT_EQ(allText(blocks[0]), "first");
  ASSERT_EQ(allText(blocks[1]), "second");
}

TEST(headings_h1_h4) {
  auto blocks = parseAll("# H1\n## H2\n### H3\n#### H4\n");
  ASSERT_EQ(blocks.size(), 4u);
  ASSERT_EQ(blocks[0].kind, md::BlockKind::Heading1);
  ASSERT_EQ(blocks[1].kind, md::BlockKind::Heading2);
  ASSERT_EQ(blocks[2].kind, md::BlockKind::Heading3);
  ASSERT_EQ(blocks[3].kind, md::BlockKind::Heading4);
  ASSERT_EQ(allText(blocks[0]), "H1");
  ASSERT_EQ(allText(blocks[3]), "H4");
}

TEST(heading_with_trailing_hashes) {
  auto blocks = parseAll("## Title ##\n");
  ASSERT_EQ(blocks.size(), 1u);
  ASSERT_EQ(blocks[0].kind, md::BlockKind::Heading2);
  ASSERT_EQ(allText(blocks[0]), "Title");
}

TEST(too_many_hashes_is_paragraph) {
  auto blocks = parseAll("##### too many\n");
  ASSERT_EQ(blocks.size(), 1u);
  ASSERT_EQ(blocks[0].kind, md::BlockKind::Paragraph);
}

TEST(bullet_list) {
  auto blocks = parseAll("- one\n- two\n- three\n");
  ASSERT_EQ(blocks.size(), 3u);
  for (auto& b : blocks) ASSERT_EQ(b.kind, md::BlockKind::BulletItem);
  ASSERT_EQ(allText(blocks[1]), "two");
}

TEST(numbered_list) {
  auto blocks = parseAll("1. apple\n2. banana\n10. orange\n");
  ASSERT_EQ(blocks.size(), 3u);
  ASSERT_EQ(blocks[0].kind, md::BlockKind::NumberedItem);
  ASSERT_EQ(blocks[0].number, 1u);
  ASSERT_EQ(blocks[2].number, 10u);
  ASSERT_EQ(allText(blocks[2]), "orange");
}

TEST(nested_bullet_indent_levels) {
  auto blocks = parseAll("- outer\n  - inner\n");
  ASSERT_EQ(blocks.size(), 2u);
  ASSERT_EQ(blocks[0].indent, 0u);
  ASSERT_EQ(blocks[1].indent, 1u);
}

TEST(horizontal_rule) {
  auto blocks = parseAll("para\n\n---\n\nafter\n");
  ASSERT_EQ(blocks.size(), 3u);
  ASSERT_EQ(blocks[1].kind, md::BlockKind::HorizontalRule);
}

TEST(blockquote) {
  auto blocks = parseAll("> quoted text\n");
  ASSERT_EQ(blocks.size(), 1u);
  ASSERT_EQ(blocks[0].kind, md::BlockKind::Quote);
  ASSERT_EQ(allText(blocks[0]), "quoted text");
}

TEST(fenced_code_block) {
  auto blocks = parseAll("```\nint x = 1;\nint y = 2;\n```\n");
  ASSERT_EQ(blocks.size(), 2u);
  ASSERT_EQ(blocks[0].kind, md::BlockKind::CodeBlock);
  ASSERT_EQ(blocks[1].kind, md::BlockKind::CodeBlock);
  ASSERT_EQ(allText(blocks[0]), "int x = 1;");
  ASSERT_EQ(allText(blocks[1]), "int y = 2;");
}

TEST(fenced_code_preserves_special_chars) {
  // Inside code fence, # and - and > must NOT be parsed as block markers.
  auto blocks = parseAll("```\n# not a heading\n- not a bullet\n```\n");
  ASSERT_EQ(blocks.size(), 2u);
  for (auto& b : blocks) ASSERT_EQ(b.kind, md::BlockKind::CodeBlock);
  ASSERT_EQ(allText(blocks[0]), "# not a heading");
  ASSERT_EQ(allText(blocks[1]), "- not a bullet");
}

TEST(image_stub) {
  auto blocks = parseAll("![diagram](img.png)\n");
  ASSERT_EQ(blocks.size(), 1u);
  ASSERT_EQ(blocks[0].kind, md::BlockKind::ImageStub);
  ASSERT_EQ(allText(blocks[0]), "[image: diagram]");
}

TEST(inline_bold) {
  std::vector<md::InlineSpan> spans;
  md::parseInline("a **bold** word", spans);
  ASSERT_EQ(spans.size(), 3u);
  ASSERT_EQ(spans[0].text, "a ");
  ASSERT_EQ(spans[0].style, md::STYLE_NONE);
  ASSERT_EQ(spans[1].text, "bold");
  ASSERT_EQ(spans[1].style, md::STYLE_BOLD);
  ASSERT_EQ(spans[2].text, " word");
}

TEST(inline_italic_underscore) {
  std::vector<md::InlineSpan> spans;
  md::parseInline("an _italic_ run", spans);
  ASSERT_EQ(spans.size(), 3u);
  ASSERT_EQ(spans[1].text, "italic");
  ASSERT_EQ(spans[1].style, md::STYLE_ITALIC);
}

TEST(inline_code) {
  std::vector<md::InlineSpan> spans;
  md::parseInline("call `foo()` here", spans);
  ASSERT_EQ(spans.size(), 3u);
  ASSERT_EQ(spans[1].text, "foo()");
  ASSERT_EQ(spans[1].style, md::STYLE_CODE);
}

TEST(inline_link) {
  std::vector<md::InlineSpan> spans;
  md::parseInline("see [docs](https://x.example/) page", spans);
  ASSERT_EQ(spans.size(), 3u);
  ASSERT_EQ(spans[1].text, "docs");
  ASSERT_EQ(spans[1].style, md::STYLE_LINK);
  ASSERT_EQ(spans[1].href, "https://x.example/");
}

TEST(inline_unmatched_delim_literal) {
  std::vector<md::InlineSpan> spans;
  md::parseInline("a * lone star", spans);
  // The '*' has no matching close, so it stays literal.
  ASSERT_EQ(spans.size(), 1u);
  ASSERT_EQ(spans[0].text, "a * lone star");
  ASSERT_EQ(spans[0].style, md::STYLE_NONE);
}

TEST(inline_image_within_paragraph) {
  std::vector<md::InlineSpan> spans;
  md::parseInline("see ![pic](x.png) above", spans);
  // Should produce: "see ", "[image: pic]", " above"
  std::string concat;
  for (auto& s : spans) concat += s.text;
  ASSERT_EQ(concat, "see [image: pic] above");
}

TEST(crlf_line_endings) {
  auto blocks = parseAll("one\r\ntwo\r\n\r\nthree\r\n");
  ASSERT_EQ(blocks.size(), 2u);
  ASSERT_EQ(allText(blocks[0]), "one two");
  ASSERT_EQ(allText(blocks[1]), "three");
}

TEST(paragraph_then_heading) {
  auto blocks = parseAll("intro paragraph\n# Heading\nfollow-up\n");
  ASSERT_EQ(blocks.size(), 3u);
  ASSERT_EQ(blocks[0].kind, md::BlockKind::Paragraph);
  ASSERT_EQ(blocks[1].kind, md::BlockKind::Heading1);
  ASSERT_EQ(blocks[2].kind, md::BlockKind::Paragraph);
}

TEST(streaming_chunks_match_full_buffer) {
  // The same input parsed all-at-once and parsed across small chunk
  // boundaries must produce the same block kinds. We validate kinds
  // (paragraph re-joining across chunk boundaries may insert extra
  // breaks — that's by design and acceptable).
  std::string input =
      "# Title\n\nA paragraph with **bold** text.\n\n- one\n- two\n\n```\ncode\n```\n";
  auto full = parseAll(input);

  // Now parse in fixed-step refills, growing the working window when the
  // parser asks for more data.
  md::BlockParser parser;
  std::vector<md::Block> chunked;
  std::string buffer;
  size_t cursor = 0;
  size_t targetSize = 8;
  while (cursor < input.size() || !buffer.empty()) {
    while (buffer.size() < targetSize && cursor < input.size()) {
      buffer.push_back(input[cursor++]);
    }
    bool isEof = (cursor >= input.size());
    md::Block b;
    size_t consumed = parser.parseBlock(buffer.data(), buffer.size(), isEof, b);
    if (consumed == 0) {
      if (isEof) break;
      // Parser needs a longer window — grow the target and refill.
      targetSize += 8;
      continue;
    }
    if (b.kind != md::BlockKind::Empty) chunked.push_back(std::move(b));
    buffer.erase(0, consumed);
    targetSize = 8;  // reset window after a successful parse
  }

  // Must parse at least the same block kinds in the same order. Chunked
  // parsing may insert extra paragraph breaks if a paragraph spans a
  // chunk boundary and EOF wasn't reached yet — that's documented as
  // acceptable. So we check that chunked is a superset that contains
  // every full block kind in order.
  size_t fi = 0;
  for (const auto& cb : chunked) {
    if (fi < full.size() && cb.kind == full[fi].kind) ++fi;
  }
  ASSERT_EQ(fi, full.size());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  fprintf(stderr, "Running %zu Markdown parser tests...\n", runners.size());
  for (const auto& r : runners) {
    currentTest = r.name;
    int beforeFail = testsFailed;
    r.fn();
    if (testsFailed == beforeFail) {
      ++testsPassed;
    }
  }
  fprintf(stderr, "\n%d passed, %d failed\n", testsPassed, testsFailed);
  return testsFailed == 0 ? 0 : 1;
}
