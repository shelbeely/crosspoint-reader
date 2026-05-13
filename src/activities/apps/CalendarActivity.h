#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// CalendarActivity — monthly calendar with event storage.
//
// Data file: /.crosspoint/calendar.json
// Format:    JSON array of { "date":"YYYY-MM-DD", "title":"...", "note":"..." }
// Max events: 64 (all loaded into RAM on enter, written back on add).
//
// Views:
//   MONTH  — 7-column Su–Sa grid; days with events show a dot; selected day
//            is inverted.  Left/Right move by day; Up/Down move by week.
//   DAY    — list of event titles for the selected day; Confirm opens DETAIL.
//   DETAIL — full title + note for one event.
//
// Adding an event: hold Confirm on MONTH view → keyboard for title → keyboard
// for note → event appended and file written.

class CalendarActivity final : public Activity {
 public:
  struct CalEvent {
    char date[11];    // "YYYY-MM-DD"
    char title[48];
    char note[128];
  };

  explicit CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Calendar", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int MAX_EVENTS = 64;
  static constexpr const char* kDataPath = "/.crosspoint/calendar.json";

  CalEvent events[MAX_EVENTS];
  int eventCount = 0;
  bool dirty = false;

  // Calendar navigation state
  int viewYear = 2025;
  int viewMonth = 1;  // 1-based
  int selectedDay = 1;

  enum View { MONTH, DAY, DETAIL };
  View view = MONTH;

  // DAY / DETAIL state
  int dayEventIndices[MAX_EVENTS];  // indices into events[] for selected day
  int dayEventCount = 0;
  int daySelector = 0;
  int detailIndex = -1;

  // Hold-confirm detection
  bool confirmHeld = false;
  bool confirmLongHandled = false;
  static constexpr uint16_t HOLD_MS = 600;
  unsigned long confirmPressMs = 0;

  void loadEvents();
  void saveEvents() const;
  void buildDayEventList();

  static int daysInMonth(int year, int month);
  static int dayOfWeek(int year, int month, int day);
  static bool isLeapYear(int year);

  void renderMonth();
  void renderDay();
  void renderDetail();

  void loopMonth();
  void loopDay();
  void loopDetail();

  void startAddEvent();
};
