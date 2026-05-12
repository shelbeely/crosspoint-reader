#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// WorldClockActivity — multi-timezone clock display.
//
// Data file: /.crosspoint/world_clocks.json
// Format:    JSON array of { "label":"...", "utc_offset_min":int }
// Max zones: 6.  Persisted to SD after add/delete.
//
// View:
//   Vertical list of timezone rows, each showing label + HH:MM calculated
//   from system NTP time + UTC offset.  Refreshes every second.
//
// Add: Confirm on an empty slot → keyboard for label → keyboard for offset
//      in format ±HH:MM.
// Delete: hold Confirm on a filled slot → ConfirmationActivity → delete.

class WorldClockActivity final : public Activity {
 public:
  struct ClockEntry {
    char label[16];
    int16_t utcOffsetMin;  // ±UTC offset in minutes
  };

  static constexpr int MAX_CLOCKS = 6;

  explicit WorldClockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("WorldClock", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr const char* kDataPath = "/.crosspoint/world_clocks.json";

  ClockEntry clocks[MAX_CLOCKS];
  int clockCount = 0;
  int selector = 0;

  unsigned long lastRefreshMs = 0;
  static constexpr unsigned long REFRESH_INTERVAL_MS = 1000;

  // Hold-confirm detection
  bool confirmHeld = false;
  bool confirmLongHandled = false;
  static constexpr uint16_t HOLD_MS = 600;
  unsigned long confirmPressMs = 0;

  void loadClocks();
  void saveClocks() const;

  void startAddClock(int slot);
  void startDeleteClock(int idx);

  // Parse ±HH:MM string into total offset minutes; returns false on error
  static bool parseOffset(const char* str, int16_t& outMinutes);
};
