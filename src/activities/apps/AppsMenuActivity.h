#pragma once
#include <memory>
#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct RadarNode;
struct RadarHomeStatus;

class AppsMenuActivity final : public Activity {
 public:
  explicit AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AppsMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int selectorIndex = 0;
  static constexpr int ITEM_COUNT = 8;
  static constexpr int COLS = 2;
  static constexpr int ROWS = 4;

  int getRow() const { return selectorIndex / COLS; }
  int getCol() const { return selectorIndex % COLS; }

  // Cached system info (refreshed on enter + periodically)
  uint32_t freeHeap = 0;
  uint8_t batteryPercent = 0;
  unsigned long uptimeSeconds = 0;
  bool wifiConnected = false;
  unsigned long lastInfoRefresh = 0;
  static constexpr unsigned long INFO_REFRESH_MS = 30000;
  char uptimeStr[16] = "";
  char macShort[6] = "";  // last two octets: "ee:ff"

  // Badge placeholder — no active badges in this build
  int badgeSecurity = -1;

  void refreshSystemInfo();

  // Last-used activity per category (read from SD on enter)
  char lastUsedName[ITEM_COUNT][32] = {};
  void loadLastUsed();

  // Build the AppCategoryActivity for the given tile index
  std::unique_ptr<Activity> buildCategory(int index);

  // Tile rendering
  void drawTile(int index, int x, int y, int w, int h, bool selected) const;
  void drawStatusBar() const;
};
