#include "ClaudeBuddyActivity.h"

#include <ArduinoJson.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_mac.h>
#include <esp_timer.h>

#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

ClaudeBuddyActivity* ClaudeBuddyActivity::activeInstance = nullptr;

// ---------------------------------------------------------------------------
// ASCII pet data — static const so the tables live in flash (.rodata).
// Layout: art[petState][lineIndex]; nullptr terminates the line list.
// State indices: 0=sleep 1=idle 2=busy 3=attention 4=celebrate 5=heart
// ---------------------------------------------------------------------------
namespace {

struct PetDef {
  const char* name;
  const char* art[6][6];
};

static const PetDef kPets[ClaudeBuddyActivity::PET_COUNT] = {
    // ── Pet 0: Cosmo (cat) ──────────────────────────────────────────────────
    {"Cosmo",
     {
         // sleep
         {"  zzZz ", " /\\_/\\", "(-.-) ", " > ^ <", "  UU  ", nullptr},
         // idle
         {" /\\_/\\", "(o.o) ", " > ^ <", "  UU  ", nullptr, nullptr},
         // busy
         {" /\\_/\\", "(@.@) ", ">~^~< ", "  UU  ", nullptr, nullptr},
         // attention
         {" !! !!", " /\\_/\\", "(O.O) ", " > ! <", "  UU  ", nullptr},
         // celebrate
         {"* * * *", " /\\_/\\", "(^.^) ", " >*^*<", "  UU  ", nullptr},
         // heart
         {"  <3<3 ", " /\\_/\\", "(^.^) ", " >v^v<", "  UU  ", nullptr},
     }},
    // ── Pet 1: Bleep (robot) ────────────────────────────────────────────────
    {"Bleep",
     {
         // sleep
         {" [zZz] ", " |. .| ", "  [_]  ", "   ||  ", "  [__] ", nullptr},
         // idle
         {" [---] ", " |o o| ", "  [_]  ", "   ||  ", "  [__] ", nullptr},
         // busy
         {" [===] ", " |# #| ", "  [=]  ", "  =||= ", "  [__] ", nullptr},
         // attention
         {" [!!!] ", " |! !| ", "  [!]  ", "   ||  ", "  [__] ", nullptr},
         // celebrate
         {" [***] ", " |^ ^| ", "  [*]  ", "   ||  ", "  [__] ", nullptr},
         // heart
         {"  [<3] ", " |^ ^| ", "  [v]  ", "   ||  ", "  [__] ", nullptr},
     }},
    // ── Pet 2: Boo (ghost) ──────────────────────────────────────────────────
    {"Boo",
     {
         // sleep
         {"  zz  ", " (-.-)  ", "  ) (  ", " ~~~   ", nullptr, nullptr},
         // idle
         {" (o.o) ", "  ) (  ", " ~~~   ", nullptr, nullptr, nullptr},
         // busy
         {" (@.@) ", "  )=(  ", " ~~~   ", nullptr, nullptr, nullptr},
         // attention
         {"  !!  ", " (O.O) ", "  )!(  ", " ~~~   ", nullptr, nullptr},
         // celebrate
         {"  * *  ", " (^.^) ", "  )*(  ", " ~*~   ", nullptr, nullptr},
         // heart
         {"  <3  ", " (^.^) ", "  )<3  ", " ~~~   ", nullptr, nullptr},
     }},
};

// ---------------------------------------------------------------------------
// File-scope BLE callback objects — must outlive the activity.
// They delegate to the static methods on ClaudeBuddyActivity so the header
// stays free of BLE library callback inheritance.
// ---------------------------------------------------------------------------
class BuddyServerCallbacks : public BLEServerCallbacks {
 public:
  void onConnect(BLEServer*) override { ClaudeBuddyActivity::onBleConnect(); }
  void onDisconnect(BLEServer* s) override { ClaudeBuddyActivity::onBleDisconnect(s); }
};

class BuddyRxCallbacks : public BLECharacteristicCallbacks {
 public:
  void onWrite(BLECharacteristic* p) override { ClaudeBuddyActivity::onRxWrite(p); }
};

BuddyServerCallbacks gServerCbs;
BuddyRxCallbacks gRxCbs;

}  // namespace

// ---------------------------------------------------------------------------
// Activity lifecycle
// ---------------------------------------------------------------------------

void ClaudeBuddyActivity::onEnter() {
  Activity::onEnter();

  // Reset all state
  memset(&stats, 0, sizeof(stats));
  state = IDLE;
  bleInitialized = false;
  bleServer = nullptr;
  txChar = nullptr;
  rxLen = 0;
  bleConnectedEvent = false;
  bleDisconnectedEvent = false;
  pendingRender = false;
  celebrateUntil = 0;
  heartUntil = 0;
  prevTokensToday = 0;
  activeInstance = this;

  // Read WiFi STA MAC from eFuse (no radio required) to build the device name.
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(deviceName, sizeof(deviceName), "ClaudeX4-%02X%02X", mac[4], mac[5]);

  if (!RADIO.ensureBle(deviceName)) {
    LOG_ERR("BUDDY", "BLE init failed");
    requestUpdate();
    return;
  }

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(&gServerCbs);

  BLEService* service = bleServer->createService(NUS_SERVICE_UUID);

  // TX: device → desktop (Notify). Desktop writes CCCD to enable notifications.
  txChar = service->createCharacteristic(NUS_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());

  // RX: desktop → device (Write | Write without response).
  BLECharacteristic* rxChar = service->createCharacteristic(
      NUS_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxChar->setCallbacks(&gRxCbs);

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(NUS_SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  bleInitialized = true;
  state = ADVERTISING;
  LOG_INF("BUDDY", "Advertising as %s, heap: %d", deviceName, ESP.getFreeHeap());

  requestUpdate();
}

void ClaudeBuddyActivity::onExit() {
  Activity::onExit();
  if (bleInitialized) {
    BLEDevice::getAdvertising()->stop();
    // Null the pointers before deinit — BLEDevice::deinit() frees them.
    txChar = nullptr;
    bleServer = nullptr;
    bleInitialized = false;
  }
  RADIO.shutdown();
  activeInstance = nullptr;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void ClaudeBuddyActivity::loop() {
  // BLE-002: drain volatile event flags set by BLE-task callbacks.
  if (pendingRender) {
    pendingRender = false;
    requestUpdate();
  }
  if (bleConnectedEvent) {
    bleConnectedEvent = false;
    state = CONNECTED;
    requestUpdate();
  }
  if (bleDisconnectedEvent) {
    bleDisconnectedEvent = false;
    if (bleInitialized) {
      state = ADVERTISING;
      BLEDevice::startAdvertising();
    }
    requestUpdate();
  }

  // Token milestone check — every new 50 K of tokens_today triggers celebrate.
  {
    uint32_t cur = 0;
    portENTER_CRITICAL(&statsMux);
    cur = stats.tokensToday;
    portEXIT_CRITICAL(&statsMux);
    if (cur > 0 && cur / 50000 > prevTokensToday / 50000) {
      celebrateUntil = millis() + 8000;
      requestUpdate();
    }
    prevTokensToday = cur;
  }

  // Button handling
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (state == CONNECTED) {
    bool hasPending = false;
    char promptIdSnap[28] = {};
    portENTER_CRITICAL(&statsMux);
    hasPending = stats.hasPendingPrompt;
    if (hasPending) memcpy(promptIdSnap, stats.promptId, sizeof(promptIdSnap));
    portEXIT_CRITICAL(&statsMux);

    if (hasPending) {
      // Confirm = approve once
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"once\"}\n", promptIdSnap);
        sendJson(buf);
        portENTER_CRITICAL(&statsMux);
        stats.hasPendingPrompt = false;
        portEXIT_CRITICAL(&statsMux);
        heartUntil = millis() + 5000;  // heart animation for 5 s after fast approval
        requestUpdate();
      }
      // Right = deny
      if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"deny\"}\n", promptIdSnap);
        sendJson(buf);
        portENTER_CRITICAL(&statsMux);
        stats.hasPendingPrompt = false;
        portEXIT_CRITICAL(&statsMux);
        requestUpdate();
      }
    } else {
      // Confirm cycles the ASCII pet when no prompt is waiting.
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        petIndex = (petIndex + 1) % PET_COUNT;
        requestUpdate();
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void ClaudeBuddyActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // Snapshot stats under the spinlock, then render without holding it.
  BuddyStats snap = {};
  portENTER_CRITICAL(&statsMux);
  snap = stats;
  portEXIT_CRITICAL(&statsMux);

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();

  // Header
  char subtitle[52];
  if (state == CONNECTED) {
    if (snap.ownerName[0]) {
      snprintf(subtitle, sizeof(subtitle), "Connected \xe2\x80\x94 %s", snap.ownerName);
    } else {
      snprintf(subtitle, sizeof(subtitle), "Connected");
    }
  } else if (state == ADVERTISING) {
    snprintf(subtitle, sizeof(subtitle), "Advertising...");
  } else {
    snprintf(subtitle, sizeof(subtitle), "BLE off");
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight},
                 "Claude Buddy", subtitle);

  // Pet (left column) + stats (right column)
  renderPet(derivePetState(snap));
  renderStats(snap);

  // Button hints
  const bool hasPending = snap.hasPendingPrompt && (state == CONNECTED);
  if (hasPending) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Approve", "", "Deny");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Pet >", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}

ClaudeBuddyActivity::PetState ClaudeBuddyActivity::derivePetState(const BuddyStats& snap) const {
  if (state != CONNECTED) return PET_SLEEP;
  if (snap.waiting > 0) return PET_ATTENTION;
  const unsigned long now = millis();
  if (heartUntil > 0 && now < heartUntil) return PET_HEART;
  if (celebrateUntil > 0 && now < celebrateUntil) return PET_CELEBRATE;
  if (snap.running > 0) return PET_BUSY;
  return PET_IDLE;
}

void ClaudeBuddyActivity::renderPet(PetState petState) const {
  static constexpr int PET_ZONE_W = 300;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom =
      renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const PetDef& pet = kPets[petIndex];
  const char* const* lines = pet.art[static_cast<int>(petState)];

  // Count non-null lines
  int lineCount = 0;
  for (int i = 0; i < 6 && lines[i] != nullptr; i++) lineCount++;

  const int lineH = renderer.getLineHeight(UI_12_FONT_ID) + 3;
  const int blockH = lineCount * lineH;
  int y = (contentTop + contentBottom - blockH) / 2;

  // Pet name above the art block
  const int nameH = renderer.getLineHeight(UI_10_FONT_ID);
  int nameY = y - nameH - 4;
  if (nameY >= contentTop) {
    int nameW = renderer.getTextWidth(UI_10_FONT_ID, pet.name);
    renderer.drawText(UI_10_FONT_ID, (PET_ZONE_W - nameW) / 2, nameY, pet.name);
  }

  // Art lines, each centered in the pet zone
  for (int i = 0; i < 6; i++) {
    const char* line = lines[i];
    if (!line) break;
    int lineW = renderer.getTextWidth(UI_12_FONT_ID, line);
    int x = (PET_ZONE_W - lineW) / 2;
    if (x < 0) x = 0;
    renderer.drawText(UI_12_FONT_ID, x, y, line);
    y += lineH;
  }

  // Vertical separator between pet zone and stats zone
  renderer.drawLine(PET_ZONE_W, contentTop, PET_ZONE_W, contentBottom, true);
}

void ClaudeBuddyActivity::renderStats(const BuddyStats& snap) const {
  static constexpr int STATS_X = 310;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lineH = renderer.getLineHeight(SMALL_FONT_ID) + 4;
  int y = contentTop;

  if (state == IDLE) {
    renderer.drawText(SMALL_FONT_ID, STATS_X, y, "BLE not initialized");
    return;
  }

  if (state == ADVERTISING) {
    renderer.drawText(SMALL_FONT_ID, STATS_X, y, "Advertising as:");
    y += lineH;
    renderer.drawText(UI_12_FONT_ID, STATS_X, y, deviceName);
    y += renderer.getLineHeight(UI_12_FONT_ID) + 10;
    renderer.drawText(SMALL_FONT_ID, STATS_X, y, "To connect from Claude Desktop:");
    y += lineH;
    renderer.drawText(SMALL_FONT_ID, STATS_X, y, "1. Help > Troubleshooting >");
    y += lineH;
    renderer.drawText(SMALL_FONT_ID, STATS_X, y, "   Enable Developer Mode");
    y += lineH;
    renderer.drawText(SMALL_FONT_ID, STATS_X, y, "2. Developer > Open Hardware Buddy...");
    y += lineH;
    renderer.drawText(SMALL_FONT_ID, STATS_X, y, "3. Click Connect and pick this device");
    // Spinner to indicate active advertising
    const int spinnerY = y + lineH * 2;
    GUI.drawSpinner(renderer, STATS_X + 80, spinnerY, nullptr,
                    static_cast<int>(millis() / 600));
    return;
  }

  // CONNECTED — live heartbeat stats
  char line[80];

  snprintf(line, sizeof(line), "%d sessions  %d running  %d waiting", snap.total, snap.running,
           snap.waiting);
  renderer.drawText(SMALL_FONT_ID, STATS_X, y, line);
  y += lineH;

  if (snap.tokensToday > 0 || snap.tokens > 0) {
    snprintf(line, sizeof(line), "Today: %lu  Total: %lu", (unsigned long)snap.tokensToday,
             (unsigned long)snap.tokens);
    renderer.drawText(SMALL_FONT_ID, STATS_X, y, line);
    y += lineH;
  }

  if (snap.msg[0]) {
    renderer.drawText(SMALL_FONT_ID, STATS_X, y, snap.msg);
    y += lineH;
  }

  if (snap.updatedAt > 0) {
    unsigned long ageSec = (millis() - snap.updatedAt) / 1000;
    snprintf(line, sizeof(line), "Updated %lus ago", ageSec);
    int w = renderer.getTextWidth(SMALL_FONT_ID, line);
    renderer.drawText(SMALL_FONT_ID, pageW - metrics.contentSidePadding - w, y, line);
    y += lineH;
  }

  // Pending permission prompt — draw a separator and the tool + hint
  if (snap.hasPendingPrompt) {
    y += lineH / 2;
    renderer.drawLine(STATS_X, y, pageW - metrics.contentSidePadding, y, true);
    y += lineH / 2;

    snprintf(line, sizeof(line), "Approve: %s", snap.promptTool);
    renderer.drawText(UI_10_FONT_ID, STATS_X, y, line);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

    if (snap.promptHint[0]) {
      renderer.drawText(SMALL_FONT_ID, STATS_X + 8, y, snap.promptHint);
    }
  }
}

// ---------------------------------------------------------------------------
// BLE helpers
// ---------------------------------------------------------------------------

void ClaudeBuddyActivity::sendJson(const char* json) {
  if (!txChar || state != CONNECTED) return;
  // Copy to a local buffer so we can pass uint8_t* without casting away const
  // (Arduino BLE setValue takes non-const uint8_t* but only reads the data).
  static constexpr size_t MAX_SEND = 200;
  size_t len = strlen(json);
  if (len > MAX_SEND) len = MAX_SEND;
  uint8_t buf[MAX_SEND];
  memcpy(buf, json, len);
  txChar->setValue(buf, len);
  txChar->notify();
}

void ClaudeBuddyActivity::sendStatusAck() {
  char buf[160];
  uint32_t upSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
  uint32_t heap = static_cast<uint32_t>(ESP.getFreeHeap());
  snprintf(buf, sizeof(buf),
           "{\"ack\":\"status\",\"ok\":true,\"data\":{\"name\":\"%s\","
           "\"sec\":false,\"sys\":{\"up\":%lu,\"heap\":%lu}}}\n",
           deviceName, static_cast<unsigned long>(upSec), static_cast<unsigned long>(heap));
  sendJson(buf);
}

// ---------------------------------------------------------------------------
// Protocol parser — called from onRxWrite() in the BLE task context.
// Must not hold statsMux when calling sendJson().
// ---------------------------------------------------------------------------

void ClaudeBuddyActivity::processLine(const char* line) {
  if (!line || line[0] == '\0') return;

  // ArduinoJson v7 has no StaticJsonDocument; JsonDocument is the correct v7 API.
  // It allocates its pool from the heap and is freed on scope exit.
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;  // parse error — ignore silently

  // ── Heartbeat snapshot ────────────────────────────────────────────────────
  // Identified by the presence of the "total" field.
  if (!doc["total"].isNull()) {
    portENTER_CRITICAL(&statsMux);
    stats.total = doc["total"] | 0;
    stats.running = doc["running"] | 0;
    stats.waiting = doc["waiting"] | 0;
    stats.tokens = doc["tokens"] | static_cast<uint32_t>(0);
    stats.tokensToday = doc["tokens_today"] | static_cast<uint32_t>(0);
    const char* msgStr = doc["msg"];
    if (msgStr) {
      strncpy(stats.msg, msgStr, sizeof(stats.msg) - 1);
      stats.msg[sizeof(stats.msg) - 1] = '\0';
    } else {
      stats.msg[0] = '\0';
    }
    JsonObject prompt = doc["prompt"].as<JsonObject>();
    if (!prompt.isNull()) {
      stats.hasPendingPrompt = true;
      const char* id = prompt["id"];
      const char* tool = prompt["tool"];
      const char* hint = prompt["hint"];
      if (id) {
        strncpy(stats.promptId, id, sizeof(stats.promptId) - 1);
        stats.promptId[sizeof(stats.promptId) - 1] = '\0';
      } else {
        stats.promptId[0] = '\0';
      }
      if (tool) {
        strncpy(stats.promptTool, tool, sizeof(stats.promptTool) - 1);
        stats.promptTool[sizeof(stats.promptTool) - 1] = '\0';
      } else {
        stats.promptTool[0] = '\0';
      }
      if (hint) {
        strncpy(stats.promptHint, hint, sizeof(stats.promptHint) - 1);
        stats.promptHint[sizeof(stats.promptHint) - 1] = '\0';
      } else {
        stats.promptHint[0] = '\0';
      }
    } else {
      stats.hasPendingPrompt = false;
    }
    stats.updatedAt = millis();
    portEXIT_CRITICAL(&statsMux);
    return;
  }

  // ── Desktop commands ──────────────────────────────────────────────────────
  const char* cmd = doc["cmd"];
  if (cmd) {
    if (strcmp(cmd, "status") == 0) {
      sendStatusAck();
    } else if (strcmp(cmd, "owner") == 0) {
      const char* name = doc["name"];
      if (name) {
        portENTER_CRITICAL(&statsMux);
        strncpy(stats.ownerName, name, sizeof(stats.ownerName) - 1);
        stats.ownerName[sizeof(stats.ownerName) - 1] = '\0';
        portEXIT_CRITICAL(&statsMux);
      }
      sendJson("{\"ack\":\"owner\",\"ok\":true}\n");
    } else if (strcmp(cmd, "name") == 0) {
      sendJson("{\"ack\":\"name\",\"ok\":true}\n");
    } else if (strcmp(cmd, "unpair") == 0) {
      sendJson("{\"ack\":\"unpair\",\"ok\":true}\n");
    }
    return;
  }

  // ── Turn event ────────────────────────────────────────────────────────────
  // Can be up to 4 KB; we only log the role and discard the content.
  const char* evt = doc["evt"];
  if (evt && strcmp(evt, "turn") == 0) {
    LOG_DBG("BUDDY", "Turn event, role: %s", (const char*)doc["role"] ?: "?");
    return;
  }

  // ── Time sync (one-shot on connect) ───────────────────────────────────────
  if (!doc["time"].isNull()) {
    LOG_DBG("BUDDY", "Time sync received");
    return;
  }
}

// ---------------------------------------------------------------------------
// Static BLE callbacks — run from the BLE task.
// ---------------------------------------------------------------------------

void ClaudeBuddyActivity::onBleConnect() {
  if (!activeInstance) return;
  activeInstance->bleConnectedEvent = true;
  activeInstance->pendingRender = true;
  LOG_DBG("BUDDY", "BLE connected");
}

void ClaudeBuddyActivity::onBleDisconnect(BLEServer*) {
  if (!activeInstance) return;
  // Set the flag; the main loop restarts advertising from the main task.
  activeInstance->bleDisconnectedEvent = true;
  activeInstance->pendingRender = true;
  LOG_DBG("BUDDY", "BLE disconnected");
}

void ClaudeBuddyActivity::onRxWrite(BLECharacteristic* characteristic) {
  if (!activeInstance) return;

  // BLE-003: accumulate bytes into a line buffer; process on '\n'.
  // getData()/getLength() avoids the std::string copy from getValue().
  const uint8_t* data = characteristic->getData();
  size_t len = characteristic->getLength();

  for (size_t i = 0; i < len; i++) {
    char c = static_cast<char>(data[i]);
    if (c == '\n') {
      if (activeInstance->rxLen > 0) {
        activeInstance->rxBuf[activeInstance->rxLen] = '\0';
        activeInstance->processLine(activeInstance->rxBuf);
        activeInstance->pendingRender = true;
      }
      activeInstance->rxLen = 0;
    } else {
      if (activeInstance->rxLen < RX_BUF_SIZE - 1) {
        activeInstance->rxBuf[activeInstance->rxLen++] = c;
      }
      // If buffer fills without a newline the line is too large — discard
      // silently (turn events > 512 B are not processed, which is fine).
    }
  }
}
