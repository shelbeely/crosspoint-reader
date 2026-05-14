#pragma once
#include <BLECharacteristic.h>
#include <BLEServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include <cstdint>

#include "activities/Activity.h"

/**
 * ClaudeBuddyActivity
 *
 * Connects to Claude Desktop / Claude Code Desktop over BLE using the
 * Hardware Buddy protocol (Nordic UART Service).  Advertises as
 * "ClaudeX4-XXYY" where XXYY are the last two bytes of the WiFi STA MAC.
 *
 * Protocol (https://github.com/anthropics/claude-desktop-buddy/blob/main/REFERENCE.md):
 *   Desktop → Device (RX characteristic, write):
 *     - Heartbeat snapshot:  {total, running, waiting, msg, tokens,
 *                             tokens_today, prompt?}  sent on change + 10 s keepalive
 *     - Commands:            {cmd:"status"|"name"|"owner"|"unpair", ...}
 *     - Time sync:           {time:[epochSec, tzOffsetSec]}  one-shot on connect
 *   Device → Desktop (TX characteristic, notify):
 *     - Status ack:          {ack:"status", ok:true, data:{name, sec, sys}}
 *     - Permission decision: {cmd:"permission", id:"...", decision:"once"|"deny"}
 *     - Generic acks:        {ack:"<cmd>", ok:true}
 *
 * All messages are newline-delimited UTF-8 JSON.  Incoming bytes are
 * accumulated across MTU-fragmented BLE write packets (BLE-003).
 *
 * Pet states are derived from the live heartbeat and drive the ASCII art
 * companion displayed on the left half of the screen:
 *   sleep     — not connected
 *   idle      — connected, nothing running
 *   busy      — sessions actively generating
 *   attention — a permission prompt is waiting
 *   celebrate — tokens_today crossed a new 50 K milestone
 *   heart     — an approval was sent within the last 5 s
 *
 * Three pets ship by default (Cosmo the cat, Bleep the robot, Boo the ghost);
 * Confirm cycles them when no prompt is pending.
 *
 * Threading:
 *   statsMux (BLE-001) — portMUX_TYPE spinlock protecting BuddyStats;
 *                         written from BLE task, read from render task.
 *   pendingRender / bleConnectedEvent / bleDisconnectedEvent (BLE-002) —
 *                         volatile flags set by BLE callbacks, drained in loop().
 *   rxBuf / rxLen (BLE-003) — only accessed from BLE task; no lock needed.
 */
class ClaudeBuddyActivity final : public Activity {
 public:
  explicit ClaudeBuddyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClaudeBuddy", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return true; }

  // Invoked by the file-scope BLE callback objects defined in the .cpp.
  static void onBleConnect();
  static void onBleDisconnect(BLEServer* server);
  static void onRxWrite(BLECharacteristic* characteristic);

  static constexpr int PET_COUNT = 3;

 private:
  enum State { IDLE, ADVERTISING, CONNECTED };

  // Pet animation states — index must match kPets[][state] in the .cpp.
  enum PetState { PET_SLEEP = 0, PET_IDLE, PET_BUSY, PET_ATTENTION, PET_CELEBRATE, PET_HEART };

  // NUS UUIDs (Nordic UART Service — de-facto serial-over-BLE standard).
  static constexpr const char* NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
  static constexpr const char* NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
  static constexpr const char* NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

  // Dashboard data received in the heartbeat snapshot.
  // BLE-001: guarded by statsMux — written from BLE task, read from render task.
  // mutable so const render helpers can acquire the spinlock.
  struct BuddyStats {
    int total;               // total open sessions
    int running;             // sessions actively generating
    int waiting;             // sessions blocked on a permission prompt
    char msg[64];            // one-line summary from the desktop
    uint32_t tokens;         // cumulative output tokens since desktop start
    uint32_t tokensToday;    // output tokens since local midnight
    char ownerName[24];      // set by cmd:owner on connect
    bool hasPendingPrompt;   // true when prompt.id is populated
    char promptId[28];       // echo back in the permission decision
    char promptTool[24];     // tool name shown to user
    char promptHint[64];     // hint string (e.g. command preview)
    unsigned long updatedAt; // millis() of last heartbeat
  };
  BuddyStats stats = {};
  mutable portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;

  // BLE-002: volatile event flags set by BLE callbacks; drained in loop().
  volatile bool bleConnectedEvent = false;
  volatile bool bleDisconnectedEvent = false;
  volatile bool pendingRender = false;

  State state = IDLE;
  BLEServer* bleServer = nullptr;
  BLECharacteristic* txChar = nullptr;
  char deviceName[20] = {};
  bool bleInitialized = false;

  // BLE-003: line-accumulation buffer for MTU-fragmented RX packets.
  // The NUS protocol sends one JSON object per line (\n-terminated).
  static constexpr int RX_BUF_SIZE = 512;
  char rxBuf[RX_BUF_SIZE] = {};
  int rxLen = 0;

  // Pet state
  int petIndex = 0;
  unsigned long celebrateUntil = 0; // millis() deadline for celebrate state
  unsigned long heartUntil = 0;     // millis() deadline for heart state
  uint32_t prevTokensToday = 0;     // milestone tracking (main task only)

  void sendJson(const char* json);
  void sendStatusAck();
  void processLine(const char* line);

  PetState derivePetState(const BuddyStats& snap) const;
  void renderPet(PetState petState) const;
  void renderStats(const BuddyStats& snap) const;

  static ClaudeBuddyActivity* activeInstance;
};
