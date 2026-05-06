#include "MarkdownReaderActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr size_t CHUNK_SIZE = 8 * 1024;
constexpr uint32_t CACHE_MAGIC = 0x4D444B49;  // "MDKI"
constexpr uint8_t CACHE_VERSION = 1;          // bump when format below changes

// Translate the BlockParser's inline style bitmask into the EpdFontFamily
// Style enum value used by the renderer. Underline is reported separately
// because it composes orthogonally with the weight/italic axis.
EpdFontFamily::Style toFontStyle(uint8_t spanStyle) {
  const bool bold = (spanStyle & md::STYLE_BOLD) != 0;
  // Inline code is rendered as italic-bold so it stands out from prose
  // without requiring a separate monospace font (none is available in
  // SINGLE_BUFFER_MODE — see SCOPE.md image/font budget).
  const bool italic = (spanStyle & (md::STYLE_ITALIC | md::STYLE_CODE)) != 0;
  if (bold && italic) return EpdFontFamily::BOLD_ITALIC;
  if (bold) return EpdFontFamily::BOLD;
  if (italic) return EpdFontFamily::ITALIC;
  return EpdFontFamily::REGULAR;
}

// Map a heading block's BlockKind to a base style for its row(s). All four
// heading levels render as BOLD with the reader's normal font; visual
// hierarchy comes from leading/trailing spacing, not font size, to keep
// the page index cache stable across font changes.
EpdFontFamily::Style headingBaseStyle(md::BlockKind /*kind*/) { return EpdFontFamily::BOLD; }

// Indentation per list nesting level, in pixels.
constexpr int LIST_INDENT_PX = 20;
// Pixels reserved for a bullet/number marker before the wrapped content.
constexpr int MARKER_GUTTER_PX = 16;
// Pixels reserved for the blockquote vertical bar.
constexpr int QUOTE_GUTTER_PX = 12;

// True if the block uses a fixed style for all its inline spans (its
// `baseStyle` overrides per-span styling). Headings render uniformly bold;
// code blocks render italic and verbatim. Body blocks (paragraphs, lists,
// quotes) instead use per-span styles.
bool blockHasFixedStyle(md::BlockKind kind) {
  switch (kind) {
    case md::BlockKind::Heading1:
    case md::BlockKind::Heading2:
    case md::BlockKind::Heading3:
    case md::BlockKind::Heading4:
    case md::BlockKind::CodeBlock:
      return true;
    default:
      return false;
  }
}

}  // namespace

void MarkdownReaderActivity::onEnter() {
  Activity::onEnter();

  if (!doc) {
    return;
  }

  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  doc->setupCacheDir();

  const auto& filePath = doc->getPath();
  auto fileName = filePath.substr(filePath.rfind('/') + 1);
  APP_STATE.openEpubPath = filePath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(filePath, fileName, "", "");

  requestUpdate();
}

void MarkdownReaderActivity::onExit() {
  Activity::onExit();

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  pageOffsets.clear();
  currentPageRows.clear();
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  doc.reset();
}

void MarkdownReaderActivity::loop() {
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    activityManager.goToFileBrowser(doc ? doc->getPath() : "");
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    onGoHome();
    return;
  }

  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  if (prevTriggered && currentPage > 0) {
    currentPage--;
    requestUpdate();
  } else if (nextTriggered) {
    if (currentPage < totalPages - 1) {
      currentPage++;
      requestUpdate();
    } else {
      onGoHome();
    }
  }
}

void MarkdownReaderActivity::initializeReader() {
  if (initialized) return;

  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;

  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom +=
      std::max(cachedScreenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  lineHeight = renderer.getLineHeight(cachedFontId);
  if (lineHeight < 1) lineHeight = 1;
  paragraphSpacing = lineHeight / 2;

  LOG_DBG("MDR", "Viewport: %dx%d, lineHeight: %d", viewportWidth, viewportHeight, lineHeight);

  if (!loadPageIndexCache()) {
    buildPageIndex();
    savePageIndexCache();
  }

  loadProgress();
  initialized = true;
}

void MarkdownReaderActivity::layoutBlock(const md::Block& block, std::vector<Row>& outRows) const {
  if (block.kind == md::BlockKind::Empty) return;

  // Resolve indentation and prefix text once per block.
  uint8_t indentPx = 0;
  std::string prefix;       // text drawn before the block's first row
  EpdFontFamily::Style baseStyle = EpdFontFamily::REGULAR;
  bool addLeadingBlank = false;
  bool addTrailingBlank = false;

  switch (block.kind) {
    case md::BlockKind::HorizontalRule: {
      Row r;
      r.isRule = true;
      r.extraSpacing = true;
      outRows.push_back(std::move(r));
      return;
    }
    case md::BlockKind::ImageStub:
      // Falls through to paragraph-style rendering with no special prefix.
      addTrailingBlank = true;
      break;
    case md::BlockKind::Heading1:
    case md::BlockKind::Heading2:
    case md::BlockKind::Heading3:
    case md::BlockKind::Heading4: {
      baseStyle = headingBaseStyle(block.kind);
      // Heading1/2 get a leading blank for stronger hierarchy; H3/H4 only trailing.
      addLeadingBlank = (block.kind == md::BlockKind::Heading1 || block.kind == md::BlockKind::Heading2);
      addTrailingBlank = true;
      break;
    }
    case md::BlockKind::CodeBlock: {
      baseStyle = EpdFontFamily::ITALIC;
      indentPx = LIST_INDENT_PX;
      break;
    }
    case md::BlockKind::Quote: {
      indentPx = QUOTE_GUTTER_PX;
      addTrailingBlank = false;
      break;
    }
    case md::BlockKind::BulletItem: {
      indentPx = LIST_INDENT_PX * (block.indent + 1);
      prefix = "\xE2\x80\xA2 ";  // U+2022 BULLET, UTF-8
      break;
    }
    case md::BlockKind::NumberedItem: {
      indentPx = LIST_INDENT_PX * (block.indent + 1);
      char buf[12];
      snprintf(buf, sizeof(buf), "%u. ", static_cast<unsigned>(block.number));
      prefix = buf;
      break;
    }
    case md::BlockKind::Paragraph:
    default:
      addTrailingBlank = true;
      break;
  }

  if (addLeadingBlank) {
    Row blank;
    outRows.push_back(std::move(blank));
  }

  const int rowAvailableWidth = std::max(1, viewportWidth - indentPx);

  // Word-wrap the inline spans into rows.
  // Strategy: walk spans token-by-token (split on spaces). For each token,
  // measure its width (with the span's resolved Style) and decide whether
  // it fits on the current row. If not, flush the current row and start a
  // new one. This keeps state small and preserves per-span styling.
  Row currentRow;
  currentRow.indentPx = indentPx;
  int currentRowWidth = 0;

  auto flushRow = [&]() {
    outRows.push_back(std::move(currentRow));
    currentRow = Row{};
    currentRow.indentPx = indentPx;
    currentRowWidth = 0;
  };

  // Helper: emit a token (string + style + underline) at currentRowWidth.
  // Returns true if the token fit (or was forcibly placed); false on error.
  auto emitToken = [&](const std::string& text, EpdFontFamily::Style style, bool underline) {
    if (text.empty()) return;
    int w = renderer.getTextWidth(cachedFontId, text.c_str(), style);

    // If the row already has content and adding this token would overflow,
    // wrap to a new row first.
    if (currentRowWidth > 0 && currentRowWidth + w > rowAvailableWidth) {
      flushRow();
    }

    Run r;
    r.text = text;
    r.style = static_cast<uint8_t>(style);
    r.underline = underline;
    r.xOffset = currentRowWidth;
    currentRow.runs.push_back(std::move(r));
    currentRowWidth += w;
  };

  // Emit prefix (bullet, number) on the first row only.
  if (!prefix.empty()) {
    Run r;
    r.text = prefix;
    r.style = static_cast<uint8_t>(EpdFontFamily::REGULAR);
    r.xOffset = 0;
    int prefixW = renderer.getTextWidth(cachedFontId, prefix.c_str(), EpdFontFamily::REGULAR);
    currentRow.runs.push_back(std::move(r));
    currentRowWidth = std::max(prefixW, MARKER_GUTTER_PX);
  }

  // Emit each span as whitespace-separated tokens.
  for (const auto& span : block.spans) {
    EpdFontFamily::Style style = baseStyle;
    if (!blockHasFixedStyle(block.kind)) {
      // Body text: the span's style overrides the block base.
      style = toFontStyle(span.style);
    } else if (block.kind != md::BlockKind::CodeBlock && (span.style & md::STYLE_CODE)) {
      // Within a heading, allow inline `code` to render bold-italic.
      style = EpdFontFamily::BOLD_ITALIC;
    }
    bool underline = (span.style & md::STYLE_LINK) != 0;

    // Fenced code preserves whitespace verbatim (one block per source line),
    // so emit as a single non-wrapping token. If it overflows the row it
    // will be clipped — acceptable for v1.
    if (block.kind == md::BlockKind::CodeBlock) {
      emitToken(span.text, style, underline);
      continue;
    }

    // Tokenize on space.
    size_t i = 0;
    while (i < span.text.size()) {
      // Skip leading spaces — collapse to single inter-word space when
      // there's already content on the row.
      size_t spaceStart = i;
      while (i < span.text.size() && span.text[i] == ' ') ++i;
      bool sawSpace = (i > spaceStart);
      if (sawSpace && currentRowWidth > (prefix.empty() ? 0 : MARKER_GUTTER_PX)) {
        // Add a single space using the current style.
        Run sp;
        sp.text = " ";
        sp.style = static_cast<uint8_t>(style);
        sp.underline = underline;
        sp.xOffset = currentRowWidth;
        int sw = renderer.getTextWidth(cachedFontId, " ", style);
        // If row is already overflowing, just drop the space — wrap will
        // happen on the next non-space token.
        if (currentRowWidth + sw <= rowAvailableWidth) {
          currentRow.runs.push_back(std::move(sp));
          currentRowWidth += sw;
        }
      }

      size_t tokStart = i;
      while (i < span.text.size() && span.text[i] != ' ') ++i;
      if (i == tokStart) continue;
      std::string token = span.text.substr(tokStart, i - tokStart);
      emitToken(token, style, underline);
    }
  }

  // Flush trailing row. Skip if the block produced no visible content
  // (e.g., a paragraph that parsed to zero spans).
  if (!currentRow.runs.empty()) {
    if (addTrailingBlank) currentRow.extraSpacing = true;
    outRows.push_back(std::move(currentRow));
  } else if (addTrailingBlank && !outRows.empty()) {
    outRows.back().extraSpacing = true;
  }
}

bool MarkdownReaderActivity::loadPageAtOffset(size_t startOffset, std::vector<Row>& outRows, size_t& outNextOffset) {
  outRows.clear();
  outNextOffset = startOffset;

  const size_t fileSize = doc->getFileSize();
  if (startOffset >= fileSize) {
    return false;
  }

  // Stream the file in CHUNK_SIZE windows. After parsing each block, the
  // BlockParser tells us how many bytes were consumed; we advance the
  // file offset accordingly. When the parser asks for more data (returns
  // 0 with isEof=false) we slide the chunk window forward.
  md::BlockParser parser;
  size_t fileOffset = startOffset;
  size_t windowSize = std::min(CHUNK_SIZE, fileSize - fileOffset);

  // Reusable chunk buffer. `+1` for a defensive trailing NUL.
  auto* buffer = static_cast<uint8_t*>(malloc(CHUNK_SIZE + 1));
  if (!buffer) {
    LOG_ERR("MDR", "Failed to allocate chunk buffer (%zu bytes)", CHUNK_SIZE + 1);
    return false;
  }

  size_t bytesRead = 0;
  if (!doc->readContent(buffer, fileOffset, windowSize, bytesRead) || bytesRead == 0) {
    free(buffer);
    return false;
  }
  buffer[bytesRead] = '\0';

  int usedHeight = 0;
  bool madeProgress = false;

  while (fileOffset < fileSize) {
    md::Block block;
    bool isEof = (fileOffset + bytesRead >= fileSize);
    size_t consumed = parser.parseBlock(reinterpret_cast<const char*>(buffer), bytesRead, isEof, block);

    if (consumed == 0) {
      // Parser needs more data. Slide the window forward (rare in practice
      // — most blocks fit in 8 KB).
      if (isEof) {
        // No more bytes available and parser still couldn't finish — bail.
        break;
      }
      // Re-read a larger window starting from current offset.
      size_t newWindow = std::min(CHUNK_SIZE, fileSize - fileOffset);
      if (newWindow == bytesRead) {
        // The block does not fit even in CHUNK_SIZE. This is a pathological
        // input — a single Markdown block larger than 8 KB is unusual but
        // possible (e.g., a multi-megabyte fenced code block with no
        // newlines). Skip one byte to guarantee forward progress and log
        // it so the situation is visible during debugging.
        LOG_ERR("MDR", "Block exceeds %zu-byte chunk at offset %zu; advancing 1 byte to avoid stall",
                CHUNK_SIZE, fileOffset);
        fileOffset += 1;
        if (fileOffset >= fileSize) break;
        newWindow = std::min(CHUNK_SIZE, fileSize - fileOffset);
      }
      if (!doc->readContent(buffer, fileOffset, newWindow, bytesRead) || bytesRead == 0) break;
      buffer[bytesRead] = '\0';
      windowSize = newWindow;
      continue;
    }

    if (block.kind == md::BlockKind::Empty) {
      // Whitespace consumed; advance and continue without emitting rows.
      // We do NOT reset the parser — its only persistent state is
      // `_inCodeFence`, which must survive blank-line consumption.
      fileOffset += consumed;
      if (fileOffset >= fileSize) break;
      // Slide window.
      windowSize = std::min(CHUNK_SIZE, fileSize - fileOffset);
      if (!doc->readContent(buffer, fileOffset, windowSize, bytesRead) || bytesRead == 0) break;
      buffer[bytesRead] = '\0';
      continue;
    }

    // Lay out this block into rows. If adding them would exceed viewport
    // height AND we already have at least one row on the page, stop here
    // and start the next page at this block's file offset.
    std::vector<Row> blockRows;
    layoutBlock(block, blockRows);

    int blockHeight = 0;
    for (const auto& r : blockRows) {
      blockHeight += lineHeight;
      if (r.extraSpacing) blockHeight += paragraphSpacing;
    }

    if (madeProgress && usedHeight + blockHeight > viewportHeight && !parser.inCodeFence()) {
      // Page is full. Do NOT consume this block; report it as the next
      // page's start offset.
      // We refuse to break inside a fenced code block because the next
      // call to loadPageAtOffset() starts with a fresh BlockParser whose
      // `_inCodeFence` is false — splitting a fence here would cause the
      // remaining code lines to be reparsed as Markdown blocks.
      outNextOffset = fileOffset;
      free(buffer);
      return true;
    }

    // Append (or, if the block alone is taller than the viewport, append
    // anyway — it will be clipped — and end the page).
    for (auto& r : blockRows) outRows.push_back(std::move(r));
    usedHeight += blockHeight;
    madeProgress = true;
    fileOffset += consumed;

    if (usedHeight >= viewportHeight && !parser.inCodeFence()) {
      outNextOffset = fileOffset;
      free(buffer);
      return true;
    }

    // Slide window for next block.
    if (fileOffset >= fileSize) break;
    windowSize = std::min(CHUNK_SIZE, fileSize - fileOffset);
    if (!doc->readContent(buffer, fileOffset, windowSize, bytesRead) || bytesRead == 0) break;
    buffer[bytesRead] = '\0';

    // Yield occasionally during indexing.
    if (outRows.size() % 32 == 0) vTaskDelay(1);
  }

  free(buffer);
  outNextOffset = fileOffset;
  return madeProgress;
}

void MarkdownReaderActivity::buildPageIndex() {
  pageOffsets.clear();
  pageOffsets.push_back(0);

  GUI.drawPopup(renderer, tr(STR_INDEXING));

  size_t offset = 0;
  const size_t fileSize = doc->getFileSize();

  while (offset < fileSize) {
    std::vector<Row> tempRows;
    size_t nextOffset = offset;
    if (!loadPageAtOffset(offset, tempRows, nextOffset)) break;
    if (nextOffset <= offset) break;  // no progress — defensive
    offset = nextOffset;
    if (offset < fileSize) pageOffsets.push_back(offset);
    if (pageOffsets.size() % 16 == 0) vTaskDelay(1);
  }

  totalPages = static_cast<int>(pageOffsets.size());
  LOG_DBG("MDR", "Built page index: %d pages", totalPages);
}

void MarkdownReaderActivity::render(RenderLock&&) {
  if (!doc) return;

  if (!initialized) initializeReader();

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  size_t offset = pageOffsets[currentPage];
  size_t nextOffset;
  currentPageRows.clear();
  loadPageAtOffset(offset, currentPageRows, nextOffset);

  renderer.clearScreen();
  renderPage();

  saveProgress();
}

void MarkdownReaderActivity::renderPage() {
  auto renderRows = [&]() {
    int y = cachedOrientedMarginTop;
    for (const auto& row : currentPageRows) {
      if (row.isRule) {
        const int ruleY = y + lineHeight / 2;
        renderer.drawLine(cachedOrientedMarginLeft, ruleY,
                          cachedOrientedMarginLeft + viewportWidth - 1, ruleY);
      } else {
        for (const auto& run : row.runs) {
          if (run.text.empty()) continue;
          const int x = cachedOrientedMarginLeft + row.indentPx + run.xOffset;
          const auto style = static_cast<EpdFontFamily::Style>(run.style);
          renderer.drawText(cachedFontId, x, y, run.text.c_str(), true, style);
          if (run.underline) {
            const int w = renderer.getTextWidth(cachedFontId, run.text.c_str(), style);
            renderer.drawLine(x, y + lineHeight - 2, x + w - 1, y + lineHeight - 2);
          }
        }
      }
      y += lineHeight;
      if (row.extraSpacing) y += paragraphSpacing;
    }
  };

  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderRows();
  scope.endScanAndPrewarm();

  renderRows();
  renderStatusBar();

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, [&renderRows]() { renderRows(); });
  }
}

void MarkdownReaderActivity::renderStatusBar() const {
  const float progress = totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0;
  std::string title;
  if (SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE) {
    title = doc->getTitle();
  }
  GUI.drawStatusBar(renderer, progress, currentPage + 1, totalPages, title);
}

void MarkdownReaderActivity::saveProgress() const {
  FsFile f;
  if (Storage.openFileForWrite("MDR", doc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = currentPage & 0xFF;
    data[1] = (currentPage >> 8) & 0xFF;
    data[2] = 0;
    data[3] = 0;
    f.write(data, 4);
  }
}

void MarkdownReaderActivity::loadProgress() {
  FsFile f;
  if (Storage.openFileForRead("MDR", doc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] + (data[1] << 8);
      if (currentPage >= totalPages) currentPage = totalPages - 1;
      if (currentPage < 0) currentPage = 0;
      LOG_DBG("MDR", "Loaded progress: page %d/%d", currentPage, totalPages);
    }
  }
}

bool MarkdownReaderActivity::loadPageIndexCache() {
  // Cache file format (md_<hash>/index.bin, see docs/file-formats.md):
  //   uint32_t magic        = 0x4D444B49 ("MDKI")
  //   uint8_t  version      = CACHE_VERSION
  //   uint32_t fileSize     (source file size for invalidation)
  //   int32_t  viewportW    (re-layout if changed — orientation/margins)
  //   int32_t  viewportH
  //   int32_t  fontId       (re-layout if font family/size changed)
  //   int32_t  screenMargin (re-layout if margin changed)
  //   uint32_t numPages
  //   numPages * uint32_t   page start offsets (file bytes)
  std::string cachePath = doc->getCachePath() + "/index.bin";
  FsFile f;
  if (!Storage.openFileForRead("MDR", cachePath, f)) {
    LOG_DBG("MDR", "No page index cache");
    return false;
  }

  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) {
    LOG_DBG("MDR", "Cache magic mismatch, rebuilding");
    return false;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) {
    LOG_DBG("MDR", "Cache version mismatch (%d != %d), rebuilding", version, CACHE_VERSION);
    return false;
  }

  uint32_t fileSize;
  serialization::readPod(f, fileSize);
  if (fileSize != doc->getFileSize()) {
    LOG_DBG("MDR", "Cache file size mismatch, rebuilding");
    return false;
  }

  int32_t cachedW;
  serialization::readPod(f, cachedW);
  if (cachedW != viewportWidth) return false;

  int32_t cachedH;
  serialization::readPod(f, cachedH);
  if (cachedH != viewportHeight) return false;

  int32_t fontId;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) return false;

  int32_t margin;
  serialization::readPod(f, margin);
  if (margin != cachedScreenMargin) return false;

  uint32_t numPages;
  serialization::readPod(f, numPages);

  pageOffsets.clear();
  pageOffsets.reserve(numPages);
  for (uint32_t i = 0; i < numPages; ++i) {
    uint32_t off;
    serialization::readPod(f, off);
    pageOffsets.push_back(off);
  }

  totalPages = static_cast<int>(pageOffsets.size());
  LOG_DBG("MDR", "Loaded page index cache: %d pages", totalPages);
  return true;
}

void MarkdownReaderActivity::savePageIndexCache() const {
  std::string cachePath = doc->getCachePath() + "/index.bin";
  FsFile f;
  if (!Storage.openFileForWrite("MDR", cachePath, f)) {
    LOG_ERR("MDR", "Failed to save page index cache");
    return;
  }

  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(doc->getFileSize()));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(viewportHeight));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));
  for (size_t off : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(off));
  }

  LOG_DBG("MDR", "Saved page index cache: %d pages", totalPages);
}

ScreenshotInfo MarkdownReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;  // share Txt screenshot type — same status-bar layout
  if (doc) {
    const std::string t = doc->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage + 1;
  info.totalPages = totalPages;
  info.progressPercent = totalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / totalPages + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}
