---
title: Adding an activity
parent: Contributing
nav_order: 6
---

# Adding an activity

This guide walks you through creating a new screen from scratch. By the end you will have a working activity that appears in a menu, renders content, responds to button input, uses translated strings, and can be tested in the simulator.

## 1. Choose the right directory

Put your activity in the `src/activities/` subdirectory that matches its purpose:

| Directory | Use for |
|-----------|---------|
| `apps/` | A new app tile in the biscuit dashboard |
| `reader/` | A new reading format or in-reader UI |
| `settings/` | A new settings or configuration screen |
| `network/` | WiFi/server-related screens |
| `home/` | Home, library, bookmarks |
| `boot_sleep/` | Boot or sleep transitions |

## 2. Create the header

```cpp
// src/activities/apps/MyActivity.h
#pragma once
#include "activities/Activity.h"

class MyActivity : public Activity {
 public:
  MyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("MyActivity", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;
};
```

## 3. Implement the source file

```cpp
// src/activities/apps/MyActivity.cpp
#include "MyActivity.h"
#include <I18n.h>
#include <Logging.h>

void MyActivity::onEnter() {
  Activity::onEnter();
  // allocate buffers, open files, start tasks
  LOG_INF("MyActivity", "enter");
  requestUpdate();
}

void MyActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  // handle other input, update state
}

void MyActivity::render(RenderLock&& lock) {
  renderer.clearScreen();
  renderer.drawCenteredText(FONT_ID_REGULAR, 100, tr(STR_MY_ACTIVITY_TITLE));
  renderer.displayBuffer();
}

void MyActivity::onExit() {
  // free resources in reverse order of allocation
  Activity::onExit();
}
```

Key rules to follow:
- All user-visible strings go through `tr(STR_KEY)` — never hardcode UI text
- Use `LOG_INF` / `LOG_DBG` / `LOG_ERR` for logging — they may use hardcoded English
- Use `MappedInputManager::Button::*` enums — never raw button indices
- Open files in `onEnter()`, close in `onExit()` — use `HalFile` / `FsFile`
- Allocate large buffers on the heap in `onEnter()`, free in `onExit()`
- Keep stack locals under 256 bytes

## 4. Add translated strings

Open `lib/I18n/translations/en.yaml` and add your string keys:

```yaml
STR_MY_ACTIVITY_TITLE: "My App"
STR_MY_ACTIVITY_DESCRIPTION: "Does something useful"
```

Add the same keys to every other language file under `lib/I18n/translations/`. If you do not have a translation, duplicate the English value — it is better than a missing key that causes a compile error.

Regenerate the C++ headers:

```sh
python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/
```

This runs automatically as a pre-build step in PlatformIO, but running it manually lets you catch typos before building.

## 5. Wire into a menu

### Appearing in the apps dashboard

To add a tile to `AppsMenuActivity`, open `src/activities/apps/AppsMenuActivity.cpp` and add a `buildCategory()` entry in the right tile. Each entry needs a tile title, an icon (`UIIcon::*`), and a factory lambda:

```cpp
// inside buildCategory(TILE_TOOLS):
addApp(tr(STR_MY_ACTIVITY_TITLE), UIIcon::STAR, [](auto& r, auto& m) {
    return std::make_unique<MyActivity>(r, m);
});
```

Include your header at the top of `AppsMenuActivity.cpp`.

### Appearing in a sub-category

`AppCategoryActivity` hosts a list of items, each with a title and launch callback. Instantiate it from your parent activity and push it.

### Deep-linked navigation

For screens reachable from `ActivityManager` directly, add a `goTo...()` method to `ActivityManager.h` / `ActivityManager.cpp`:

```cpp
// ActivityManager.h
void goToMyActivity();

// ActivityManager.cpp
void ActivityManager::goToMyActivity() {
    replaceActivity(std::make_unique<MyActivity>(renderer, mappedInput));
}
```

## 6. Stack allocation budget

If your activity uses FreeRTOS tasks, set the stack size based on what the task does:

| Task type | Stack size |
|-----------|-----------|
| Simple rendering or state work | 2048 bytes |
| Network, EPUB parsing, file I/O | 4096 bytes |

Declare the task handle as a member and delete the task in `onExit()` before any objects the task references are destroyed:

```cpp
// header
TaskHandle_t workerTask_ = nullptr;

// onExit
if (workerTask_) {
    vTaskDelete(workerTask_);
    workerTask_ = nullptr;
}
```

## 7. Test in the simulator

Before flashing to hardware, build and run in the simulator:

```sh
pio run -e simulator
```

See [Testing & Debugging](./testing-debugging.md) for simulator setup.

The simulator does not support image decoding (PNG/JPEG stubs always fail) and `esp_deep_sleep_start()` is a no-op. All other drawing and input works.

## 8. Pre-PR checklist

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

Verify:
- All new UI strings use `tr(STR_*)` and are present in every translation YAML
- `onExit()` frees every resource allocated in `onEnter()`
- FreeRTOS tasks are deleted in `onExit()`
- File handles are closed in `onExit()`
- No SD writes inside `loop()` unless batched/debounced
