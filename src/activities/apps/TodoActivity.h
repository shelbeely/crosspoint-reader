#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// TodoActivity — simple task checklist.
//
// Data file: /.crosspoint/todo.json
// Format:    JSON array of { "text":"...", "done":true/false }
// Max items: 64.  Loaded on onEnter, written to SD in onExit only if dirty.
//
// Controls:
//   Up/Down  — navigate items
//   Confirm  — toggle done/undone
//   Hold Confirm on a task — delete (via ConfirmationActivity)
//   Hold Confirm past last item — add new task (via KeyboardEntryActivity)

class TodoActivity final : public Activity {
 public:
  struct TodoItem {
    char text[64];
    bool done;
  };

  explicit TodoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Todo", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int MAX_ITEMS = 64;
  static constexpr const char* kDataPath = "/.crosspoint/todo.json";

  TodoItem items[MAX_ITEMS];
  int itemCount = 0;
  int selector = 0;
  bool dirty = false;

  // Hold-confirm detection
  bool confirmHeld = false;
  bool confirmLongHandled = false;
  static constexpr uint16_t HOLD_MS = 600;
  unsigned long confirmPressMs = 0;

  void loadItems();
  void saveItems() const;
  void saveAndClearDirty();

  void startAddItem();
  void startDeleteItem(int idx);
};
