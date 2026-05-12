#pragma once
#include <cstdint>
#include <vector>
#include <string>

#include "activities/Activity.h"

/**
 * KarmaAttackActivity
 *
 * Implements a basic Karma attack:
 *   1. Puts the radio into promiscuous mode to sniff probe-request frames.
 *   2. For each unique SSID seen, opens an AP with that exact SSID so clients
 *      believe their trusted network is present.
 *   3. Captures the list of probed SSIDs and the number of times each was seen.
 *   4. Displays the live probe list; pressing Confirm opens the AP with the
 *      currently selected (most-probed) SSID.
 *
 * Because the ESP32-C3 has a single radio, promiscuous sniffing and hosting an
 * AP cannot run simultaneously. The activity alternates:
 *   SNIFF phase (5 s) → pick most-probed SSID → AP phase (10 s) → repeat.
 *
 * WARNING: This tool is for educational / authorised security testing only.
 */
class KarmaAttackActivity final : public Activity {
 public:
  explicit KarmaAttackActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("KarmaAttack", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  // ── Phase control ────────────────────────────────────────────────────────
  enum class Phase { IDLE, SNIFF, AP };
  Phase phase = Phase::IDLE;
  unsigned long phaseStart = 0;
  static constexpr unsigned long SNIFF_MS = 5000;
  static constexpr unsigned long AP_MS = 10000;

  // ── Probe list ───────────────────────────────────────────────────────────
  struct ProbeEntry {
    char ssid[33];
    uint32_t count;
  };
  static constexpr int MAX_PROBES = 24;
  ProbeEntry probes[MAX_PROBES] = {};
  int probeCount = 0;
  int scrollOffset = 0;

  // ── Current AP target ───────────────────────────────────────────────────
  char activeApSsid[33] = "";

  // ── Running flag ────────────────────────────────────────────────────────
  bool running = false;

  // ── ISR-safe probe callback ──────────────────────────────────────────────
  static void IRAM_ATTR promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type);
  static KarmaAttackActivity* activeInstance;

  // ── Helpers ─────────────────────────────────────────────────────────────
  void startSniff();
  void stopSniff();
  void startAp(const char* ssid);
  void stopAp();
  void recordProbe(const char* ssid);
  const char* bestSsid() const;  // most-probed SSID, or nullptr
};
