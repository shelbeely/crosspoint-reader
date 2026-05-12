#include "MacRandomizerActivity.h"

#include <GfxRenderer.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/MacManager.h"
#include "util/RadioManager.h"

void MacRandomizerActivity::onEnter() {
  Activity::onEnter();
  statusMsg[0] = '\0';
  refreshMac();
  requestUpdate();
}

void MacRandomizerActivity::onExit() { Activity::onExit(); }

void MacRandomizerActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // Confirm = randomize
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    doRandomize();
    return;
  }

  // Down = restore factory MAC
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    doRestore();
    return;
  }
}

void MacRandomizerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "MAC Randomizer", isRandomized ? "[RANDOMIZED]" : "[FACTORY]");

  const int x = metrics.contentSidePadding;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID) + 10;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  // Current MAC
  renderer.drawText(SMALL_FONT_ID, x, y, "Current MAC:");
  y += renderer.getLineHeight(SMALL_FONT_ID) + 4;
  renderer.drawText(UI_12_FONT_ID, x, y, currentMacStr, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + lineH;

  // Instructions
  renderer.drawText(UI_10_FONT_ID, x, y, isRandomized ? "Factory MAC is stored." : "MAC is currently factory.");
  y += lineH;

  // Status message
  if (statusMsg[0] != '\0') {
    y += 4;
    renderer.drawLine(x, y, pageWidth - x, y, true);
    y += 8;
    renderer.drawText(SMALL_FONT_ID, x, y, statusMsg);
  }

  const auto labels = mappedInput.mapLabels("Back", "Randomize", "", "Restore");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

// ── private ──────────────────────────────────────────────────────────────────

void MacRandomizerActivity::refreshMac() {
  uint8_t mac[6];
  MAC_MGR.getMac(mac);
  MacManager::formatFull(mac, currentMacStr);
  isRandomized = MAC_MGR.isRandomized();
}

void MacRandomizerActivity::doRandomize() {
  // Radio must be stopped before esp_wifi_set_mac() can be called.
  RADIO.shutdown();

  // Bring WiFi up briefly so the driver is initialised (needed for set_mac).
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (MAC_MGR.randomize()) {
    snprintf(statusMsg, sizeof(statusMsg), "MAC randomized successfully.");
  } else {
    snprintf(statusMsg, sizeof(statusMsg), "Randomize failed — see logs.");
  }

  WiFi.mode(WIFI_OFF);
  refreshMac();
  requestUpdate();
}

void MacRandomizerActivity::doRestore() {
  RADIO.shutdown();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (MAC_MGR.restore()) {
    snprintf(statusMsg, sizeof(statusMsg), "Factory MAC restored.");
  } else {
    snprintf(statusMsg, sizeof(statusMsg), "Restore failed — see logs.");
  }

  WiFi.mode(WIFI_OFF);
  refreshMac();
  requestUpdate();
}
