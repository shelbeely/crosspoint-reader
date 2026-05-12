#pragma once
#include <cstdint>

/**
 * Manages the WiFi station MAC address.
 *
 * Provides helpers to:
 *  - read the current effective MAC (factored or randomized)
 *  - apply a locally-administered random MAC via esp_wifi_set_mac()
 *  - restore the factory eFuse MAC
 *  - format a MAC as a short "xx:xx" (last two octets) string for the status bar
 *    or as a full "xx:xx:xx:xx:xx:xx" string for display in apps
 *
 * MAC changes take effect only when the WiFi driver is NOT running.
 * Callers must call RadioManager::shutdown() before randomizing, then
 * call the desired ensureXxx() afterwards.
 *
 * Thread safety: all methods must be called from the main Arduino loop task.
 */
class MacManager {
 public:
  static MacManager& getInstance() {
    static MacManager instance;
    return instance;
  }

  /**
   * Read the current effective STA MAC into buf[6].
   * Falls back to the eFuse factory MAC if the radio is off.
   */
  void getMac(uint8_t buf[6]) const;

  /**
   * Apply a locally-administered random MAC.
   * The WiFi driver must be stopped before calling this.
   * Returns true on success.
   */
  bool randomize();

  /**
   * Restore the factory eFuse MAC.
   * The WiFi driver must be stopped before calling this.
   * Returns true on success.
   */
  bool restore();

  /** True if the current MAC differs from the factory eFuse MAC. */
  bool isRandomized() const;

  /** Format full MAC: "aa:bb:cc:dd:ee:ff" (18 bytes including NUL). */
  static void formatFull(const uint8_t mac[6], char out[18]);

  /** Format last two octets: "ee:ff" (6 bytes including NUL). */
  static void formatShort(const uint8_t mac[6], char out[6]);

 private:
  MacManager() = default;
};

#define MAC_MGR MacManager::getInstance()
