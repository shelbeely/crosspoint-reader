#pragma once

#include <Markdown.h>
#include <MarkdownParser.h>

#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"

// Markdown reader activity.
//
// Modelled on `TxtReaderActivity`: same lifecycle, same per-page file-offset
// cache (`.crosspoint/md_<hash>/index.bin`), same progress file
// (`progress.bin`), same status bar. Differences:
//   - Content is parsed through `lib/Markdown::BlockParser` rather than
//     treated as a flat sequence of lines.
//   - Each block lowers to one or more wrapped rendered rows. A row knows
//     its left indent (for lists/quotes) and the inline runs it should draw
//     (text + EpdFontFamily::Style + underline flag for links).
//   - Pages break at block boundaries in v1. A single block that exceeds
//     the viewport height renders to one page and is clipped — see
//     SCOPE.md "Markdown features beyond v1" for the deferred work.
class MarkdownReaderActivity final : public Activity {
 public:
  // One drawable run within a row — produced by the inline parser plus a
  // pixel-space x position assigned during wrapping.
  struct Run {
    std::string text;
    uint8_t style = 0;     // EpdFontFamily::Style bitmask
    bool underline = false;
    int xOffset = 0;       // x position relative to the row's left edge
  };

  struct Row {
    std::vector<Run> runs;
    uint8_t indentPx = 0;  // additional left indent in pixels
    bool isRule = false;   // horizontal rule line (drawn as a thin line)
    bool extraSpacing = false;  // paragraph/heading separator below this row
  };

  explicit MarkdownReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  std::unique_ptr<Markdown> doc)
      : Activity("MdReader", renderer, mappedInput), doc(std::move(doc)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  ScreenshotInfo getScreenshotInfo() const override;

 private:
  std::unique_ptr<Markdown> doc;

  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;

  std::vector<size_t> pageOffsets;  // file offset of first block on each page
  std::vector<Row> currentPageRows;

  int viewportWidth = 0;
  int viewportHeight = 0;
  int lineHeight = 0;
  int paragraphSpacing = 0;
  bool initialized = false;

  // Cached settings for cache validation.
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  void initializeReader();
  void renderPage();
  void renderStatusBar() const;

  // Parse and lay out blocks starting at `startOffset` until the viewport
  // is full or end-of-file is reached. Sets outRows and outNextOffset.
  bool loadPageAtOffset(size_t startOffset, std::vector<Row>& outRows, size_t& outNextOffset);

  // Convert a parsed Block into one or more rendered Rows, wrapped to
  // viewport width. Returned rows are appended to outRows.
  void layoutBlock(const md::Block& block, std::vector<Row>& outRows) const;

  void buildPageIndex();
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  void saveProgress() const;
  void loadProgress();
};
