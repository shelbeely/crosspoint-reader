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

## Apps Framework (biscuit integration)

- `src/activities/apps/` contains the biscuit-derived apps framework: `AppsMenuActivity` (8-tile grid + RADAR theme), `AppCategoryActivity` (generic submenu), `BackgroundManagerActivity`, `TaskManagerActivity`, and 16 curated apps (MeshChat, Calculator, Cipher, Clock, DeviceInfo, DiceRoller, GameOfLife, Minesweeper, MorseCode, OtpGenerator, QrGenerator, ReadingStats, SdFileBrowser, Snake, Sudoku, UnitConverter).
- The RADAR theme requires exactly 8 nodes (`kRadarNodes[8]`, `NODE_COUNT=8`). Do not change `ITEM_COUNT` without updating `RadarHomeRenderer`.
- Last-used category files live in `/.crosspoint/lastused_N.txt` (not `/biscuit/`).

## ESP-NOW / RadioManager

- `RadioManager` has three states: `WIFI`, `BLE`, `ESPNOW`. ESP-NOW = WiFi STA without IP stack + `esp_now_init()`. `ensureEspNow()` handles setup; `shutdown()` tears down all states including ESPNOW.
- `MeshChatActivity` uses `RADIO.ensureEspNow()` for init; only calls `RADIO.shutdown()` on exit (does not call esp_now_deinit directly).
- NVS namespace for RadioManager / disclaimer is `"crosspoint"` (changed from `"biscuit"`).
