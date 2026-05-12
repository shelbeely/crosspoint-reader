#include "TodoActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <StreamingJsonParser.h>

#include <cstring>
#include <algorithm>
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

struct TodoLoadCtx {
  TodoActivity::TodoItem* items;
  int* count;
  int maxItems;
  int depth = 0;
  enum Field : uint8_t { NONE, TEXT, DONE } field = NONE;
};

void todoOnKey(void* ctx, const char* key, size_t len) {
  auto* c = static_cast<TodoLoadCtx*>(ctx);
  if (strncmp(key, "text", len) == 0 && len == 4) c->field = TodoLoadCtx::TEXT;
  else if (strncmp(key, "done", len) == 0 && len == 4) c->field = TodoLoadCtx::DONE;
  else c->field = TodoLoadCtx::NONE;
}

void todoOnString(void* ctx, const char* value, size_t len) {
  auto* c = static_cast<TodoLoadCtx*>(ctx);
  if (*c->count >= c->maxItems || c->field != TodoLoadCtx::TEXT) return;
  snprintf(c->items[*c->count].text, 64, "%.*s", (int)len, value);
  c->field = TodoLoadCtx::NONE;
}

void todoOnBool(void* ctx, bool value) {
  auto* c = static_cast<TodoLoadCtx*>(ctx);
  if (*c->count >= c->maxItems || c->field != TodoLoadCtx::DONE) return;
  c->items[*c->count].done = value;
  c->field = TodoLoadCtx::NONE;
}

void todoOnObjectStart(void* ctx) {
  auto* c = static_cast<TodoLoadCtx*>(ctx);
  if (c->depth == 1 && *c->count < c->maxItems) {
    c->items[*c->count].text[0] = '\0';
    c->items[*c->count].done = false;
  }
  c->depth++;
}

void todoOnObjectEnd(void* ctx) {
  auto* c = static_cast<TodoLoadCtx*>(ctx);
  c->depth--;
  if (c->depth == 1 && *c->count < c->maxItems) (*c->count)++;
}

void todoOnArrayStart(void* ctx) { static_cast<TodoLoadCtx*>(ctx)->depth++; }
void todoOnArrayEnd(void* ctx) { static_cast<TodoLoadCtx*>(ctx)->depth--; }
void todoOnNumber(void*, const char*, size_t) {}
void todoOnNull(void*) {}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void TodoActivity::onEnter() {
  Activity::onEnter();
  itemCount = 0;
  selector = 0;
  dirty = false;
  loadItems();
  requestUpdate();
}

void TodoActivity::onExit() {
  if (dirty) saveItems();
  Activity::onExit();
}

void TodoActivity::loop() {
  // Hold-confirm on a task → delete; hold past last → add
  if (mappedInput.isHeld(MappedInputManager::Button::Confirm)) {
    if (!confirmHeld) {
      confirmHeld = true;
      confirmPressMs = millis();
    } else if (!confirmLongHandled && millis() - confirmPressMs >= HOLD_MS) {
      confirmLongHandled = true;
      if (selector < itemCount) {
        startDeleteItem(selector);
      } else {
        startAddItem();
      }
      return;
    }
  } else {
    if (confirmHeld && !confirmLongHandled && selector < itemCount) {
      // Tap confirm → toggle done
      items[selector].done = !items[selector].done;
      dirty = true;
      requestUpdate();
    }
    confirmHeld = false;
    confirmLongHandled = false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  // selector can go one past the last item (the "add new" virtual slot)
  const int maxSel = itemCount;  // 0..itemCount inclusive
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) && selector < maxSel) {
    selector++;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) && selector > 0) {
    selector--;
    requestUpdate();
  }
}

void TodoActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, tr(STR_TODO));

  int y = metrics.topPadding + metrics.headerHeight + 4;
  const int itemH = metrics.listRowHeight;
  const int visCount = (pageH - y - metrics.buttonHintsHeight) / itemH - 1;  // leave room for "add" row

  if (itemCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, y + 20, tr(STR_NO_TASKS));
  } else {
    int first = selector - visCount / 2;
    if (first < 0) first = 0;
    if (first + visCount > itemCount) first = std::max(0, itemCount - visCount);

    for (int i = first; i < first + visCount && i < itemCount; i++) {
      bool sel = (i == selector);
      if (sel) renderer.fillRect(0, y, pageW, itemH, true);

      char label[68];
      snprintf(label, sizeof(label), "[%c] %s", items[i].done ? 'x' : ' ', items[i].text);
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y + 6, label, !sel);
      y += itemH;
    }
  }

  // "Add new" virtual row
  {
    bool sel = (selector == itemCount);
    if (sel) renderer.fillRect(0, y, pageW, itemH, true);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y + 6, tr(STR_NEW_TASK), !sel);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "^", "v");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
// Load / save
// ---------------------------------------------------------------------------

void TodoActivity::loadItems() {
  FsFile file;
  if (!Storage.openFileForRead("TODO", kDataPath, file)) return;

  TodoLoadCtx ctx{items, &itemCount, MAX_ITEMS};
  JsonCallbacks cb{&ctx, todoOnKey, todoOnString, todoOnNumber, todoOnBool, todoOnNull,
                   todoOnObjectStart, todoOnObjectEnd, todoOnArrayStart, todoOnArrayEnd};
  StreamingJsonParser parser(cb);

  char buf[256];
  int n;
  while ((n = file.read((uint8_t*)buf, sizeof(buf))) > 0) {
    parser.feed(buf, (size_t)n);
  }
  file.close();
  LOG_INF("TODO", "Loaded %d todo items", itemCount);
}

void TodoActivity::saveItems() const {
  Storage.ensureDirectoryExists("/.crosspoint");
  FsFile file;
  if (!Storage.openFileForWrite("TODO", kDataPath, file)) {
    LOG_ERR("TODO", "Cannot write todo.json");
    return;
  }
  file.write((const uint8_t*)"[\n", 2);
  for (int i = 0; i < itemCount; i++) {
    char line[96];
    int n = snprintf(line, sizeof(line), "  {\"text\":\"%s\",\"done\":%s}%s\n",
                     items[i].text, items[i].done ? "true" : "false", (i < itemCount - 1) ? "," : "");
    file.write((const uint8_t*)line, (size_t)n);
  }
  file.write((const uint8_t*)"]\n", 2);
  file.close();
  LOG_INF("TODO", "Saved %d todo items", itemCount);
}

void TodoActivity::saveAndClearDirty() {
  saveItems();
  dirty = false;
}

// ---------------------------------------------------------------------------
// Add / delete
// ---------------------------------------------------------------------------

void TodoActivity::startAddItem() {
  if (itemCount >= MAX_ITEMS) return;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_NEW_TASK), "", 63),
      [this](const ActivityResult& r) {
        if (!r.isCancelled && std::holds_alternative<KeyboardResult>(r.data)) {
          const std::string& text = std::get<KeyboardResult>(r.data).text;
          if (!text.empty() && itemCount < MAX_ITEMS) {
            snprintf(items[itemCount].text, 64, "%s", text.c_str());
            items[itemCount].done = false;
            itemCount++;
            selector = itemCount - 1;
            dirty = true;
            saveAndClearDirty();
          }
        }
        requestUpdate();
      });
}

void TodoActivity::startDeleteItem(int idx) {
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_TASK), items[idx].text),
      [this, idx](const ActivityResult& r) {
        if (!r.isCancelled) {
          // Remove item
          for (int i = idx; i < itemCount - 1; i++) items[i] = items[i + 1];
          itemCount--;
          if (selector >= itemCount && selector > 0) selector--;
          dirty = true;
          saveAndClearDirty();
        }
        requestUpdate();
      });
}
