#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// Material Design 3 theme metrics — built on an 8 px baseline grid.
namespace Material3Metrics {
constexpr ThemeMetrics values = {.batteryWidth = 16,
                                 .batteryHeight = 12,
                                 .topPadding = 8,
                                 .batteryBarHeight = 40,
                                 .headerHeight = 64,
                                 .verticalSpacing = 8,
                                 .contentSidePadding = 16,
                                 .listRowHeight = 56,
                                 .listWithSubtitleRowHeight = 72,
                                 .menuRowHeight = 56,
                                 .menuSpacing = 8,
                                 .tabSpacing = 0,
                                 .tabBarHeight = 48,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 56,
                                 .homeCoverHeight = 226,
                                 .homeCoverTileHeight = 242,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = 16,
                                 .buttonHintsHeight = 56,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyWidth = 30,
                                 .keyboardKeyHeight = 48,
                                 .keyboardKeySpacing = 4,
                                 .keyboardBottomKeyHeight = 40,
                                 .keyboardBottomKeySpacing = 4,
                                 .keyboardBottomAligned = true,
                                 .keyboardCenteredText = true,
                                 .keyboardVerticalOffset = 0,
                                 .keyboardTextFieldWidthPercent = 90,
                                 .keyboardWidthPercent = 95,
                                 .keyboardKeyCornerRadius = 8};
}

// Material Design 3 theme for the CrossPoint e-paper display.
//
// Design principles applied to monochrome e-ink:
//   - Primary surface    → black fill
//   - Secondary surface  → DarkGray dither (~75 % fill)
//   - Tertiary surface   → LightGray dither (~25 % fill)
//   - Shape              → rounded rects mirroring MD3 corner radii
//   - Elevation shadows  → LightGray offset box (3 px)
//   - State layers       → invert fill on selection (white text on black)
//   - Motion / ripple    → omitted; use FAST_REFRESH for interactions
class Material3Theme : public BaseTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                     const char* rightLabel = nullptr) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle = nullptr,
                const std::function<UIIcon(int index)>& rowIcon = nullptr,
                const std::function<std::string(int index)>& rowValue = nullptr, bool highlightValue = false,
                const std::function<bool(int index)>& rowDimmed = nullptr,
                const std::function<bool(int index)>& isHeader = nullptr) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  Rect drawPopup(const GfxRenderer& renderer, const char* message) const override;
  void fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, int progress) const override;
  void drawTextField(const GfxRenderer& renderer, Rect rect, int textWidth, bool cursorMode = false,
                     int contentStartX = 0, int contentWidth = 0) const override;
  void drawKeyboardKey(const GfxRenderer& renderer, Rect rect, const char* label, bool isSelected,
                       const char* secondaryLabel = nullptr, KeyboardKeyType keyType = KeyboardKeyType::Normal,
                       bool inactiveSelection = false) const override;
  bool showsFileIcons() const override { return true; }
};
