#include "AppsMenuActivity.h"

#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "AppCategoryActivity.h"
#include "BackgroundManagerActivity.h"
#include "CalculatorActivity.h"
#include "CipherActivity.h"
#include "ClockActivity.h"
#include "DeviceInfoActivity.h"
#include "DiceRollerActivity.h"
#include "GameOfLifeActivity.h"
#include "MappedInputManager.h"
#include "MeshChatActivity.h"
#include "MinesweeperActivity.h"
#include "MorseCodeActivity.h"
#include "OtpGeneratorActivity.h"
#include "QrGeneratorActivity.h"
#include "ReadingStatsActivity.h"
#include "SdFileBrowserActivity.h"
#include "SnakeActivity.h"
#include "SudokuActivity.h"
#include "TaskManagerActivity.h"
#include "UnitConverterActivity.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/home/RecentBooksActivity.h"
#include "activities/network/NetworkModeSelectionActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "components/UITheme.h"
#include "components/themes/radar/RadarHomeRenderer.h"
#include "fontIds.h"

// 8 radar nodes — kept in flash (.rodata).
// Order: COMMS, TOOLS, CRYPTO, GAMES, READER, FILES, SYSTEM, SETTINGS
static constexpr RadarNode kRadarNodes[8] = {
    {"COMMS", 1}, {"TOOLS", 4}, {"CRYPTO", 3}, {"GAMES", 5},
    {"READER", 4}, {"FILES", 1}, {"SYSTEM", 3}, {"SETTINGS", 2},
};

void AppsMenuActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  badgeSecurity = -1;  // no security-PIN app — badge unused
  refreshSystemInfo();
  loadLastUsed();
  requestUpdate();
}

void AppsMenuActivity::loop() {
  // === RADAR MODE: circular navigation ===
  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::RADAR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      selectorIndex = (selectorIndex + 1) % ITEM_COUNT;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
               mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      selectorIndex = (selectorIndex - 1 + ITEM_COUNT) % ITEM_COUNT;
      requestUpdate();
    }
    if (millis() - lastInfoRefresh > INFO_REFRESH_MS) {
      uint32_t oldHeap = freeHeap;
      bool oldWifi = wifiConnected;
      refreshSystemInfo();
      if ((freeHeap / 1024) != (oldHeap / 1024) || wifiConnected != oldWifi) requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      auto app = buildCategory(selectorIndex);
      if (app) activityManager.pushActivity(std::move(app));
    }
    return;
  }

  // === 2D GRID NAVIGATION ===
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    int col = (getCol() + 1) % COLS;
    int row = col == 0 ? (getRow() + 1) % ROWS : getRow();
    selectorIndex = row * COLS + col;
    if (selectorIndex >= ITEM_COUNT) selectorIndex = 0;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    int col = getCol() - 1;
    int row = getRow();
    if (col < 0) {
      col = COLS - 1;
      row = (row - 1 + ROWS) % ROWS;
    }
    selectorIndex = row * COLS + col;
    if (selectorIndex >= ITEM_COUNT) selectorIndex = ITEM_COUNT - 1;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    int row = (getRow() + 1) % ROWS;
    selectorIndex = row * COLS + getCol();
    if (selectorIndex >= ITEM_COUNT) selectorIndex = getCol();
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    int col = getCol();
    int row = (getRow() - 1 + ROWS) % ROWS;
    selectorIndex = row * COLS + col;
    if (selectorIndex >= ITEM_COUNT) {
      row = (row - 1 + ROWS) % ROWS;
      selectorIndex = row * COLS + col;
    }
    requestUpdate();
  }

  if (millis() - lastInfoRefresh > INFO_REFRESH_MS) {
    uint32_t oldHeap = freeHeap;
    bool oldWifi = wifiConnected;
    refreshSystemInfo();
    if ((freeHeap / 1024) != (oldHeap / 1024) || wifiConnected != oldWifi) requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    auto app = buildCategory(selectorIndex);
    if (app) activityManager.pushActivity(std::move(app));
  }
  // Back ignored on main screen — use Power button to sleep.
}

std::unique_ptr<Activity> AppsMenuActivity::buildCategory(int index) {
  switch (index) {
    case 0: {
      // COMMS — ESP-NOW + local wireless communication
      std::vector<AppCategoryActivity::AppEntry> e = {
          {"Mesh Chat", "ESP-NOW text chat — no WiFi router needed", UIIcon::Transfer,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MeshChatActivity>(r, m); }},
      };
      return std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Comms", std::move(e), false, 0);
    }
    case 1: {
      // TOOLS — productivity utilities (no radio required)
      std::vector<AppCategoryActivity::AppEntry> e = {
          {"Clock", "NTP clock / stopwatch / pomodoro", UIIcon::Recent,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ClockActivity>(r, m); }},
          {"Calculator", "Basic calculator", UIIcon::File,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CalculatorActivity>(r, m); }},
          {tr(STR_MORSE_CODE), "Encode/decode morse code", UIIcon::Text,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MorseCodeActivity>(r, m); }},
          {tr(STR_UNIT_CONVERTER), "Convert between units", UIIcon::File,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<UnitConverterActivity>(r, m); }},
      };
      return std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Tools", std::move(e), false, 1);
    }
    case 2: {
      // CRYPTO — cipher and one-time code tools
      std::vector<AppCategoryActivity::AppEntry> e = {
          {"Cipher Tools", "ROT13, Caesar, Vigenere, XOR", UIIcon::Text,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<CipherActivity>(r, m); }},
          {"OTP Generator", "One-time pad random tokens", UIIcon::Text,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<OtpGeneratorActivity>(r, m); }},
          {tr(STR_QR_GENERATOR), "Generate QR codes from text", UIIcon::Image,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<QrGeneratorActivity>(r, m); }},
      };
      return std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Crypto", std::move(e), false, 2);
    }
    case 3: {
      // GAMES — offline games, no radio needed
      std::vector<AppCategoryActivity::AppEntry> e = {
          {tr(STR_SNAKE), "Classic snake", UIIcon::File,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SnakeActivity>(r, m); }},
          {tr(STR_MINESWEEPER), "Classic minesweeper", UIIcon::File,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<MinesweeperActivity>(r, m); }},
          {tr(STR_SUDOKU), "Number puzzle", UIIcon::File,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SudokuActivity>(r, m); }},
          {tr(STR_DICE_ROLLER), "Roll dice with animation", UIIcon::File,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DiceRollerActivity>(r, m); }},
          {tr(STR_GAME_OF_LIFE), "Conway's cellular automaton", UIIcon::File,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<GameOfLifeActivity>(r, m); }},
      };
      return std::make_unique<AppCategoryActivity>(renderer, mappedInput, tr(STR_GAMES), std::move(e), false, 3);
    }
    case 4: {
      // READER — books, OPDS, reading progress
      std::vector<AppCategoryActivity::AppEntry> e = {
          {"Open Book", "Browse and open an ebook", UIIcon::Book,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<FileBrowserActivity>(r, m); }},
          {"Recent Books", "Continue where you left off", UIIcon::Recent,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<RecentBooksActivity>(r, m); }},
          {"OPDS Browser", "Download books from OPDS servers", UIIcon::Library,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<OpdsBookBrowserActivity>(r, m); }},
          {"Reading Stats", "Pages read, streaks, progress", UIIcon::Book,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<ReadingStatsActivity>(r, m); }},
      };
      return std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Reader", std::move(e), false, 4);
    }
    case 5: {
      // FILES — SD card file browser
      std::vector<AppCategoryActivity::AppEntry> e = {
          {"File Browser", "Browse files on SD card", UIIcon::Folder,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SdFileBrowserActivity>(r, m); }},
      };
      return std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Files", std::move(e), false, 5);
    }
    case 6: {
      // SYSTEM — device info, task and background managers
      std::vector<AppCategoryActivity::AppEntry> e = {
          {"Device Info", "Chip, flash, RAM, firmware info", UIIcon::Settings,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<DeviceInfoActivity>(r, m); }},
          {"Task Manager", "Heap, uptime, activity stack", UIIcon::Settings,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<TaskManagerActivity>(r, m); }},
          {"Background", "Radio state, SD, active timers", UIIcon::Settings,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<BackgroundManagerActivity>(r, m); }},
      };
      return std::make_unique<AppCategoryActivity>(renderer, mappedInput, "System", std::move(e), false, 6);
    }
    case 7: {
      // SETTINGS — preferences + WiFi file transfer
      std::vector<AppCategoryActivity::AppEntry> e = {
          {"Settings", "Display, reader, controls, system", UIIcon::Settings,
           [](GfxRenderer& r, MappedInputManager& m) { return std::make_unique<SettingsActivity>(r, m); }},
          {"WiFi Transfer", "Upload/download files via WiFi", UIIcon::Transfer,
           [](GfxRenderer& r, MappedInputManager& m) {
             return std::make_unique<NetworkModeSelectionActivity>(r, m);
           }},
      };
      return std::make_unique<AppCategoryActivity>(renderer, mappedInput, "Settings", std::move(e), false, 7);
    }
    default:
      return nullptr;
  }
}

void AppsMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // === RADAR MODE ===
  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::RADAR) {
    char radioBuf[48];
    char sysBuf[32];
    snprintf(radioBuf, sizeof(radioBuf), "wifi:%s  ble:OFF", wifiConnected ? "ON " : "OFF");
    snprintf(sysBuf, sizeof(sysBuf), "heap:%luK", (unsigned long)(freeHeap / 1024));
    RadarHomeStatus status{radioBuf, sysBuf, static_cast<int>(batteryPercent)};
    RadarHomeRenderer::draw(renderer, kRadarNodes, selectorIndex, status);
    const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), "<", ">");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  drawStatusBar();

  // Status info row
  constexpr int statusRowY = 42;
  char statusBuf[64];
  snprintf(statusBuf, sizeof(statusBuf), "WiFi: %s | %luK | %s", wifiConnected ? "on" : "off",
           (unsigned long)(freeHeap / 1024), uptimeStr);
  renderer.drawText(SMALL_FONT_ID, 14, statusRowY, statusBuf);

  // Tile grid
  constexpr int statusBarH = 40;
  constexpr int buttonHintsH = 40;
  constexpr int sidePad = 14;
  constexpr int tileGap = 6;
  constexpr int gridTop = statusBarH + 32;
  const int gridBottom = pageHeight - buttonHintsH - 2;
  const int gridHeight = gridBottom - gridTop;

  const int tileW = (pageWidth - sidePad * 2 - tileGap) / COLS;
  const int tileH = (gridHeight - tileGap * (ROWS - 1)) / ROWS;

  for (int i = 0; i < ITEM_COUNT; i++) {
    int row = i / COLS;
    int col = i % COLS;
    int x = sidePad + col * (tileW + tileGap);
    int y = gridTop + row * (tileH + tileGap);
    drawTile(i, x, y, tileW, tileH, i == selectorIndex);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "^", "v");
  renderer.displayBuffer();
}

void AppsMenuActivity::refreshSystemInfo() {
  freeHeap = esp_get_free_heap_size();
  uptimeSeconds = (unsigned long)(esp_timer_get_time() / 1000000LL);
  batteryPercent = (uint8_t)powerManager.getBatteryPercentage();
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  lastInfoRefresh = millis();

  unsigned long hrs = uptimeSeconds / 3600;
  unsigned long mins = (uptimeSeconds % 3600) / 60;
  if (hrs > 0) {
    snprintf(uptimeStr, sizeof(uptimeStr), "%luh%02lum", hrs, mins);
  } else {
    snprintf(uptimeStr, sizeof(uptimeStr), "%lum", mins);
  }
}

void AppsMenuActivity::loadLastUsed() {
  for (int i = 0; i < ITEM_COUNT; i++) {
    lastUsedName[i][0] = '\0';
    char path[44];
    snprintf(path, sizeof(path), "/.crosspoint/lastused_%d.txt", i);
    FsFile file;
    if (Storage.openFileForRead("APPS", path, file)) {
      int len = file.read((uint8_t*)lastUsedName[i], 31);
      if (len > 0) {
        lastUsedName[i][len] = '\0';
        if (lastUsedName[i][len - 1] == '\n') lastUsedName[i][len - 1] = '\0';
      }
      file.close();
    }
  }
}

void AppsMenuActivity::drawStatusBar() const {
  const auto pageWidth = renderer.getScreenWidth();
  constexpr int pad = 14;

  renderer.drawText(UI_12_FONT_ID, pad, 10, "crosspoint.", true, EpdFontFamily::BOLD);

  int rightX = pageWidth - pad;

  int uptimeW = renderer.getTextWidth(SMALL_FONT_ID, uptimeStr);
  renderer.drawText(SMALL_FONT_ID, rightX - uptimeW, 14, uptimeStr);
  rightX -= uptimeW + 10;

  char heapStr[16];
  snprintf(heapStr, sizeof(heapStr), "%luK", (unsigned long)(freeHeap / 1024));
  int heapW = renderer.getTextWidth(SMALL_FONT_ID, heapStr);
  renderer.drawText(SMALL_FONT_ID, rightX - heapW, 14, heapStr);
  rightX -= heapW + 10;

  if (wifiConnected) {
    renderer.fillRect(rightX - 6, 16, 6, 6, true);
  } else {
    renderer.drawRect(rightX - 6, 16, 6, 6, true);
  }
  rightX -= 14;

  GUI.drawBatteryRight(renderer, Rect{rightX - 16, 14, 15, 12});
  renderer.drawLine(pad, 38, pageWidth - pad, 38, true);
}

void AppsMenuActivity::drawTile(int index, int x, int y, int w, int h, bool selected) const {
  if (selected) {
    renderer.fillRect(x, y, w, h, true);
  } else {
    renderer.drawRect(x, y, w, h, true);
  }

  constexpr int pad = 10;
  int nameY = y + pad;

  struct TileInfo {
    const char* name;
    const char* subtitle;
    int appCount;
  };
  static constexpr TileInfo kTiles[ITEM_COUNT] = {
      {"COMMS", "ESP-NOW chat", 1},
      {"TOOLS", "Productivity", 4},
      {"CRYPTO", "Cipher & codes", 3},
      {"GAMES", "Entertainment", 5},
      {"READER", "Books & OPDS", 4},
      {"FILES", "SD card", 1},
      {"SYSTEM", "Device info", 3},
      {"SETTINGS", "Config & transfer", 2},
  };

  const TileInfo& ti = kTiles[index];
  renderer.drawText(UI_12_FONT_ID, x + pad, nameY, ti.name, !selected, EpdFontFamily::BOLD);
  nameY += renderer.getLineHeight(UI_12_FONT_ID) + 2;
  renderer.drawText(SMALL_FONT_ID, x + pad, nameY, ti.subtitle, !selected);

  int countY = y + h - pad - renderer.getLineHeight(SMALL_FONT_ID);
  if (ti.appCount > 0) {
    char countStr[16];
    snprintf(countStr, sizeof(countStr), "%d app%s", ti.appCount, ti.appCount == 1 ? "" : "s");
    int countW = renderer.getTextWidth(SMALL_FONT_ID, countStr);
    renderer.drawText(SMALL_FONT_ID, x + w - pad - countW, countY, countStr, !selected);
  }

  // Live status on selected tile
  if (selected) {
    char statusStr[48] = "";
    switch (index) {
      case 0:  // COMMS
        snprintf(statusStr, sizeof(statusStr), "ESP-NOW: ready");
        break;
      case 1:  // TOOLS
      case 2:  // CRYPTO
      case 3:  // GAMES
      case 4:  // READER
      case 5:  // FILES
        if (lastUsedName[index][0] != '\0') {
          snprintf(statusStr, sizeof(statusStr), "Last: %s", lastUsedName[index]);
        }
        break;
      case 6:  // SYSTEM
        snprintf(statusStr, sizeof(statusStr), "Heap: %luK", (unsigned long)(freeHeap / 1024));
        break;
      default:
        break;
    }
    if (statusStr[0] != '\0') {
      int statusY = countY - renderer.getLineHeight(SMALL_FONT_ID) - 4;
      renderer.drawText(SMALL_FONT_ID, x + pad, statusY, statusStr, !selected);
    }
  }
}
