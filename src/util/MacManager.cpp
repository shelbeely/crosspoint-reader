#include "MacManager.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_efuse.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_wifi.h>

#include <cstring>

void MacManager::getMac(uint8_t buf[6]) const {
  // esp_wifi_get_mac returns the current effective MAC while the driver is up.
  // If the driver is off, fall back to the eFuse default.
  if (esp_wifi_get_mac(WIFI_IF_STA, buf) != ESP_OK) {
    esp_efuse_mac_get_default(buf);
  }
}

bool MacManager::randomize() {
  // The locally-administered bit (bit 1 of octet 0) must be set and
  // the multicast bit (bit 0 of octet 0) must be clear.
  uint8_t mac[6];
  esp_fill_random(mac, sizeof(mac));
  mac[0] = (mac[0] & 0xFE) | 0x02;  // locally administered, unicast

  esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, mac);
  if (err != ESP_OK) {
    LOG_ERR("MAC", "esp_wifi_set_mac failed: %d", err);
    return false;
  }
  LOG_INF("MAC", "Randomized MAC: %02x:%02x:%02x:%02x:%02x:%02x",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return true;
}

bool MacManager::restore() {
  uint8_t factory[6];
  esp_efuse_mac_get_default(factory);

  esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, factory);
  if (err != ESP_OK) {
    LOG_ERR("MAC", "esp_wifi_set_mac (restore) failed: %d", err);
    return false;
  }
  LOG_INF("MAC", "Restored factory MAC: %02x:%02x:%02x:%02x:%02x:%02x",
          factory[0], factory[1], factory[2], factory[3], factory[4], factory[5]);
  return true;
}

bool MacManager::isRandomized() const {
  uint8_t cur[6], factory[6];
  getMac(cur);
  esp_efuse_mac_get_default(factory);
  return memcmp(cur, factory, 6) != 0;
}

void MacManager::formatFull(const uint8_t mac[6], char out[18]) {
  snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void MacManager::formatShort(const uint8_t mac[6], char out[6]) {
  snprintf(out, 6, "%02x:%02x", mac[4], mac[5]);
}
