# CrossPoint Reader — Durable Context

Keep this file focused on repo-specific gotchas that are worth reusing in future sessions.

## Simulator

- Simulator patches belong in the adjacent `crosspoint-simulator` repo.
- The valid local simulator env in this repo is `simulator`, and `pio run -e simulator` currently builds cleanly.
- The simulator `PNGdec` stub in `crosspoint-simulator/src/PNGdec.h` needs to mirror the real API shape used by app code, including `hasAlpha()` and `getTransparentColor()`, even though decode still fails intentionally.
- Known simulator limits:
  - No image rendering: `platformio.ini` ignores `hal`, `PNGdec`, and `JPEGDEC`, so image decoders are intentionally absent.
  - JPEGDEC stub always fails; `JPEGDEC fallback: open failed (err=-1)` is expected in simulator.
  - `esp_deep_sleep_start()` is a no-op in simulator.
  - `HalStorage` uses POSIX file access under `./fs_` and allows multiple readers, unlike real hardware.

## Real Hardware / Storage

- SdFat on hardware allows only one open reader per file path at a time. If a fallback needs to reopen the same file, close the first handle before reopening.

## Rendering / Reader Pipeline

- `lib/Epub/Epub/Page.cpp`: images must render only in `GfxRenderer::BW`; grayscale passes are text anti-aliasing passes only.
- Kindle EPUBs may contain paired high-res and old-Kindle fallback images. `ChapterHtmlSlimParser` should skip `<img>` nodes with `data-AmznRemoved-M8` to avoid duplicate stacked images.
- After image/layout pipeline changes that affect cached EPUB output, clear the affected `.crosspoint/epub_<hash>/` cache if behavior looks stale.

## Misc Repo Gotchas

- POSIX TZ signs are inverted from ISO 8601 in `TimeStore::applyTimezone()`: `"UTC-1"` means UTC+1.
- `LyraTheme::drawHeader()` does not call `BaseTheme::drawHeader()`, so header changes in the base theme must be duplicated in Lyra if needed.

## Documentation Added (2026-05-12)

The following documentation was added as part of a codebase deep-dive:

- `README.md` — added "Repository map" directory table, "Key concepts" quick-reference, and a full docs link table
- `docs/contributing/architecture.md` — added HAL layer, rendering pipeline, font system, input model, radio arbitration, and I18n sections
- `docs/contributing/libraries.md` — **new**: reference card for every library under `lib/`
- `docs/contributing/activities.md` — **new**: activity lifecycle, memory rules, render concurrency, result passing, directory inventory, navigation helpers
- `docs/contributing/adding-an-activity.md` — **new**: step-by-step guide for creating a new screen
- `docs/contributing/hal.md` — **new**: HAL layer details, each `Hal*` class, display pipeline, simulator compatibility
- `docs/contributing/settings-and-state.md` — **new**: SETTINGS / APP\_STATE singletons, all stores, debounce write rule
- `docs/contributing/build-system.md` — **new**: PlatformIO environments, pre-build scripts, partition table, compile flags
- `docs/contributing/testing-debugging.md` — expanded: simulator setup, native tests, host tests, common crash causes
- `docs/file-formats.md` — added Markdown cache (`MDKI` v1), contacts.bin (`VCFX` v1), github.json, watched_repos.json
- Inline doc-comments added to: `Activity.h`, `ActivityManager.h`, `CrossPointSettings.h`, `CrossPointState.h`, `RadioManager.h`, `GfxRenderer.h`, `HalDisplay.h`, `HalGPIO.h`

Key conventions confirmed during the documentation audit (all still current):

| Convention | Rule |
|------------|------|
| HAL usage | Use `lib/hal/Hal*.h` wrappers; never call open-x4-sdk SDK classes directly |
| I18n | All user-visible strings use `tr(STR_*)` — hardcoded UI strings are a bug |
| File I/O | Use `HalFile` / `FsFile` (thread-safe alias); always close handles in `onExit()` |
| Button input | Use `MappedInputManager::Button::*` enums; never raw HalGPIO button indices |
| Logging | `LOG_INF`, `LOG_DBG`, `LOG_ERR` — may use hardcoded English; UI text must use `tr()` |
| Debounced writes | Do not call `saveToFile()` on every page turn; only on user action or pre-sleep |
| Stack size | Keep locals < 256 bytes; tasks: 2048 (simple) / 4096 (network/EPUB) |
| PAT security | GitHub PAT must never appear in any `LOG_*` output |

## Apps Framework (biscuit integration)

- `src/activities/apps/` contains the biscuit-derived apps framework: `AppsMenuActivity` (8-tile grid + RADAR theme), `AppCategoryActivity` (generic submenu), `BackgroundManagerActivity`, `TaskManagerActivity`, and 16 curated apps (MeshChat, Calculator, Cipher, Clock, DeviceInfo, DiceRoller, GameOfLife, Minesweeper, MorseCode, OtpGenerator, QrGenerator, ReadingStats, SdFileBrowser, Snake, Sudoku, UnitConverter).
- The RADAR theme requires exactly 8 nodes (`kRadarNodes[8]`, `NODE_COUNT=8`). Do not change `ITEM_COUNT` without updating `RadarHomeRenderer`.
- Last-used category files live in `/.crosspoint/lastused_N.txt` (not `/biscuit/`).

## ESP-NOW / RadioManager

- `RadioManager` has three states: `WIFI`, `BLE`, `ESPNOW`. ESP-NOW = WiFi STA without IP stack + `esp_now_init()`. `ensureEspNow()` handles setup; `shutdown()` tears down all states including ESPNOW.
- `MeshChatActivity` uses `RADIO.ensureEspNow()` for init; only calls `RADIO.shutdown()` on exit (does not call esp_now_deinit directly).
- NVS namespace for RadioManager / disclaimer is `"crosspoint"` (changed from `"biscuit"`).
