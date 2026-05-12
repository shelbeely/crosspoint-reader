#include "CalendarActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <StreamingJsonParser.h>
#include <time.h>

#include <algorithm>
#include <cstring>
#include <variant>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ---------------------------------------------------------------------------
// JSON loader — streaming parse of calendar.json
// ---------------------------------------------------------------------------

namespace {

struct LoadCtx {
  CalendarActivity::CalEvent* events;
  int* count;
  int maxEvents;
  int depth = 0;

  enum Field : uint8_t { NONE, DATE, TITLE, NOTE } field = NONE;
};

void calOnKey(void* ctx, const char* key, size_t len) {
  auto* c = static_cast<LoadCtx*>(ctx);
  if (strncmp(key, "date", len) == 0 && len == 4)
    c->field = LoadCtx::DATE;
  else if (strncmp(key, "title", len) == 0 && len == 5)
    c->field = LoadCtx::TITLE;
  else if (strncmp(key, "note", len) == 0 && len == 4)
    c->field = LoadCtx::NOTE;
  else
    c->field = LoadCtx::NONE;
}

void calOnString(void* ctx, const char* value, size_t len) {
  auto* c = static_cast<LoadCtx*>(ctx);
  if (*c->count >= c->maxEvents) return;
  auto& ev = c->events[*c->count];
  switch (c->field) {
    case LoadCtx::DATE:
      snprintf(ev.date, sizeof(ev.date), "%.*s", (int)len, value);
      break;
    case LoadCtx::TITLE:
      snprintf(ev.title, sizeof(ev.title), "%.*s", (int)len, value);
      break;
    case LoadCtx::NOTE:
      snprintf(ev.note, sizeof(ev.note), "%.*s", (int)len, value);
      break;
    default:
      break;
  }
  c->field = LoadCtx::NONE;
}

void calOnObjectStart(void* ctx) {
  auto* c = static_cast<LoadCtx*>(ctx);
  if (c->depth == 1 && *c->count < c->maxEvents) {
    auto& ev = c->events[*c->count];
    ev.date[0] = '\0';
    ev.title[0] = '\0';
    ev.note[0] = '\0';
  }
  c->depth++;
}

void calOnObjectEnd(void* ctx) {
  auto* c = static_cast<LoadCtx*>(ctx);
  c->depth--;
  if (c->depth == 1 && *c->count < c->maxEvents) (*c->count)++;
}

void calOnArrayStart(void* ctx) { static_cast<LoadCtx*>(ctx)->depth++; }
void calOnArrayEnd(void* ctx) { static_cast<LoadCtx*>(ctx)->depth--; }
void calOnNumber(void*, const char*, size_t) {}
void calOnBool(void*, bool) {}
void calOnNull(void*) {}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void CalendarActivity::onEnter() {
  Activity::onEnter();

  struct tm ti = {};
  if (getLocalTime(&ti, 0)) {
    viewYear = ti.tm_year + 1900;
    viewMonth = ti.tm_mon + 1;
    selectedDay = ti.tm_mday;
  } else {
    viewYear = 2025;
    viewMonth = 1;
    selectedDay = 1;
  }

  view = MONTH;
  loadEvents();
  requestUpdate();
}

void CalendarActivity::onExit() {
  if (dirty) saveEvents();
  Activity::onExit();
}

void CalendarActivity::loop() {
  switch (view) {
    case MONTH:  loopMonth();  break;
    case DAY:    loopDay();    break;
    case DETAIL: loopDetail(); break;
  }
}

void CalendarActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (view) {
    case MONTH:  renderMonth();  break;
    case DAY:    renderDay();    break;
    case DETAIL: renderDetail(); break;
  }
  renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
// Load / save
// ---------------------------------------------------------------------------

void CalendarActivity::loadEvents() {
  eventCount = 0;
  FsFile file;
  if (!Storage.openFileForRead("CAL", kDataPath, file)) return;

  LoadCtx ctx{events, &eventCount, MAX_EVENTS};
  JsonCallbacks cb{&ctx, calOnKey, calOnString, calOnNumber, calOnBool, calOnNull,
                   calOnObjectStart, calOnObjectEnd, calOnArrayStart, calOnArrayEnd};
  StreamingJsonParser parser(cb);

  char buf[256];
  int n;
  while ((n = file.read((uint8_t*)buf, sizeof(buf))) > 0) {
    parser.feed(buf, (size_t)n);
  }
  file.close();
  LOG_INF("CAL", "Loaded %d calendar events", eventCount);
}

void CalendarActivity::saveEvents() const {
  Storage.ensureDirectoryExists("/.crosspoint");
  FsFile file;
  if (!Storage.openFileForWrite("CAL", kDataPath, file)) {
    LOG_ERR("CAL", "Cannot write calendar.json");
    return;
  }
  file.write((const uint8_t*)"[\n", 2);
  for (int i = 0; i < eventCount; i++) {
    const auto& ev = events[i];
    char line[256];
    int n = snprintf(line, sizeof(line), "  {\"date\":\"%s\",\"title\":\"%s\",\"note\":\"%s\"}%s\n",
                     ev.date, ev.title, ev.note, (i < eventCount - 1) ? "," : "");
    file.write((const uint8_t*)line, (size_t)n);
  }
  file.write((const uint8_t*)"]\n", 2);
  file.close();
  LOG_INF("CAL", "Saved %d calendar events", eventCount);
}

// ---------------------------------------------------------------------------
// Date helpers
// ---------------------------------------------------------------------------

bool CalendarActivity::isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int CalendarActivity::daysInMonth(int year, int month) {
  static constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) return 29;
  return kDays[month - 1];
}

int CalendarActivity::dayOfWeek(int year, int month, int day) {
  // Tomohiko Sakamoto's algorithm — returns 0=Sun
  static constexpr int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month < 3) year--;
  return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}

void CalendarActivity::buildDayEventList() {
  dayEventCount = 0;
  char dateStr[11];
  snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", viewYear, viewMonth, selectedDay);
  for (int i = 0; i < eventCount && dayEventCount < MAX_EVENTS; i++) {
    if (strncmp(events[i].date, dateStr, 10) == 0) {
      dayEventIndices[dayEventCount++] = i;
    }
  }
  daySelector = 0;
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

void CalendarActivity::loopMonth() {
  const int dim = daysInMonth(viewYear, viewMonth);

  // Hold-confirm → add event; tap-confirm → day detail
  if (mappedInput.isHeld(MappedInputManager::Button::Confirm)) {
    if (!confirmHeld) {
      confirmHeld = true;
      confirmPressMs = millis();
    } else if (!confirmLongHandled && millis() - confirmPressMs >= HOLD_MS) {
      confirmLongHandled = true;
      startAddEvent();
      return;
    }
  } else {
    if (confirmHeld && !confirmLongHandled) {
      buildDayEventList();
      view = DAY;
      requestUpdate();
    }
    confirmHeld = false;
    confirmLongHandled = false;
  }

  bool changed = false;

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    selectedDay++;
    if (selectedDay > dim) {
      selectedDay = 1;
      if (++viewMonth > 12) { viewMonth = 1; viewYear++; }
    }
    changed = true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    selectedDay--;
    if (selectedDay < 1) {
      if (--viewMonth < 1) { viewMonth = 12; viewYear--; }
      selectedDay = daysInMonth(viewYear, viewMonth);
    }
    changed = true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    selectedDay = std::min(selectedDay + 7, dim);
    changed = true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    selectedDay = std::max(selectedDay - 7, 1);
    changed = true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (changed) requestUpdate();
}

void CalendarActivity::loopDay() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    view = MONTH;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) && daySelector < dayEventCount - 1) {
    daySelector++;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) && daySelector > 0) {
    daySelector--;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && dayEventCount > 0) {
    detailIndex = dayEventIndices[daySelector];
    view = DETAIL;
    requestUpdate();
  }
}

void CalendarActivity::loopDetail() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    view = DAY;
    requestUpdate();
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void CalendarActivity::renderMonth() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();

  static constexpr const char* kDayNames[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
  static constexpr const char* kMonthNames[12] = {
      "January", "February", "March", "April", "May", "June",
      "July", "August", "September", "October", "November", "December"};

  char hdr[32];
  snprintf(hdr, sizeof(hdr), "%s %d", kMonthNames[viewMonth - 1], viewYear);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, hdr);

  const int gridTop = metrics.topPadding + metrics.headerHeight + 4;
  const int cellW = pageW / 7;
  const int cellH = 36;

  for (int d = 0; d < 7; d++) {
    int tw = renderer.getTextWidth(SMALL_FONT_ID, kDayNames[d]);
    renderer.drawText(SMALL_FONT_ID, d * cellW + (cellW - tw) / 2, gridTop, kDayNames[d]);
  }
  renderer.drawLine(0, gridTop + 14, pageW, gridTop + 14, true);

  const int firstDow = dayOfWeek(viewYear, viewMonth, 1);
  const int dim = daysInMonth(viewYear, viewMonth);

  // Mark days that have events
  bool hasEvent[32] = {};
  char prefix[8];
  snprintf(prefix, sizeof(prefix), "%04d-%02d", viewYear, viewMonth);
  for (int i = 0; i < eventCount; i++) {
    if (strncmp(events[i].date, prefix, 7) == 0) {
      int d = (events[i].date[8] - '0') * 10 + (events[i].date[9] - '0');
      if (d >= 1 && d <= 31) hasEvent[d] = true;
    }
  }

  const int startRow = gridTop + 18;
  for (int day = 1; day <= dim; day++) {
    int slot = firstDow + day - 1;
    int row = slot / 7;
    int col = slot % 7;
    int cx = col * cellW;
    int cy = startRow + row * cellH;

    bool sel = (day == selectedDay);
    if (sel) renderer.fillRect(cx, cy, cellW - 1, cellH - 2, true);

    char dayStr[4];
    snprintf(dayStr, sizeof(dayStr), "%d", day);
    int tw = renderer.getTextWidth(UI_10_FONT_ID, dayStr);
    renderer.drawText(UI_10_FONT_ID, cx + (cellW - tw) / 2, cy + 4, dayStr, !sel);

    if (hasEvent[day]) {
      renderer.fillRect(cx + cellW / 2 - 1, cy + cellH - 6, 3, 3, !sel);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void CalendarActivity::renderDay() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();

  char hdr[24];
  snprintf(hdr, sizeof(hdr), "%04d-%02d-%02d", viewYear, viewMonth, selectedDay);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, hdr);

  int y = metrics.topPadding + metrics.headerHeight + 8;

  if (dayEventCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, y + 40, tr(STR_NO_EVENTS));
  } else {
    for (int i = 0; i < dayEventCount; i++) {
      const auto& ev = events[dayEventIndices[i]];
      bool sel = (i == daySelector);
      if (sel) renderer.fillRect(0, y, pageW, metrics.listRowHeight, true);
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y + 6, ev.title, !sel);
      y += metrics.listRowHeight;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "^", "v");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void CalendarActivity::renderDetail() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();

  if (detailIndex < 0 || detailIndex >= eventCount) { view = DAY; return; }
  const auto& ev = events[detailIndex];

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, ev.date);

  int y = metrics.topPadding + metrics.headerHeight + 12;
  renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, y, ev.title, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 8;
  if (ev.note[0] != '\0') {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, ev.note);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------------------------------------------------------------------------
// Add event
// ---------------------------------------------------------------------------

void CalendarActivity::startAddEvent() {
  if (eventCount >= MAX_EVENTS) return;

  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_EVENT_TITLE), "", 47),
      [this](const ActivityResult& r) {
        if (r.isCancelled || !std::holds_alternative<KeyboardResult>(r.data)) return;
        const std::string& titleStr = std::get<KeyboardResult>(r.data).text;
        if (titleStr.empty()) return;

        std::string capturedTitle = titleStr;
        startActivityForResult(
            std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_EVENT_NOTE), "", 127),
            [this, capturedTitle](const ActivityResult& r2) {
              std::string noteStr;
              if (!r2.isCancelled && std::holds_alternative<KeyboardResult>(r2.data)) {
                noteStr = std::get<KeyboardResult>(r2.data).text;
              }
              if (eventCount < MAX_EVENTS) {
                auto& ev = events[eventCount++];
                snprintf(ev.date, sizeof(ev.date), "%04d-%02d-%02d", viewYear, viewMonth, selectedDay);
                snprintf(ev.title, sizeof(ev.title), "%s", capturedTitle.c_str());
                snprintf(ev.note, sizeof(ev.note), "%s", noteStr.c_str());
                dirty = false;
                saveEvents();
              }
              requestUpdate();
            });
      });
}
