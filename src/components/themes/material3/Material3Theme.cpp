#include "Material3Theme.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "components/icons/book24.h"
#include "components/icons/file24.h"
#include "components/icons/folder24.h"
#include "components/icons/image24.h"
#include "components/icons/text24.h"
#include "components/icons/book.h"
#include "components/icons/chart.h"
#include "components/icons/folder.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

namespace {
// MD3 corner radii
constexpr int kListRowRadius = 12;
constexpr int kMenuTileRadius = 16;
constexpr int kTabIndicatorRadius = 2;
constexpr int kDialogRadius = 28;
constexpr int kKeyRadius = 8;
constexpr int kBtnHintsRadius = 8;

// Horizontal inset inside a row before the text starts
constexpr int kTextInset = 12;

// Maximum value column width for list rows
constexpr int kMaxValueWidth = 200;

// Elevation shadow offset for dialogs
constexpr int kShadowOffset = 3;

// Button hints geometry (X4 physical button positions + width)
constexpr int kBtnWidth = 80;
constexpr int kX4BtnPositions[] = {58, 146, 254, 342};
constexpr int kX3BtnPositions[] = {65, 157, 291, 383};

// Vertical text offset inside button hints bar
constexpr int kBtnTextYOffset = 20;

// Side-button hint geometry
constexpr int kSideBtnHeight = 78;
constexpr int kX4SideBtnY = 345;

const uint8_t* iconForName(UIIcon icon, int size) {
  if (size == 24) {
    switch (icon) {
      case UIIcon::Folder:
        return Folder24Icon;
      case UIIcon::Text:
        return Text24Icon;
      case UIIcon::Image:
        return Image24Icon;
      case UIIcon::Book:
        return Book24Icon;
      case UIIcon::File:
        return File24Icon;
      default:
        return nullptr;
    }
  }
  if (size == 32) {
    switch (icon) {
      case UIIcon::Folder:
        return FolderIcon;
      case UIIcon::Book:
        return BookIcon;
      case UIIcon::Chart:
        return ChartIcon;
      case UIIcon::Recent:
        return RecentIcon;
      case UIIcon::Settings:
        return Settings2Icon;
      case UIIcon::Transfer:
        return TransferIcon;
      case UIIcon::Library:
        return LibraryIcon;
      case UIIcon::Wifi:
        return WifiIcon;
      case UIIcon::Hotspot:
        return HotspotIcon;
      default:
        return nullptr;
    }
  }
  return nullptr;
}
}  // namespace

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

void Material3Theme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                                const char* subtitle) const {
  // White background
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const bool showBattery =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = rect.x + rect.width - 12 - Material3Metrics::values.batteryWidth;
  drawBatteryRight(renderer,
                   Rect{batteryX, rect.y + 8, Material3Metrics::values.batteryWidth,
                        Material3Metrics::values.batteryHeight},
                   showBattery);

  // Space reserved for battery group (icon + percentage text)
  const int maxBatteryGroupWidth =
      showBattery ? renderer.getTextWidth(SMALL_FONT_ID, "100%") + batteryPercentSpacing +
                        Material3Metrics::values.batteryWidth + 8
                  : Material3Metrics::values.batteryWidth + 8;

  const int titleY = rect.y + Material3Metrics::values.batteryBarHeight;
  const int pad = Material3Metrics::values.contentSidePadding;

  if (title) {
    int maxTitleWidth = rect.width - pad * 2 - maxBatteryGroupWidth;
    if (subtitle) {
      // Leave room for right-aligned subtitle
      maxTitleWidth -= renderer.getTextWidth(SMALL_FONT_ID, subtitle) + pad;
    }
    auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title, std::max(0, maxTitleWidth), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rect.x + pad, titleY, truncatedTitle.c_str(), true, EpdFontFamily::BOLD);
  }

  if (subtitle) {
    const int subtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, subtitle);
    const int maxSubW = rect.width - pad * 2 - maxBatteryGroupWidth;
    auto truncSub = renderer.truncatedText(SMALL_FONT_ID, subtitle, std::max(0, maxSubW));
    const int truncSubW = renderer.getTextWidth(SMALL_FONT_ID, truncSub.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - pad - truncSubW,
                      titleY + renderer.getLineHeight(UI_12_FONT_ID) - renderer.getLineHeight(SMALL_FONT_ID),
                      truncSub.c_str(), true);
    (void)subtitleWidth;
  }

  // MD3 bottom divider (1 px)
  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

// ---------------------------------------------------------------------------
// Sub-header
// ---------------------------------------------------------------------------

void Material3Theme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                                   const char* rightLabel) const {
  const int pad = Material3Metrics::values.contentSidePadding;
  int rightSpace = pad;

  if (rightLabel) {
    auto truncRight = renderer.truncatedText(SMALL_FONT_ID, rightLabel, kMaxValueWidth);
    const int rw = renderer.getTextWidth(SMALL_FONT_ID, truncRight.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - pad - rw, rect.y + 6, truncRight.c_str(), true);
    rightSpace += rw + kTextInset;
  }

  const int maxLabelW = rect.width - pad - rightSpace;
  auto truncLabel = renderer.truncatedText(UI_10_FONT_ID, label, std::max(0, maxLabelW));
  renderer.drawText(UI_10_FONT_ID, rect.x + pad, rect.y + 6, truncLabel.c_str(), true);

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

// ---------------------------------------------------------------------------
// Tab bar — MD3 Secondary Tab Bar
// ---------------------------------------------------------------------------

void Material3Theme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                                bool selected) const {
  if (tabs.empty()) return;

  const int n = static_cast<int>(tabs.size());
  const int slotWidth = rect.width / n;
  constexpr int indicatorH = 3;
  const int textLineH = renderer.getLineHeight(UI_12_FONT_ID);
  // Vertically center the text leaving room for the indicator at the bottom
  const int textY = rect.y + (rect.height - textLineH - indicatorH - 4) / 2;

  for (int i = 0; i < n; i++) {
    const auto& tab = tabs[i];
    const int slotX = rect.x + i * slotWidth;
    const EpdFontFamily::Style style = tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, tab.label, style);
    const int textX = slotX + (slotWidth - textWidth) / 2;

    // Tab label — invert when active and focused
    const bool invert = tab.selected && selected;
    renderer.drawText(UI_12_FONT_ID, textX, textY, tab.label, !invert, style);

    if (tab.selected) {
      // MD3 indicator pill spanning most of the slot width
      const int indW = slotWidth - 16;
      const int indX = slotX + 8;
      const int indY = rect.y + rect.height - indicatorH - 1;
      renderer.fillRoundedRect(indX, indY, indW, indicatorH, kTabIndicatorRadius, Color::Black);
    }
  }

  // Bottom divider
  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

void Material3Theme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                              const std::function<std::string(int index)>& rowTitle,
                              const std::function<std::string(int index)>& rowSubtitle,
                              const std::function<UIIcon(int index)>& rowIcon,
                              const std::function<std::string(int index)>& rowValue, bool highlightValue,
                              const std::function<bool(int index)>& rowDimmed,
                              const std::function<bool(int index)>& isHeader) const {
  const int rowHeight =
      rowSubtitle ? Material3Metrics::values.listWithSubtitleRowHeight : Material3Metrics::values.listRowHeight;
  const int pageItems = std::max(1, rect.height / rowHeight);
  constexpr int sectionHeaderTopPadding = 20;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    const int scrollH = rect.height;
    const int thumbH = (scrollH * pageItems) / itemCount;
    const int currentPage = selectedIndex / pageItems;
    const int thumbY =
        rect.y + ((scrollH - thumbH) * currentPage) / std::max(1, totalPages - 1);
    const int scrollX = rect.x + rect.width - Material3Metrics::values.scrollBarRightOffset;
    renderer.drawLine(scrollX, rect.y, scrollX, rect.y + scrollH, true);
    renderer.fillRect(scrollX - Material3Metrics::values.scrollBarWidth, thumbY,
                      Material3Metrics::values.scrollBarWidth, thumbH, true);
  }

  const int contentWidth =
      rect.width -
      (totalPages > 1 ? (Material3Metrics::values.scrollBarWidth + Material3Metrics::values.scrollBarRightOffset) : 1);
  const int pad = Material3Metrics::values.contentSidePadding;
  const int pageStartIndex = selectedIndex / pageItems * pageItems;

  // Selected row highlight — MD3 state layer (full-width black rounded rect)
  if (selectedIndex >= 0 && !(isHeader && isHeader(selectedIndex))) {
    int selY = rect.y;
    for (int j = pageStartIndex; j < selectedIndex; j++) {
      selY += rowHeight;
      if (isHeader && isHeader(j + 1)) selY += sectionHeaderTopPadding;
    }
    renderer.fillRoundedRect(rect.x + pad, selY, contentWidth - pad * 2, rowHeight, kListRowRadius, Color::Black);
  }

  // Text / icon layout
  const int iconSize = rowSubtitle ? 32 : 24;
  int textX = rect.x + pad + kTextInset;
  int textWidth = contentWidth - pad * 2 - kTextInset * 2;
  if (rowIcon) {
    textX += iconSize + kTextInset;
    textWidth -= iconSize + kTextInset;
  }

  int currentY = rect.y;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    if (i > pageStartIndex && isHeader && isHeader(i)) currentY += sectionHeaderTopPadding;
    const int itemY = currentY;
    currentY += rowHeight;

    if (isHeader && isHeader(i)) {
      // Section header: bold uppercase label with divider
      std::string label = rowTitle(i);
      for (auto& c : label) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      auto trunc = renderer.truncatedText(UI_10_FONT_ID, label.c_str(), contentWidth - pad * 2, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, rect.x + pad, itemY + 6, trunc.c_str(), true, EpdFontFamily::BOLD);
      renderer.drawLine(rect.x, itemY + rowHeight - 1, rect.x + contentWidth, itemY + rowHeight - 1, true);
      continue;
    }

    const bool isSelected = i == selectedIndex;
    int rowTextWidth = textWidth;

    // Right-side value
    std::string valueText;
    if (rowValue) {
      valueText = rowValue(i);
      if (!valueText.empty()) {
        valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), kMaxValueWidth);
        const int valW = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + kTextInset;
        rowTextWidth -= valW;
      }
    }

    // Title
    auto itemName = rowTitle(i);
    auto item = renderer.truncatedText(UI_12_FONT_ID, itemName.c_str(), rowTextWidth);
    const int titleY = rowSubtitle
                           ? itemY + 10
                           : itemY + (rowHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    renderer.drawText(UI_12_FONT_ID, textX, titleY, item.c_str(), !isSelected);

    // Dimmed overlay (checkerboard) for disabled items
    if (rowDimmed && rowDimmed(i) && !isSelected) {
      const int tw = renderer.getTextWidth(UI_12_FONT_ID, item.c_str());
      const int lh = renderer.getLineHeight(UI_12_FONT_ID);
      for (int py = titleY; py < titleY + lh; py++)
        for (int px = textX; px < textX + tw; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }

    // Icon
    if (rowIcon) {
      UIIcon icon = rowIcon(i);
      const uint8_t* bmp = iconForName(icon, iconSize);
      if (bmp) {
        const int iconX = rect.x + pad + kTextInset;
        const int iconY = itemY + (rowHeight - iconSize) / 2;
        if (isSelected) {
          renderer.fillRect(iconX - 1, iconY - 1, iconSize + 2, iconSize + 2, false);
        }
        renderer.drawIcon(bmp, iconX, iconY, iconSize, iconSize);
      }
    }

    // Subtitle
    if (rowSubtitle) {
      const std::string subText = rowSubtitle(i);
      if (!subText.empty()) {
        auto sub = renderer.truncatedText(UI_10_FONT_ID, subText.c_str(), rowTextWidth);
        renderer.drawText(UI_10_FONT_ID, textX, itemY + 10 + renderer.getLineHeight(UI_12_FONT_ID) + 4,
                          sub.c_str(), !isSelected);
      }
    }

    // Value (right-aligned)
    if (!valueText.empty()) {
      const int valW = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
      const int valX = rect.x + contentWidth - pad - valW;
      const int valY = rowSubtitle ? titleY + renderer.getLineHeight(UI_12_FONT_ID) / 2
                                   : itemY + (rowHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
      if (isSelected && highlightValue) {
        renderer.fillRoundedRect(valX - kTextInset, itemY, valW + kTextInset * 2, rowHeight, kListRowRadius,
                                 Color::White);
        renderer.drawText(UI_10_FONT_ID, valX, valY, valueText.c_str(), true);
      } else {
        renderer.drawText(UI_10_FONT_ID, valX, valY, valueText.c_str(), !isSelected);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Button hints — MD3 Navigation Bar (solid black strip, white labels)
// ---------------------------------------------------------------------------

void Material3Theme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                     const char* btn4) const {
  const GfxRenderer::Orientation origOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int barHeight = Material3Metrics::values.buttonHintsHeight;
  const int barY = pageHeight - barHeight;
  const int* buttonPositions = gpio.deviceIsX3() ? kX3BtnPositions : kX4BtnPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  // Solid black navigation bar
  renderer.fillRect(0, barY, pageWidth, barHeight, true);

  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    const int x = buttonPositions[i];
    const int centerX = x + kBtnWidth / 2;
    const int centerY = barY + barHeight / 2;
    if (!BaseTheme::drawArrowIfNeeded(renderer, labels[i], centerX, centerY, 5, false)) {
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
      renderer.drawText(SMALL_FONT_ID, x + (kBtnWidth - textWidth) / 2, barY + kBtnTextYOffset, labels[i],
                        false);  // white text on black bar
    }
  }

  renderer.setOrientation(origOrientation);
}

// ---------------------------------------------------------------------------
// Side button hints — MD3 filled dark rounded rects with white labels
// ---------------------------------------------------------------------------

void Material3Theme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn,
                                         const char* bottomBtn) const {
  const int screenWidth = renderer.getScreenWidth();
  const int btnW = Material3Metrics::values.sideButtonHintsWidth;
  const char* labels[] = {topBtn, bottomBtn};

  if (gpio.deviceIsX3()) {
    // X3: Up on left side, Down on right side
    constexpr int x3BtnY = 155;
    if (topBtn && topBtn[0] != '\0') {
      renderer.fillRoundedRect(0, x3BtnY, btnW, kSideBtnHeight, kBtnHintsRadius, false, true, false, true,
                               Color::Black);
      if (!BaseTheme::drawArrowIfNeeded(renderer, topBtn, btnW / 2, x3BtnY + kSideBtnHeight / 2, 5, false)) {
        const int tw = renderer.getTextWidth(SMALL_FONT_ID, topBtn);
        renderer.drawTextRotated90CW(SMALL_FONT_ID, (btnW - renderer.getTextHeight(SMALL_FONT_ID)) / 2,
                                     x3BtnY + (kSideBtnHeight + tw) / 2, topBtn, false);
      }
    }
    if (bottomBtn && bottomBtn[0] != '\0') {
      const int rx = screenWidth - btnW;
      renderer.fillRoundedRect(rx, x3BtnY, btnW, kSideBtnHeight, kBtnHintsRadius, true, false, true, false,
                               Color::Black);
      if (!BaseTheme::drawArrowIfNeeded(renderer, bottomBtn, rx + btnW / 2, x3BtnY + kSideBtnHeight / 2, 5, false)) {
        const int tw = renderer.getTextWidth(SMALL_FONT_ID, bottomBtn);
        renderer.drawTextRotated90CW(SMALL_FONT_ID, rx + (btnW - renderer.getTextHeight(SMALL_FONT_ID)) / 2,
                                     x3BtnY + (kSideBtnHeight + tw) / 2, bottomBtn, false);
      }
    }
  } else {
    // X4: Both buttons stacked on the right side
    const int x = screenWidth - btnW;
    for (int i = 0; i < 2; i++) {
      if (labels[i] == nullptr || labels[i][0] == '\0') continue;
      const int y = kX4SideBtnY + i * (kSideBtnHeight + 5);
      renderer.fillRoundedRect(x, y, btnW, kSideBtnHeight, kBtnHintsRadius, true, false, true, false, Color::Black);
      if (!BaseTheme::drawArrowIfNeeded(renderer, labels[i], x + btnW / 2, y + kSideBtnHeight / 2, 5, false)) {
        const int tw = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
        renderer.drawTextRotated90CW(SMALL_FONT_ID, x + (btnW - renderer.getTextHeight(SMALL_FONT_ID)) / 2,
                                     y + (kSideBtnHeight + tw) / 2, labels[i], false);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Button menu — MD3 list tiles
// ---------------------------------------------------------------------------

void Material3Theme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                    const std::function<std::string(int index)>& buttonLabel,
                                    const std::function<UIIcon(int index)>& rowIcon) const {
  const int pad = Material3Metrics::values.contentSidePadding;
  constexpr int maxVisible = 7;
  const int pageItems = maxVisible;
  const int totalPages = (buttonCount + pageItems - 1) / pageItems;
  const int pageStartIndex = (selectedIndex / pageItems) * pageItems;

  if (totalPages > 1) {
    const int scrollAreaH =
        maxVisible * (Material3Metrics::values.menuRowHeight + Material3Metrics::values.menuSpacing) -
        Material3Metrics::values.menuSpacing;
    const int thumbH = (scrollAreaH * pageItems) / buttonCount;
    const int currentPage = selectedIndex / pageItems;
    const int thumbY = rect.y + ((scrollAreaH - thumbH) * currentPage) / std::max(1, totalPages - 1);
    const int scrollX = rect.x + rect.width - Material3Metrics::values.scrollBarRightOffset;
    renderer.drawLine(scrollX, rect.y, scrollX, rect.y + scrollAreaH, true);
    renderer.fillRect(scrollX - Material3Metrics::values.scrollBarWidth, thumbY,
                      Material3Metrics::values.scrollBarWidth, thumbH, true);
  }

  for (int i = pageStartIndex; i < buttonCount && i < pageStartIndex + pageItems; ++i) {
    const int displayIdx = i - pageStartIndex;
    int tileW = rect.width - pad * 2;
    if (totalPages > 1) {
      tileW -= (Material3Metrics::values.scrollBarWidth + Material3Metrics::values.scrollBarRightOffset);
    }
    const Rect tile{rect.x + pad,
                    rect.y + displayIdx * (Material3Metrics::values.menuRowHeight + Material3Metrics::values.menuSpacing),
                    tileW, Material3Metrics::values.menuRowHeight};
    const bool isSelected = selectedIndex == i;

    if (isSelected) {
      renderer.fillRoundedRect(tile.x, tile.y, tile.width, tile.height, kMenuTileRadius, Color::Black);
    } else {
      renderer.drawRoundedRect(tile.x, tile.y, tile.width, tile.height, 1, kMenuTileRadius, true);
    }

    int textX = tile.x + kTextInset * 2;
    const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
    const int textY = tile.y + (Material3Metrics::values.menuRowHeight - lineH) / 2;

    if (rowIcon) {
      UIIcon icon = rowIcon(i);
      const uint8_t* bmp = iconForName(icon, 32);
      if (bmp) {
        if (isSelected) renderer.fillRect(textX - 1, textY + 2, 34, 34, false);
        renderer.drawIcon(bmp, textX, textY + 2, 32, 32);
        textX += 32 + kTextInset;
      }
    }

    std::string labelStr = buttonLabel(i);
    const int maxLabelW = tile.x + tile.width - textX - kTextInset;
    auto truncLabel = renderer.truncatedText(UI_12_FONT_ID, labelStr.c_str(), std::max(0, maxLabelW));
    renderer.drawText(UI_12_FONT_ID, textX, textY, truncLabel.c_str(), !isSelected);
  }
}

// ---------------------------------------------------------------------------
// Popup — MD3 Dialog
// ---------------------------------------------------------------------------

Rect Material3Theme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  const int y = static_cast<int>(renderer.getScreenHeight() * 0.165f);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, EpdFontFamily::REGULAR);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  constexpr int marginX = 24;
  constexpr int marginY = 18;
  const int w = textWidth + marginX * 2;
  const int h = textHeight + marginY * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;

  // Elevation shadow via LightGray offset box
  renderer.fillRoundedRect(x + kShadowOffset, y + kShadowOffset, w, h, kDialogRadius, Color::LightGray);

  // White dialog card
  renderer.fillRoundedRect(x, y, w, h, kDialogRadius, Color::White);

  // 1 px border
  renderer.drawRoundedRect(x, y, w, h, 1, kDialogRadius, true);

  // Centered message text
  const int textX = x + (w - textWidth) / 2;
  const int textY = y + marginY;
  renderer.drawText(UI_12_FONT_ID, textX, textY, message, true, EpdFontFamily::REGULAR);

  renderer.displayBuffer();

  return Rect{x, y, w, h};
}

// ---------------------------------------------------------------------------
// Popup progress — MD3 linear progress indicator
// ---------------------------------------------------------------------------

void Material3Theme::fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const {
  constexpr int barH = 4;
  constexpr int barRadius = 2;
  constexpr int barSidePad = 24;
  const int barW = layout.width - barSidePad * 2;
  const int barX = layout.x + barSidePad;
  const int barY = layout.y + layout.height - barH - 8;

  // Track (LightGray)
  renderer.fillRoundedRect(barX, barY, barW, barH, barRadius, Color::LightGray);

  // Fill
  const int fillW = barW * progress / 100;
  if (fillW > 0) {
    renderer.fillRoundedRect(barX, barY, fillW, barH, barRadius, Color::Black);
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

// ---------------------------------------------------------------------------
// Text field — MD3 Filled Text Field
// ---------------------------------------------------------------------------

void Material3Theme::drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode,
                                   int contentStartX, int contentWidth) const {
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineY = rect.y + rect.height + lineH + Material3Metrics::values.verticalSpacing;
  const int thickness = cursorMode ? 2 : 1;

  // LightGray background fill for the input area
  renderer.fillRectDither(rect.x - kTextInset, rect.y - 4, rect.width + kTextInset * 2, rect.height + 8,
                          Color::LightGray);

  if (contentWidth > 0) {
    // Cursor underline segment
    renderer.drawLine(rect.x + contentStartX, lineY, rect.x + contentStartX + contentWidth - 1, lineY, thickness,
                      true);
    return;
  }

  // Full-width bottom indicator line
  constexpr int hPad = 8;
  const int lineW = textWidth + hPad * 2;
  const int lineStart = rect.x + (rect.width - lineW) / 2;
  renderer.drawLine(lineStart, lineY, lineStart + lineW - 1, lineY, thickness, true);
}

// ---------------------------------------------------------------------------
// Keyboard key — MD3 filled key shape
// ---------------------------------------------------------------------------

void Material3Theme::drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label,
                                     const bool isSelected, const char* secondaryLabel,
                                     const KeyboardKeyType keyType, const bool inactiveSelection) const {
  const bool disabled = keyType == KeyboardKeyType::Disabled;
  const bool invert = isSelected && !inactiveSelection;

  // Background fill
  Color fillColor;
  if (isSelected) {
    fillColor = (inactiveSelection || disabled) ? Color::LightGray : Color::Black;
  } else {
    fillColor = disabled ? Color::LightGray : Color::DarkGray;
  }
  renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, kKeyRadius, fillColor);

  // Border for non-selected keys so they read against a white background
  if (!isSelected) {
    renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, kKeyRadius, true);
  }

  // Special key graphics
  if (keyType == KeyboardKeyType::Space) {
    const int lineHalfW = rect.width * 3 / 10;
    const int cx = rect.x + rect.width / 2;
    const int ly = rect.y + rect.height / 2 + 3;
    renderer.drawLine(cx - lineHalfW, ly, cx + lineHalfW, ly, 3, !invert);
    return;
  }

  if (keyType == KeyboardKeyType::Del) {
    const int cx = rect.x + rect.width / 2;
    const int cy = rect.y + rect.height / 2;
    const int arrowLen = rect.width / 4;
    const int arrowHead = std::max(1, arrowLen / 2);
    renderer.drawLine(cx - arrowLen / 2, cy, cx + arrowLen / 2, cy, 3, !invert);
    renderer.drawLine(cx - arrowLen / 2, cy, cx - arrowLen / 2 + arrowHead, cy - arrowHead, 3, !invert);
    renderer.drawLine(cx - arrowLen / 2, cy, cx - arrowLen / 2 + arrowHead, cy + arrowHead, 3, !invert);
    return;
  }

  if (label && label[0] != '\0') {
    const int tw = renderer.getTextWidth(UI_12_FONT_ID, label);
    const int tx = rect.x + (rect.width - tw) / 2;
    const int ty = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    renderer.drawText(UI_12_FONT_ID, tx, ty, label, !invert);
  }

  if (secondaryLabel && secondaryLabel[0] != '\0') {
    const int sw = renderer.getTextWidth(SMALL_FONT_ID, secondaryLabel);
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - sw - 3, rect.y + 1, secondaryLabel, !invert);
  }
}
