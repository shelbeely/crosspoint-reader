#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// ExpenseTrackerActivity — simple expense tracker.
//
// Data file: /.crosspoint/expenses.json
// Format:    JSON array of { "date":"YYYY-MM-DD", "amount_cents":int,
//                            "category":"...", "note":"..." }
// Max entries: 128.
// Amounts stored as integer cents to avoid floating-point on MCU.
//
// Views:
//   SUMMARY — total + per-category ASCII bar chart.
//             Confirm → LIST; Right → export CSV.
//   LIST    — entries sorted newest-first. Up/Down scroll. Back → SUMMARY.
//   ADD     — numeric entry for amount, category picker, optional note.

class ExpenseTrackerActivity final : public Activity {
 public:
  struct Expense {
    char date[11];           // "YYYY-MM-DD"
    int32_t amount_cents;
    char category[24];
    char note[64];
  };

  static constexpr int MAX_ENTRIES = 128;
  static constexpr int CATEGORY_COUNT = 5;
  static constexpr const char* kCategories[CATEGORY_COUNT];

  explicit ExpenseTrackerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Expenses", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr const char* kDataPath = "/.crosspoint/expenses.json";
  static constexpr const char* kExportPath = "/expenses_export.csv";

  Expense* expenses = nullptr;  // heap-allocated MAX_ENTRIES
  int expenseCount = 0;
  bool dirty = false;

  enum View { SUMMARY, LIST, ADD };
  View view = SUMMARY;

  int listSelector = 0;

  // ADD state
  char amountBuf[12] = {};  // raw digit string for numeric entry
  int amountBufLen = 0;
  int addCategoryIdx = 0;
  bool addEnteringAmount = true;  // true = amount entry, false = category pick

  void loadExpenses();
  void saveExpenses() const;
  void exportCsv() const;

  void renderSummary();
  void renderList();
  void renderAdd();

  void loopSummary();
  void loopList();
  void loopAdd();

  void startAddNote(int32_t amount_cents, int catIdx);
};
