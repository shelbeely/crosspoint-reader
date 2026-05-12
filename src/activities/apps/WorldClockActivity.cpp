#include "WorldClockActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <StreamingJsonParser.h>
#include <time.h>

#include <cstring>
#include <variant>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ---------------------------------------------------------------------------
// JSON loader
// ---------------------------------------------------------------------------

namespace {

struct WcLoadCtx {
  WorldClockActivity::ClockEntry* clocks;
  int* count;
  int maxClocks;
  int depth = 0;
  enum Field : uint8_t { NONE, LABEL, OFFSET } field = NONE;
};

void wcOnKey(void* ctx, const char* key, size_t len) {
  auto* c = static_cast<WcLoadCtx*>(ctx);
  if (strncmp(key, "label", len) == 0 && len == 5) c->field = WcLoadCtx::LABEL;
  else if (strncmp(key, "utc_offset_min", len) == 0 && len == 14) c->field = WcLoadCtx::OFFSET;
  else c->field = WcLoadCtx::NONE;
}

void wcOnString(void* ctx, const char* value, size_t len) {
  auto* c = static_cast<WcLoadCtx*>(ctx);
  if (*c->count >= c->maxClocks || c->field != WcLoadCtx::LABEL) return;
  snprintf(c->clocks[*c->count].label, 16, "%.*s", (int)len, value);
  c->field = WcLoadCtx::NONE;
}

void wcOnNumber(void* ctx, const char* value, size_t /*len*/) {
  auto* c = static_cast<WcLoadCtx*>(ctx);
  if (*c->count >= c->maxClocks || c->field != WcLoadCtx::OFFSET) return;
  c->clocks[*c->count].utcOffsetMin = (int16_t)atoi(value);
  c->field = WcLoadCtx::NONE;
}

void wcOnObjectStart(void* ctx) {
  auto* c = static_cast<WcLoadCtx*>(ctx);
  if (c->depth == 1 && *c->count < c->maxClocks) {
    c->clocks[*c->count].label[0] = '\0';
    c->clocks[*c->count].utcOffsetMin = 0;
  }
  c->depth++;
}

void wcOnObjectEnd(void* ctx) {
  auto* c = static_cast<WcLoadCtx*>(ctx);
  c->depth--;
  if (c->depth == 1 && *c->count < c->maxClocks) (*c->count)++;
}

void wcOnArrayStart(void* ctx) { static_cast<WcLoadCtx*>(ctx)->depth++; }
void wcOnArrayEnd(void* ctx) { static_cast<WcLoadCtx*>(ctx)->depth--; }
void wcOnBool(void*, bool) {}
void wcOnNull(void*) {}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void WorldClockActivity::onEnter() {
  Activity::onEnter();
  clockCount = 0;
  selector = 0;
  loadClocks();
  lastRefreshMs = millis();
  requestUpdate();
}

void WorldClockActivity::onExit() {
  Activity::onExit();
}

void WorldClockActivity::loop() {
  // Periodic refresh for clock display
  if (millis() - lastRefreshMs >= REFRESH_INTERVAL_MS) {
    lastRefreshMs = millis();
    requestUpdate();
  }

  // Hold-confirm → delete; tap-confirm on empty slot → add
  if (mappedInput.isHeld(MappedInputManager::Button::Confirm)) {
    if (!confirmHeld) {
      confirmHeld = true;
      confirmPressMs = millis();
    } else if (!confirmLongHandled && millis() - confirmPressMs >= HOLD_MS) {
      confirmLongHandled = true;
      if (selector < clockCount) {
        startDeleteClock(selector);
      }
      return;
    }
  } else {
    if (confirmHeld && !confirmLongHandled) {
      // Tap confirm
      if (selector >= clockCount && clockCount < MAX_CLOCKS) {
        // Empty slot → add
        startAddClock(clockCount);
      }
    }
    confirmHeld = false;
    confirmLongHandled = false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const int maxSel = clockCount < MAX_CLOCKS ? clockCount : clockCount - 1;
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) && selector < maxSel) {
    selector++;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) && selector > 0) {
    selector--;
    requestUpdate();
  }
}

void WorldClockActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, tr(STR_WORLD_CLOCK));

  int y = metrics.topPadding + metrics.headerHeight + 8;
  const int rowH = metrics.listWithSubtitleRowHeight;
  const int pad = metrics.contentSidePadding;

  // Get current UTC epoch.  configTime(0, 0, ...) sets no timezone offset so
  // getLocalTime() returns UTC-based time; mktime gives us seconds since Unix epoch.
  time_t utcNow = 0;
  struct tm utcTm = {};
  if (getLocalTime(&utcTm, 0)) {
    utcNow = mktime(&utcTm);
  }

  if (clockCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, y + 20, tr(STR_NO_CLOCKS));
  } else {
    for (int i = 0; i < clockCount; i++) {
      bool sel = (i == selector);
      if (sel) renderer.fillRect(0, y, pageW, rowH, true);

      // Calculate local time for this timezone
      time_t zoneTime = utcNow + (time_t)clocks[i].utcOffsetMin * 60;
      struct tm* zt = gmtime(&zoneTime);

      char timeBuf[12];
      if (zt) {
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", zt->tm_hour, zt->tm_min);
      } else {
        snprintf(timeBuf, sizeof(timeBuf), "--:--");
      }

      renderer.drawText(UI_10_FONT_ID, pad, y + 4, clocks[i].label, !sel, EpdFontFamily::BOLD);

      // Offset label e.g. "+05:30"
      char offBuf[10];
      int absMins = clocks[i].utcOffsetMin < 0 ? -clocks[i].utcOffsetMin : clocks[i].utcOffsetMin;
      snprintf(offBuf, sizeof(offBuf), "%c%02d:%02d", clocks[i].utcOffsetMin >= 0 ? '+' : '-',
               absMins / 60, absMins % 60);

      // Right-align the time
      int timeW = renderer.getTextWidth(UI_12_FONT_ID, timeBuf);
      renderer.drawText(UI_12_FONT_ID, pageW - pad - timeW, y + 2, timeBuf, !sel, EpdFontFamily::BOLD);
      renderer.drawText(SMALL_FONT_ID, pad, y + 4 + renderer.getLineHeight(UI_10_FONT_ID) + 2, offBuf, !sel);

      y += rowH;
    }
  }

  // "Add" virtual row if slots available
  if (clockCount < MAX_CLOCKS) {
    bool sel = (selector == clockCount);
    if (sel) renderer.fillRect(0, y, pageW, rowH, true);
    renderer.drawText(UI_10_FONT_ID, pad, y + 4, tr(STR_ADD_TIMEZONE), !sel);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "^", "v");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
// Load / save
// ---------------------------------------------------------------------------

void WorldClockActivity::loadClocks() {
  FsFile file;
  if (!Storage.openFileForRead("WCK", kDataPath, file)) return;
  WcLoadCtx ctx{clocks, &clockCount, MAX_CLOCKS};
  JsonCallbacks cb{&ctx, wcOnKey, wcOnString, wcOnNumber, wcOnBool, wcOnNull,
                   wcOnObjectStart, wcOnObjectEnd, wcOnArrayStart, wcOnArrayEnd};
  StreamingJsonParser parser(cb);
  char buf[256];
  int n;
  while ((n = file.read((uint8_t*)buf, sizeof(buf))) > 0) parser.feed(buf, (size_t)n);
  file.close();
  LOG_INF("WCK", "Loaded %d world clocks", clockCount);
}

void WorldClockActivity::saveClocks() const {
  Storage.ensureDirectoryExists("/.crosspoint");
  FsFile file;
  if (!Storage.openFileForWrite("WCK", kDataPath, file)) {
    LOG_ERR("WCK", "Cannot write world_clocks.json");
    return;
  }
  file.write((const uint8_t*)"[\n", 2);
  for (int i = 0; i < clockCount; i++) {
    char line[80];
    int n = snprintf(line, sizeof(line), "  {\"label\":\"%s\",\"utc_offset_min\":%d}%s\n",
                     clocks[i].label, (int)clocks[i].utcOffsetMin, i < clockCount - 1 ? "," : "");
    file.write((const uint8_t*)line, (size_t)n);
  }
  file.write((const uint8_t*)"]\n", 2);
  file.close();
}

// ---------------------------------------------------------------------------
// Offset parser  ±HH:MM → minutes
// ---------------------------------------------------------------------------

bool WorldClockActivity::parseOffset(const char* str, int16_t& outMinutes) {
  if (!str || str[0] == '\0') return false;
  int sign = 1;
  int i = 0;
  if (str[0] == '+') { i++; }
  else if (str[0] == '-') { sign = -1; i++; }

  int hours = 0, mins = 0;
  while (str[i] >= '0' && str[i] <= '9') { hours = hours * 10 + (str[i++] - '0'); }
  if (str[i] == ':') i++;
  while (str[i] >= '0' && str[i] <= '9') { mins = mins * 10 + (str[i++] - '0'); }

  if (hours > 14 || mins > 59) return false;
  outMinutes = (int16_t)(sign * (hours * 60 + mins));
  return true;
}

// ---------------------------------------------------------------------------
// Add / delete
// ---------------------------------------------------------------------------

void WorldClockActivity::startAddClock(int slot) {
  if (slot >= MAX_CLOCKS) return;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TIMEZONE_LABEL), "", 15),
      [this](const ActivityResult& r) {
        if (r.isCancelled || !std::holds_alternative<KeyboardResult>(r.data)) { requestUpdate(); return; }
        const std::string& labelStr = std::get<KeyboardResult>(r.data).text;
        if (labelStr.empty()) { requestUpdate(); return; }

        std::string capturedLabel = labelStr;
        startActivityForResult(
            std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_UTC_OFFSET), "+00:00", 8),
            [this, capturedLabel](const ActivityResult& r2) {
              if (!r2.isCancelled && std::holds_alternative<KeyboardResult>(r2.data)) {
                const std::string& offStr = std::get<KeyboardResult>(r2.data).text;
                int16_t offsetMin = 0;
                if (parseOffset(offStr.c_str(), offsetMin) && clockCount < MAX_CLOCKS) {
                  snprintf(clocks[clockCount].label, 16, "%s", capturedLabel.c_str());
                  clocks[clockCount].utcOffsetMin = offsetMin;
                  clockCount++;
                  saveClocks();
                }
              }
              requestUpdate();
            });
      });
}

void WorldClockActivity::startDeleteClock(int idx) {
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_CLOCK), clocks[idx].label),
      [this, idx](const ActivityResult& r) {
        if (!r.isCancelled) {
          for (int i = idx; i < clockCount - 1; i++) clocks[i] = clocks[i + 1];
          clockCount--;
          if (selector >= clockCount && selector > 0) selector--;
          saveClocks();
        }
        requestUpdate();
      });
}
