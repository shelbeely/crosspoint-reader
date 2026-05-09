#include "Material3Theme.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "components/MaterialIcons.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int hPadding = 10;
constexpr int cornerRadius = 16;
constexpr int tabCornerRadius = 12;
constexpr int maxListValueWidth = 200;
constexpr int listIconSize = 24;
constexpr int menuIconSize = 24;

}  // namespace

void Material3Theme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                                const char* subtitle) const {
  const int batteryX = rect.x + rect.width - Material3Metrics::values.batteryWidth - 10;
  const int batteryY = rect.y + Material3Metrics::values.topPadding;
  const int batteryHeight = Material3Metrics::values.batteryHeight;
  drawBatteryRight(renderer, Rect(batteryX, batteryY, Material3Metrics::values.batteryWidth, batteryHeight), false);

  int titleY = rect.y + 14;
  const int titleMaxWidth =
      rect.width - Material3Metrics::values.batteryWidth - BaseTheme::batteryPercentSpacing - 30;
  auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title, titleMaxWidth, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, rect.x + Material3Metrics::values.contentSidePadding, titleY,
                    truncatedTitle.c_str(), true, EpdFontFamily::BOLD);

  if (subtitle != nullptr) {
    titleY += renderer.getLineHeight(UI_12_FONT_ID) + 2;
    auto truncatedSubtitle = renderer.truncatedText(UI_10_FONT_ID, subtitle, titleMaxWidth);
    renderer.drawText(UI_10_FONT_ID, rect.x + Material3Metrics::values.contentSidePadding, titleY,
                      truncatedSubtitle.c_str(), true);
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void Material3Theme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                                   const char* rightLabel) const {
  int currentX = rect.x + Material3Metrics::values.contentSidePadding;

  int rightSpace = hPadding;
  if (rightLabel != nullptr) {
    const int rightLabelWidth = renderer.getTextWidth(UI_10_FONT_ID, rightLabel);
    renderer.drawText(UI_10_FONT_ID, rect.x + rect.width - Material3Metrics::values.contentSidePadding - rightLabelWidth,
                      rect.y + 6, rightLabel, true);
    rightSpace += rightLabelWidth + hPadding;
  }

  auto truncatedLabel = renderer.truncatedText(
      UI_10_FONT_ID, label, rect.width - Material3Metrics::values.contentSidePadding - rightSpace);
  renderer.drawText(UI_10_FONT_ID, currentX, rect.y + 6, truncatedLabel.c_str(), true);

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void Material3Theme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                                bool selected) const {
  int currentX = rect.x + Material3Metrics::values.contentSidePadding;

  for (const auto& tab : tabs) {
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, tab.label);
    const int tabWidth = textWidth + 2 * hPadding;
    const int tabHeight = rect.height - 8;

    if (tab.selected) {
      renderer.fillRoundedRect(currentX, rect.y + 4, tabWidth, tabHeight, tabCornerRadius, Color::Black);
      renderer.drawText(UI_10_FONT_ID, currentX + hPadding, rect.y + 10, tab.label, false);
    } else {
      renderer.drawText(UI_10_FONT_ID, currentX + hPadding, rect.y + 10, tab.label, true);
    }

    currentX += tabWidth + Material3Metrics::values.tabSpacing;
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void Material3Theme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                              const std::function<std::string(int index)>& rowTitle,
                              const std::function<std::string(int index)>& rowSubtitle,
                              const std::function<UIIcon(int index)>& rowIcon,
                              const std::function<std::string(int index)>& rowValue, bool highlightValue) const {
  const int rowHeight =
      (rowSubtitle != nullptr) ? Material3Metrics::values.listWithSubtitleRowHeight : Material3Metrics::values.listRowHeight;
  const int pageItems = rect.height / rowHeight;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    const int scrollAreaHeight = rect.height;
    const int scrollBarHeight = (scrollAreaHeight * pageItems) / itemCount;
    const int currentPage = selectedIndex / pageItems;
    const int scrollBarY = rect.y + ((scrollAreaHeight - scrollBarHeight) * currentPage) / (totalPages - 1);
    const int scrollBarX = rect.x + rect.width - Material3Metrics::values.scrollBarRightOffset;
    renderer.drawLine(scrollBarX, rect.y, scrollBarX, rect.y + scrollAreaHeight, true);
    renderer.fillRect(scrollBarX - Material3Metrics::values.scrollBarWidth, scrollBarY,
                      Material3Metrics::values.scrollBarWidth, scrollBarHeight, true);
  }

  const int contentWidth =
      rect.width - (totalPages > 1 ? (Material3Metrics::values.scrollBarWidth + Material3Metrics::values.scrollBarRightOffset) : 1);

  if (selectedIndex >= 0) {
    renderer.fillRoundedRect(Material3Metrics::values.contentSidePadding, rect.y + selectedIndex % pageItems * rowHeight,
                             contentWidth - Material3Metrics::values.contentSidePadding * 2, rowHeight, cornerRadius,
                             Color::LightGray);
  }

  int textX = rect.x + Material3Metrics::values.contentSidePadding + hPadding;
  int textWidth = contentWidth - Material3Metrics::values.contentSidePadding * 2 - hPadding * 2;
  int iconSize = 0;
  if (rowIcon != nullptr) {
    iconSize = listIconSize;
    textX += iconSize + hPadding;
    textWidth -= iconSize + hPadding;
  }

  const auto pageStartIndex = selectedIndex / pageItems * pageItems;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;
    int rowTextWidth = textWidth;

    int valueWidth = 0;
    std::string valueText = "";
    if (rowValue != nullptr) {
      valueText = rowValue(i);
      valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), maxListValueWidth);
      valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + hPadding;
      rowTextWidth -= valueWidth;
    }

    const std::string title = rowTitle(i);
    auto truncatedTitle = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), rowTextWidth);
    int titleY = itemY + (rowHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;

    if (rowSubtitle != nullptr) {
      const std::string subtitle = rowSubtitle(i);
      titleY = itemY + 8;
      renderer.drawText(UI_10_FONT_ID, textX, titleY, truncatedTitle.c_str(), true, EpdFontFamily::BOLD);

      auto truncatedSubtitle = renderer.truncatedText(UI_10_FONT_ID, subtitle.c_str(), rowTextWidth);
      const int subtitleY = titleY + renderer.getLineHeight(UI_10_FONT_ID) + 2;
      renderer.drawText(UI_10_FONT_ID, textX, subtitleY, truncatedSubtitle.c_str(), true);
    } else {
      renderer.drawText(UI_10_FONT_ID, textX, titleY, truncatedTitle.c_str(), true);
    }

    if (!valueText.empty()) {
      const int valueX = rect.x + contentWidth - Material3Metrics::values.contentSidePadding - hPadding - valueWidth;
      const int valueY = itemY + (rowHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
      if (highlightValue) {
        renderer.drawText(UI_10_FONT_ID, valueX, valueY, valueText.c_str(), true, EpdFontFamily::BOLD);
      } else {
        renderer.drawText(UI_10_FONT_ID, valueX, valueY, valueText.c_str(), true);
      }
    }

    if (rowIcon != nullptr) {
      const UIIcon icon = rowIcon(i);
      char glyphBuf[5];
      const char* glyph = MaterialIcons::encodeIcon(icon, glyphBuf);
      const int glyphWidth = renderer.getTextWidth(MATERIAL_SYMBOLS_20_FONT_ID, glyph);
      const int iconX = rect.x + Material3Metrics::values.contentSidePadding + hPadding + (iconSize - glyphWidth) / 2;
      const int iconBaselineY = itemY + (rowHeight - renderer.getLineHeight(MATERIAL_SYMBOLS_20_FONT_ID)) / 2;
      renderer.drawText(MATERIAL_SYMBOLS_20_FONT_ID, iconX, iconBaselineY, glyph, true);
    }
  }
}

void Material3Theme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                    const std::function<std::string(int index)>& buttonLabel,
                                    const std::function<UIIcon(int index)>& rowIcon) const {
  for (int i = 0; i < buttonCount; ++i) {
    const int tileWidth = rect.width - Material3Metrics::values.contentSidePadding * 2;
    const Rect tileRect = Rect{rect.x + Material3Metrics::values.contentSidePadding,
                               rect.y + i * (Material3Metrics::values.menuRowHeight + Material3Metrics::values.menuSpacing),
                               tileWidth, Material3Metrics::values.menuRowHeight};

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, cornerRadius, Color::LightGray);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    int textX = tileRect.x + hPadding + 4;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int textY = tileRect.y + (Material3Metrics::values.menuRowHeight - lineHeight) / 2;

    if (rowIcon != nullptr) {
      const UIIcon icon = rowIcon(i);
      char glyphBuf[5];
      const char* glyph = MaterialIcons::encodeIcon(icon, glyphBuf);
      const int glyphWidth = renderer.getTextWidth(MATERIAL_SYMBOLS_24_FONT_ID, glyph);
      const int iconX = textX + (menuIconSize - glyphWidth) / 2;
      const int iconBaselineY = textY + 3;
      renderer.drawText(MATERIAL_SYMBOLS_24_FONT_ID, iconX, iconBaselineY, glyph, true);
      textX += menuIconSize + hPadding;
    }

    renderer.drawText(UI_12_FONT_ID, textX, textY, label, true);
  }
}

void Material3Theme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                     const char* btn4) const {
  const int y = renderer.getScreenHeight() - Material3Metrics::values.buttonHintsHeight;
  const int w = renderer.getScreenWidth();

  renderer.drawLine(0, y, w, y, true);

  const int textY = y + 8;
  const int centerY = y + Material3Metrics::values.buttonHintsHeight / 2;

  const int textWidth1 = btn1 != nullptr ? renderer.getTextWidth(UI_10_FONT_ID, btn1) : 0;
  const int textWidth2 = btn2 != nullptr ? renderer.getTextWidth(UI_10_FONT_ID, btn2) : 0;
  const int textWidth3 = btn3 != nullptr ? renderer.getTextWidth(UI_10_FONT_ID, btn3) : 0;
  const int textWidth4 = btn4 != nullptr ? renderer.getTextWidth(UI_10_FONT_ID, btn4) : 0;

  const int spacing = 16;
  const int totalWidth = textWidth1 + textWidth2 + textWidth3 + textWidth4 + spacing * 3;
  int currentX = (w - totalWidth) / 2;

  if (btn1 != nullptr) {
    renderer.drawText(UI_10_FONT_ID, currentX, textY, btn1, true);
    currentX += textWidth1 + spacing;
  }
  if (btn2 != nullptr) {
    renderer.drawText(UI_10_FONT_ID, currentX, textY, btn2, true);
    currentX += textWidth2 + spacing;
  }
  if (btn3 != nullptr) {
    renderer.drawText(UI_10_FONT_ID, currentX, textY, btn3, true);
    currentX += textWidth3 + spacing;
  }
  if (btn4 != nullptr) {
    renderer.drawText(UI_10_FONT_ID, currentX, textY, btn4, true);
  }
}
