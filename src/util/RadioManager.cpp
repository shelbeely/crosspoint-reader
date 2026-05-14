#include "RadioManager.h"

#include <BLEDevice.h>
#include <Logging.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

bool RadioManager::ensureWifi() {
  if (state == RadioState::WIFI) return true;

  if (state == RadioState::BLE) {
    deinitBle();
  } else if (state == RadioState::ESPNOW) {
    deinitEspNow();
  }

  WiFi.mode(WIFI_STA);
  state = RadioState::WIFI;
  LOG_DBG("RADIO", "Switched to WiFi mode, heap: %d", ESP.getFreeHeap());
  return true;
}

bool RadioManager::ensureBle(const char* deviceName) {
  if (state == RadioState::BLE) return true;

  if (state == RadioState::WIFI) {
    deinitWifi();
  } else if (state == RadioState::ESPNOW) {
    deinitEspNow();
  }

  BLEDevice::init(deviceName);
  state = RadioState::BLE;
  LOG_DBG("RADIO", "Switched to BLE mode, heap: %d", ESP.getFreeHeap());
  return true;
}

bool RadioManager::ensureEspNow() {
  if (state == RadioState::ESPNOW) return true;

  if (state == RadioState::BLE) {
    deinitBle();
  } else if (state == RadioState::WIFI) {
    deinitWifi();
  }

  // ESP-NOW runs on the WiFi radio in STA mode but without the IP stack.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    LOG_ERR("RADIO", "ESP-NOW init failed, heap: %d", ESP.getFreeHeap());
    WiFi.mode(WIFI_OFF);
    return false;
  }

  state = RadioState::ESPNOW;
  LOG_DBG("RADIO", "Switched to ESP-NOW mode, heap: %d", ESP.getFreeHeap());
  return true;
}

void RadioManager::shutdown() {
  if (state == RadioState::WIFI) deinitWifi();
  if (state == RadioState::BLE) deinitBle();
  if (state == RadioState::ESPNOW) deinitEspNow();
  state = RadioState::OFF;
}

void RadioManager::deinitWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
  LOG_DBG("RADIO", "WiFi deinitialized");
}

void RadioManager::deinitBle() {
  BLEDevice::deinit(false);
  delay(50);
  LOG_DBG("RADIO", "BLE deinitialized");
}

void RadioManager::deinitEspNow() {
  esp_now_unregister_recv_cb();
  esp_now_deinit();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
  LOG_DBG("RADIO", "ESP-NOW deinitialized");
}

bool RadioManager::isDisclaimerAcknowledged() const {
  Preferences prefs;
  prefs.begin("crosspoint", true);
  bool ack = prefs.getBool("disc_ack", false);
  prefs.end();
  return ack;
}

void RadioManager::setDisclaimerAcknowledged() {
  Preferences prefs;
  prefs.begin("crosspoint", false);
  prefs.putBool("disc_ack", true);
  prefs.end();
}
