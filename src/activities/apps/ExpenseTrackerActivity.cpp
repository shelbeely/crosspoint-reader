#include "ExpenseTrackerActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <StreamingJsonParser.h>
#include <time.h>

#include <cstring>
#include <algorithm>
#include <variant>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ---------------------------------------------------------------------------
// Category names
// ---------------------------------------------------------------------------

constexpr const char* ExpenseTrackerActivity::kCategories[CATEGORY_COUNT] = {
    "Food", "Transport", "Health", "Work", "Other"};

// ---------------------------------------------------------------------------
// JSON loader
// ---------------------------------------------------------------------------

namespace {

struct ExpLoadCtx {
  ExpenseTrackerActivity::Expense* expenses;
  int* count;
  int maxEntries;
  int depth = 0;
  enum Field : uint8_t { NONE, DATE, AMOUNT, CATEGORY, NOTE } field = NONE;
};

void expOnKey(void* ctx, const char* key, size_t len) {
  auto* c = static_cast<ExpLoadCtx*>(ctx);
  if (strncmp(key, "date", len) == 0 && len == 4) c->field = ExpLoadCtx::DATE;
  else if (strncmp(key, "amount_cents", len) == 0 && len == 12) c->field = ExpLoadCtx::AMOUNT;
  else if (strncmp(key, "category", len) == 0 && len == 8) c->field = ExpLoadCtx::CATEGORY;
  else if (strncmp(key, "note", len) == 0 && len == 4) c->field = ExpLoadCtx::NOTE;
  else c->field = ExpLoadCtx::NONE;
}

void expOnString(void* ctx, const char* value, size_t len) {
  auto* c = static_cast<ExpLoadCtx*>(ctx);
  if (*c->count >= c->maxEntries) return;
  auto& e = c->expenses[*c->count];
  if (c->field == ExpLoadCtx::DATE) {
    snprintf(e.date, sizeof(e.date), "%.*s", (int)len, value);
  } else if (c->field == ExpLoadCtx::CATEGORY) {
    snprintf(e.category, sizeof(e.category), "%.*s", (int)len, value);
  } else if (c->field == ExpLoadCtx::NOTE) {
    snprintf(e.note, sizeof(e.note), "%.*s", (int)len, value);
  }
  c->field = ExpLoadCtx::NONE;
}

void expOnNumber(void* ctx, const char* value, size_t /*len*/) {
  auto* c = static_cast<ExpLoadCtx*>(ctx);
  if (*c->count >= c->maxEntries || c->field != ExpLoadCtx::AMOUNT) return;
  c->expenses[*c->count].amount_cents = (int32_t)atol(value);
  c->field = ExpLoadCtx::NONE;
}

void expOnObjectStart(void* ctx) {
  auto* c = static_cast<ExpLoadCtx*>(ctx);
  if (c->depth == 1 && *c->count < c->maxEntries) {
    auto& e = c->expenses[*c->count];
    e.date[0] = '\0';
    e.amount_cents = 0;
    e.category[0] = '\0';
    e.note[0] = '\0';
  }
  c->depth++;
}

void expOnObjectEnd(void* ctx) {
  auto* c = static_cast<ExpLoadCtx*>(ctx);
  c->depth--;
  if (c->depth == 1 && *c->count < c->maxEntries) (*c->count)++;
}

void expOnArrayStart(void* ctx) { static_cast<ExpLoadCtx*>(ctx)->depth++; }
void expOnArrayEnd(void* ctx) { static_cast<ExpLoadCtx*>(ctx)->depth--; }
void expOnBool(void*, bool) {}
void expOnNull(void*) {}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ExpenseTrackerActivity::onEnter() {
  Activity::onEnter();
  expenses = new Expense[MAX_ENTRIES];
  expenseCount = 0;
  dirty = false;
  view = SUMMARY;
  listSelector = 0;
  amountBufLen = 0;
  amountBuf[0] = '\0';
  addCategoryIdx = 0;
  addEnteringAmount = true;
  loadExpenses();
  requestUpdate();
}

void ExpenseTrackerActivity::onExit() {
  if (dirty) saveExpenses();
  delete[] expenses;
  expenses = nullptr;
  Activity::onExit();
}

void ExpenseTrackerActivity::loop() {
  switch (view) {
    case SUMMARY: loopSummary(); break;
    case LIST:    loopList();    break;
    case ADD:     loopAdd();     break;
  }
}

void ExpenseTrackerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (view) {
    case SUMMARY: renderSummary(); break;
    case LIST:    renderList();    break;
    case ADD:     renderAdd();     break;
  }
  renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
// Load / save
// ---------------------------------------------------------------------------

void ExpenseTrackerActivity::loadExpenses() {
  FsFile file;
  if (!Storage.openFileForRead("EXP", kDataPath, file)) return;
  ExpLoadCtx ctx{expenses, &expenseCount, MAX_ENTRIES};
  JsonCallbacks cb{&ctx, expOnKey, expOnString, expOnNumber, expOnBool, expOnNull,
                   expOnObjectStart, expOnObjectEnd, expOnArrayStart, expOnArrayEnd};
  StreamingJsonParser parser(cb);
  char buf[256];
  int n;
  while ((n = file.read((uint8_t*)buf, sizeof(buf))) > 0) parser.feed(buf, (size_t)n);
  file.close();
  LOG_INF("EXP", "Loaded %d expenses", expenseCount);
}

void ExpenseTrackerActivity::saveExpenses() const {
  Storage.ensureDirectoryExists("/.crosspoint");
  FsFile file;
  if (!Storage.openFileForWrite("EXP", kDataPath, file)) {
    LOG_ERR("EXP", "Cannot write expenses.json");
    return;
  }
  file.write((const uint8_t*)"[\n", 2);
  for (int i = 0; i < expenseCount; i++) {
    const auto& e = expenses[i];
    char line[160];
    int n = snprintf(line, sizeof(line),
                     "  {\"date\":\"%s\",\"amount_cents\":%ld,\"category\":\"%s\",\"note\":\"%s\"}%s\n",
                     e.date, (long)e.amount_cents, e.category, e.note, i < expenseCount - 1 ? "," : "");
    file.write((const uint8_t*)line, (size_t)n);
  }
  file.write((const uint8_t*)"]\n", 2);
  file.close();
  LOG_INF("EXP", "Saved %d expenses", expenseCount);
}

void ExpenseTrackerActivity::exportCsv() const {
  FsFile file;
  if (!Storage.openFileForWrite("EXP", kExportPath, file)) {
    LOG_ERR("EXP", "Cannot write expenses_export.csv");
    return;
  }
  const char* hdr = "Date,Amount (cents),Category,Note\n";
  file.write((const uint8_t*)hdr, strlen(hdr));
  for (int i = 0; i < expenseCount; i++) {
    const auto& e = expenses[i];
    char row[160];
    int n = snprintf(row, sizeof(row), "%s,%ld,%s,%s\n", e.date, (long)e.amount_cents, e.category, e.note);
    file.write((const uint8_t*)row, (size_t)n);
  }
  file.close();
  LOG_INF("EXP", "Exported %d expenses to CSV", expenseCount);
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

void ExpenseTrackerActivity::loopSummary() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    view = LIST;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    // Export CSV
    exportCsv();
    // TODO: show a brief message
    requestUpdate();
  }
  // Confirm tap → open ADD
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    // Start add entry
    amountBufLen = 0;
    amountBuf[0] = '\0';
    addCategoryIdx = 0;
    addEnteringAmount = true;
    view = ADD;
    requestUpdate();
  }
}

void ExpenseTrackerActivity::loopList() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    view = SUMMARY;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) && listSelector < expenseCount - 1) {
    listSelector++;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) && listSelector > 0) {
    listSelector--;
    requestUpdate();
  }
}

void ExpenseTrackerActivity::loopAdd() {
  // Numeric digit entry for amount
  if (addEnteringAmount) {
    // Map button presses to digits (1-5 mapped to buttons available)
    // Simplified: Right = digit 1..9 cycle; Confirm = accept amount; Back = cancel
    if (mappedInput.wasReleased(MappedInputManager::Button::Right) && amountBufLen < 10) {
      // Increment last digit or append 0
      amountBuf[amountBufLen++] = '0';
      amountBuf[amountBufLen] = '\0';
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) && amountBufLen > 0) {
      // Increment last digit
      if (amountBuf[amountBufLen - 1] < '9') amountBuf[amountBufLen - 1]++;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down) && amountBufLen > 0) {
      if (amountBuf[amountBufLen - 1] > '0') amountBuf[amountBufLen - 1]--;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Left) && amountBufLen > 0) {
      amountBuf[--amountBufLen] = '\0';
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && amountBufLen > 0) {
      addEnteringAmount = false;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      view = SUMMARY;
      requestUpdate();
    }
    return;
  }

  // Category picker
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    addCategoryIdx = (addCategoryIdx + 1) % CATEGORY_COUNT;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    addCategoryIdx = (addCategoryIdx + CATEGORY_COUNT - 1) % CATEGORY_COUNT;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    addEnteringAmount = true;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    int32_t amountCents = (int32_t)atol(amountBuf);
    startAddNote(amountCents, addCategoryIdx);
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void ExpenseTrackerActivity::renderSummary() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, tr(STR_EXPENSES));

  // Calculate total and per-category totals
  int64_t total = 0;
  int64_t catTotals[CATEGORY_COUNT] = {};
  for (int i = 0; i < expenseCount; i++) {
    total += expenses[i].amount_cents;
    for (int c = 0; c < CATEGORY_COUNT; c++) {
      if (strncmp(expenses[i].category, kCategories[c], sizeof(expenses[i].category) - 1) == 0) {
        catTotals[c] += expenses[i].amount_cents;
        break;
      }
    }
  }

  int y = metrics.topPadding + metrics.headerHeight + 8;
  const int pad = metrics.contentSidePadding;

  char line[64];
  snprintf(line, sizeof(line), "Total: %lld.%02lld", (long long)(total / 100), (long long)(total % 100));
  renderer.drawText(UI_12_FONT_ID, pad, y, line, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 8;

  // Bar chart — max bar width
  const int barMaxW = pageW - pad * 2 - 80;
  int64_t maxCat = 1;
  for (int c = 0; c < CATEGORY_COUNT; c++) if (catTotals[c] > maxCat) maxCat = catTotals[c];

  for (int c = 0; c < CATEGORY_COUNT; c++) {
    snprintf(line, sizeof(line), "%-10s", kCategories[c]);
    renderer.drawText(SMALL_FONT_ID, pad, y, line);
    int barW = (int)((catTotals[c] * barMaxW) / maxCat);
    if (barW > 0) renderer.fillRect(pad + 72, y, barW, 10, true);
    snprintf(line, sizeof(line), " %lld", (long long)(catTotals[c] / 100));
    renderer.drawText(SMALL_FONT_ID, pad + 72 + barMaxW + 4, y, line);
    y += 16;
  }

  if (expenseCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, y + 20, tr(STR_NO_EXPENSES));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_LIST), tr(STR_ADD_EXPENSE), tr(STR_EXPORT_CSV));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ExpenseTrackerActivity::renderList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, tr(STR_EXPENSES));

  if (expenseCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, metrics.topPadding + metrics.headerHeight + 40, tr(STR_NO_EXPENSES));
  } else {
    int y = metrics.topPadding + metrics.headerHeight + 4;
    const int itemH = metrics.listWithSubtitleRowHeight;
    const int visCount = (pageH - y - metrics.buttonHintsHeight) / itemH;
    int first = listSelector - visCount / 2;
    if (first < 0) first = 0;
    // Show newest-first (reverse order)
    for (int i = 0; i < visCount && (first + i) < expenseCount; i++) {
      int idx = expenseCount - 1 - (first + i);
      bool sel = ((first + i) == listSelector);
      if (sel) renderer.fillRect(0, y, pageW, itemH, true);

      char line1[48], line2[48];
      snprintf(line1, sizeof(line1), "%s  %s", expenses[idx].date, expenses[idx].category);
      snprintf(line2, sizeof(line2), "%ld.%02ld  %s", (long)(expenses[idx].amount_cents / 100),
               (long)(expenses[idx].amount_cents % 100), expenses[idx].note);
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y + 4, line1, !sel, EpdFontFamily::BOLD);
      renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding,
                        y + 4 + renderer.getLineHeight(UI_10_FONT_ID) + 2, line2, !sel);
      y += itemH;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "^", "v");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ExpenseTrackerActivity::renderAdd() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, tr(STR_ADD_EXPENSE));

  int y = metrics.topPadding + metrics.headerHeight + 16;
  const int pad = metrics.contentSidePadding;

  if (addEnteringAmount) {
    char amtDisplay[24];
    if (amountBufLen == 0) {
      snprintf(amtDisplay, sizeof(amtDisplay), "0");
    } else {
      snprintf(amtDisplay, sizeof(amtDisplay), "%s", amountBuf);
    }
    renderer.drawText(UI_10_FONT_ID, pad, y, tr(STR_AMOUNT));
    y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
    renderer.drawText(UI_12_FONT_ID, pad, y, amtDisplay, true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "^+1", "v-1");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    renderer.drawText(UI_10_FONT_ID, pad, y, tr(STR_CATEGORY));
    y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
    renderer.drawText(UI_12_FONT_ID, pad, y, kCategories[addCategoryIdx], true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "<", ">");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}

// ---------------------------------------------------------------------------
// Add note and commit
// ---------------------------------------------------------------------------

void ExpenseTrackerActivity::startAddNote(int32_t amount_cents, int catIdx) {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ADD_EXPENSE), "", 63),
      [this, amount_cents, catIdx](const ActivityResult& r) {
        std::string note;
        if (!r.isCancelled && std::holds_alternative<KeyboardResult>(r.data)) {
          note = std::get<KeyboardResult>(r.data).text;
        }
        if (expenseCount < MAX_ENTRIES) {
          auto& e = expenses[expenseCount];
          // Get current date
          struct tm ti = {};
          if (getLocalTime(&ti, 0)) {
            snprintf(e.date, sizeof(e.date), "%04d-%02d-%02d",
                     ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
          } else {
            snprintf(e.date, sizeof(e.date), "0000-00-00");
          }
          e.amount_cents = amount_cents;
          snprintf(e.category, sizeof(e.category), "%s", kCategories[catIdx]);
          snprintf(e.note, sizeof(e.note), "%s", note.c_str());
          expenseCount++;
          saveExpenses();
        }
        view = SUMMARY;
        requestUpdate();
      });
}
