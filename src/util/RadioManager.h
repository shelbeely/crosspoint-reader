#pragma once
#include <cstdint>

/**
 * Manages WiFi/BLE/ESP-NOW radio coexistence on ESP32-C3.
 * The radio is shared — WiFi and BLE cannot run simultaneously.
 * ESP-NOW runs on top of the WiFi radio (STA mode, no IP stack).
 * Call ensureWifi() before any WiFi operation, ensureBle() before any BLE
 * operation, and ensureEspNow() before any ESP-NOW operation.
 *
 * State machine:
 *
 *   OFF ──ensureWifi()──► WIFI
 *   OFF ──ensureBle()───► BLE
 *   OFF ──ensureEspNow()► ESPNOW
 *   WIFI ──ensureBle()──► BLE    (deinits WiFi first)
 *   BLE ──ensureWifi()──► WIFI   (deinits BLE first)
 *   any ──shutdown()────► OFF
 *
 * Only one state is active at a time. Callers must call shutdown() in their
 * activity's onExit() if they activated a radio mode.
 *
 * Access via the RADIO macro: RADIO.ensureWifi(), RADIO.shutdown(), etc.
 * NVS namespace: "crosspoint".
 */
class RadioManager {
 public:
  enum class RadioState { OFF, WIFI, BLE, ESPNOW };

  static RadioManager& getInstance() {
    static RadioManager instance;
    return instance;
  }

  // Ensure WiFi (IP stack) is available (deinits BLE/ESP-NOW if active)
  bool ensureWifi();

  // Ensure BLE is available (deinits WiFi/ESP-NOW if active)
  bool ensureBle();

  /**
   * Ensure ESP-NOW is available.
   * Sets the radio to WiFi STA mode without the IP stack, then initialises
   * esp_now.  If ESP-NOW is already initialised, this is a no-op.
   * Deinits BLE if it was active.  If WiFi (IP) was active it is torn down
   * first; the caller is responsible for reconnecting WiFi afterwards if
   * needed.
   */
  bool ensureEspNow();

  // Shut down all radios (including ESP-NOW if active)
  void shutdown();

  RadioState getState() const { return state; }

  // Check if disclaimer has been acknowledged (stored in NVS)
  bool isDisclaimerAcknowledged() const;
  void setDisclaimerAcknowledged();

 private:
  RadioManager() = default;
  RadioState state = RadioState::OFF;

  void deinitWifi();
  void deinitBle();
  void deinitEspNow();
};

#define RADIO RadioManager::getInstance()
