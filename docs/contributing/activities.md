---
title: Activity system
parent: Contributing
nav_order: 3
---

# Activity system

The activity system is the core UI architecture of the firmware. If you are coming from Android development you will recognize the pattern immediately. If you are coming from React, think of an activity as a full-screen component that owns its own lifecycle, input handling, and rendering.

## What is an activity?

An `Activity` is a single screen. It holds a reference to `GfxRenderer` (drawing) and `MappedInputManager` (button input). The `ActivityManager` singleton owns the stack and calls lifecycle methods.

Only one activity is active at a time. There is no concept of background or paused activities.

## Lifecycle

```
ActivityManager::pushActivity(activity)
  └─ activity.onEnter()           ← allocate resources, start tasks
       └─ [loop() called each frame]
            └─ [render() called when requestUpdate() was triggered]
  └─ activity.finish()            ← triggered by the activity itself or popActivity()
       └─ activity.onExit()       ← free resources, stop tasks
            └─ activity deleted   ← heap-allocated, destroyed after onExit returns
```

### `onEnter()`

Called once when the activity becomes active. Allocate all long-lived buffers, open file handles, and start FreeRTOS tasks here.

```cpp
void MyActivity::onEnter() {
    Activity::onEnter();
    // allocate, open files, start tasks
    requestUpdate();  // trigger first render
}
```

### `loop()`

Called every main-loop iteration while the activity is active. Handle input, advance state, and call `requestUpdate()` to schedule a repaint.

Keep `loop()` fast. Do not block inside it — offload slow work to a FreeRTOS task.

### `render(RenderLock&&)`

Called from the render task (a separate FreeRTOS task) when `requestUpdate()` was triggered. Write all draw calls here using `renderer`.

`render()` runs on the render task thread, not the main loop thread. Do not mutate state shared with `loop()` without synchronization.

### `onExit()`

Called once when the activity is about to be destroyed. Free all resources **in reverse order** of allocation:

- Delete FreeRTOS tasks before destroying any objects they reference
- Close all open `FsFile` / `HalFile` handles
- Free heap allocations
- Stop any timers or callbacks

```cpp
void MyActivity::onExit() {
    if (task_) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
    if (file_.isOpen()) file_.close();
    Activity::onExit();
}
```

## Memory rules

These rules exist because the ESP32-C3 has ~380 KB usable SRAM and no PSRAM. Mistakes cause heap fragmentation and crashes.

1. **Allocate in `onEnter()`, free in `onExit()`** — do not allocate large objects on the stack or in constructors
2. **Stack limit** — keep local stack frames under 256 bytes; larger buffers go on the heap
3. **Avoid `std::string` / Arduino `String` in hot paths** — prefer `char[]` and `snprintf`
4. **Reserve `std::vector` capacity before push loops** — `vec.reserve(N)` before a loop that calls `push_back()`
5. **No heap churn in `loop()` or `render()`** — allocate once, reuse
6. **Debounce SD writes** — do not write progress or state on every page turn; batch writes

## Passing results between activities

Use `startActivityForResult()` when you need a sub-activity to return a value:

```cpp
// Parent activity
startActivityForResult(
    std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
    [this](ActivityResult result) {
        if (result.ok) handleNetworkSelected(result.value);
        requestUpdate();
    }
);
```

Inside the child activity, call `setResult()` before `finish()`:

```cpp
setResult(ActivityResult{.ok = true, .value = selectedSsid});
finish();
```

`requestUpdate()` is called automatically after the result handler completes.

## Render concurrency

The render pipeline runs on a dedicated FreeRTOS task:

```
Main loop task              Render task
─────────────────           ────────────────────
loop()                      [blocked on semaphore]
requestUpdate()  ──────────►  acquire RenderLock
                              activity.render(lock)
                              displayBuffer()
                              release RenderLock
                             [blocked again]
```

**`requestUpdate(bool immediate)`**  
Schedules a render. If `immediate = false` (default) the render happens at the end of the current loop iteration. If `immediate = true` it is triggered right away.

**`requestUpdateAndWait()`**  
Triggers a render and blocks until it completes. Returns `Rejected` if called from the render task or while a `RenderLock` is held. Safe to call from `onEnter()` for the initial draw.

**`RenderLock`**  
RAII mutex wrapper. While held, no render can start. Pass it by move into `render(RenderLock&&)`.

## Hook methods for the main loop

Override these to change activity behavior:

| Method | Default | When to override |
|--------|---------|-----------------|
| `skipLoopDelay()` | `false` | Return `true` when real-time responsiveness is required (e.g., web server) |
| `preventAutoSleep()` | `false` | Return `true` to keep the device awake (e.g., active download) |
| `isReaderActivity()` | `false` | Return `true` in reader activities to enable reader-specific input mapping |
| `canSnapshotForSleepOverlay()` | `false` | Return `true` to allow the current frame to be used as the sleep-screen background |

## Activity directory inventory

### `src/activities/home/`

Home screen, library navigation, bookmarks, file browser, alert dialogs, and the crash screen.

- `HomeActivity` — main dashboard entry point
- `FileBrowserActivity` + `FileBrowserActionActivity` — browse SD card, open files
- `BookmarksHomeActivity` — bookmark list
- `RecentBooksActivity` — continue reading from recently opened books
- `AlertActivity` — modal error/info dialog
- `CrashActivity` — crash report screen shown after a panic reboot

### `src/activities/reader/`

All reading flows. `ReaderActivity` is the dispatcher that chooses the right reader based on file extension.

- `ReaderActivity` — format detection and dispatch
- `EpubReaderActivity` — main EPUB reading screen
- `EpubReaderMenuActivity`, `EpubReaderChapterSelectionActivity`, `EpubReaderBookmarkListActivity`, `EpubReaderFootnotesActivity`, `EpubReaderPercentSelectionActivity` — in-reader navigation menus
- `TxtReaderActivity` — plain text reader
- `XtcReaderActivity` + `XtcReaderChapterSelectionActivity` — XTC format reader
- `KOReaderSyncActivity` — KOReader progress sync UI
- `QrDisplayActivity` — QR code display (for sharing links from books)
- `BookStatsActivity` / `BookStatsView` — per-book reading statistics
- `ReaderOptionsActivity` — in-reader settings overlay

### `src/activities/settings/`

Settings menus and configuration screens.

- `SettingsActivity` — top-level settings menu
- `ButtonRemapActivity` — reassign front buttons
- `FontSelectionActivity` + `FontDownloadActivity` — font picker and SD font download
- `LanguageSelectActivity` — UI language
- `KOReaderSettingsActivity` + `KOReaderAuthActivity` — KOReader sync configuration
- `OtaUpdateActivity` + `SdFirmwareUpdateActivity` — firmware update
- `OpdsServerListActivity` + `OpdsSettingsActivity` — OPDS catalog management
- `StatusBarSettingsActivity` — status bar customization
- `ClearCacheActivity` — wipe SD cache

### `src/activities/network/`

WiFi and file transfer screens.

- `NetworkModeSelectionActivity` — choose STA / AP / Calibre mode
- `WifiSelectionActivity` — scan and join a WiFi network
- `CrossPointWebServerActivity` — file transfer web server (HTTP + WebSocket)
- `CalibreConnectActivity` — Calibre wireless library connection

### `src/activities/apps/`

The biscuit apps framework: the 8-tile home grid and all curated apps.

- `AppsMenuActivity` — 8-tile dashboard (Recon, Offense, Defense, Comms, Tools, Games, Reader, Settings) with optional RADAR theme
- `AppCategoryActivity` — generic sub-menu for app categories
- `BackgroundManagerActivity` — radio state, SD status, active timers
- `TaskManagerActivity` — heap, uptime, activity stack
- Individual app activities: `MeshChatActivity`, `CalculatorActivity`, `CipherActivity`, `ClockActivity`, `DeviceInfoActivity`, `DiceRollerActivity`, `GameOfLifeActivity`, `KarmaAttackActivity`, `MacRandomizerActivity`, `MinesweeperActivity`, `MorseCodeActivity`, `OtpGeneratorActivity`, `QrGeneratorActivity`, `ReadingStatsActivity`, `SdFileBrowserActivity`, `SnakeActivity`, `SudokuActivity`, `UnitConverterActivity`

### `src/activities/boot_sleep/`

Boot and sleep transition screens.

- `BootActivity` — first screen after power-on, shows version and initializes state
- `SleepActivity` — sleep overlay; can render a custom image, book cover, reading stats, contact card, or status summary
- `FullScreenMessageActivity` — generic full-screen text (used for error messages during startup)

## Navigation helpers

`ActivityManager` exposes two families of navigation:

**Replace navigation** — destroys the entire current stack and starts fresh. Use for top-level screen transitions:

```cpp
activityManager.goHome();
activityManager.goToReader(path);
activityManager.goToSettings();
activityManager.goToFileTransfer();
// ... and others
```

**Stack navigation** — preserves the caller on the stack so the user can navigate back:

```cpp
activityManager.pushActivity(std::make_unique<WifiSelectionActivity>(...));
// ...later, child calls:
activityManager.popActivity();  // returns to caller
```

If `popActivity()` is called on the last activity in the stack, `goHome()` is called automatically.
