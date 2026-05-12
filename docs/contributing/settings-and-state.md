---
title: Settings & state
parent: Contributing
nav_order: 8
---

# Settings and state

Two singletons carry all persistent data for the application. Understanding them is essential before adding any feature that needs to remember something across reboots.

## `SETTINGS` — user preferences

**Class:** `CrossPointSettings` (`src/CrossPointSettings.h`)  
**Macro:** `SETTINGS` → `CrossPointSettings::getInstance()`  
**Persisted to:** `/.crosspoint/settings.bin`

`SETTINGS` holds every user-configurable preference. It is loaded from SD on boot and saved when the user changes a setting. Changes are written via `SETTINGS.saveToFile()`.

### What it stores (selected groups)

| Group | Fields | Notes |
|-------|--------|-------|
| **Display** | `orientation`, `refreshFrequency`, `sleepTimeout`, `enableFadingFix`, `invertDisplay` | E-ink refresh and orientation settings |
| **Reader fonts** | `fontFamily`, `fontSize`, `sdFontFamilyName`, `lineCompression`, `paragraphAlignment`, `extraParagraphSpacing`, `forceParagraphIndents` | Font and layout settings that bust the section cache when changed |
| **Reader features** | `hyphenationEnabled`, `embeddedStyle`, `imageRendering`, `bionicReadingEnabled`, `guideReadingEnabled`, `focusReadingEnabled` | Typography and accessibility features |
| **Sleep screen** | `sleepScreenMode`, `sleepScreenCoverMode`, `sleepScreenCoverFilter`, `customSleepImagePath` | What to show when the device sleeps |
| **Status bar** | `statusBarMode`, `statusBarProgressBar`, `statusBarTitle`, `statusBarThickness` | Status bar visibility and content |
| **Buttons** | `frontButtonLayout`, `sideButtonLayout`, `sideLongPress`, `shortPwrBtn`, buttonRemap arrays | Button layout and long-press actions |
| **Network** | Referenced via `WifiCredentialStore` and `OpdsServerStore` (separate store classes) | Stored in dedicated JSON files, not `settings.bin` |
| **Theme** | `uiTheme` | Classic / Lyra / Military |
| **KOReader sync** | Referenced via `KOReaderCredentialStore` | Stored separately |
| **GitHub companion** | Referenced via `GitHubCredentialStore` | Stored separately |

### Serial number / format

`settings.bin` is a custom binary format. If you add new fields, append them at the end and bump the format version. See `src/CrossPointSettings.cpp` for the serialize/deserialize methods.

---

## `APP_STATE` — runtime session state

**Class:** `CrossPointState` (`src/CrossPointState.h`)  
**Macro:** `APP_STATE` → `CrossPointState::getInstance()`  
**Persisted to:** `/.crosspoint/state.bin`

`APP_STATE` holds transient session data: what book was open, where the user was, and context for sleep/wake transitions. It is saved before sleep and loaded on boot to resume where the user left off.

### What it stores

| Field | Purpose |
|-------|---------|
| `openEpubPath` | Path of the book that was open when the device slept |
| `favoriteSleepImagePath` | User-pinned sleep screen wallpaper |
| `recentSleepImages[16]` | Circular buffer of recently shown random wallpaper indices (avoids repeats) |
| `readerActivityLoadCount` | Number of times the reader was entered (used for refresh-frequency logic) |
| `lastSleepFromReader` | Whether the device slept while reading (drives resume behavior) |
| `pendingBookmarkSpine` / `pendingBookmarkProgress` | Bookmark pending confirmation from a background task |
| `hasPendingAlert` / `pendingAlertTitle` / `pendingAlertBody` | Alert queued by a background task, consumed by `ActivityManager` |

---

## `JsonSettingsIO`

`src/JsonSettingsIO.h` is a generic helper for JSON-backed settings files. It is used by `WifiCredentialStore`, `OpdsServerStore`, and similar stores that persist their data as human-readable JSON rather than binary.

---

## Specific data stores

| Store class | File | Stores |
|-------------|------|--------|
| `RecentBooksStore` | `src/RecentBooksStore.h` | List of recently opened book paths (persisted in `.crosspoint/`) |
| `BookmarkStore` | `src/BookmarkStore.h` | Per-book bookmark positions (in the EPUB cache directory) |
| `WifiCredentialStore` | `src/WifiCredentialStore.h` | Saved WiFi SSID/password pairs (JSON on SD) |
| `OpdsServerStore` | `src/OpdsServerStore.h` | OPDS catalog server URLs (JSON on SD) |
| `WatchedReposStore` | `src/WatchedReposStore.h` | GitHub repos watchlist (up to 16; `/.crosspoint/watched_repos.json`) |
| `GitHubCredentialStore` | `src/GitHubCredentialStore.h` | GitHub PAT — MAC-XOR obfuscated; **never log the token**; `/.crosspoint/github.json` |
| `KOReaderCredentialStore` | `src/KOReaderCredentialStore.h` | KOReader sync server URL and credentials |

---

## The debounce write rule

> **Do not write to SD on every page turn, every loop iteration, or every button press.**

SD writes are slow (~10–50 ms) and can cause noticeable stutter. They also wear the SD card over time. The correct pattern:

1. Update the in-memory struct immediately
2. Write to SD only when:
   - The user explicitly saves (e.g., taps "Save" in settings)
   - The device is about to sleep (`saveToFile()` in the pre-sleep path)
   - After a meaningful milestone (e.g., finishing a chapter, not every page)

The `SETTINGS.saveToFile()` and `APP_STATE.saveToFile()` calls in the sleep path handle this for the main singletons.

---

## Adding a new persistent setting

1. Add a field to `CrossPointSettings` (or the appropriate store class)
2. Add serialize / deserialize handling in `CrossPointSettings.cpp` — always append at the end
3. Bump the format version constant
4. Add a default value that preserves existing behavior (so old `settings.bin` files still work)
5. Add the UI control in `SettingsActivity` or the relevant sub-settings screen
6. Use `tr(STR_*)` for all labels

If you change a binary file format, document the change in `docs/file-formats.md` and add a version entry explaining what changed.
