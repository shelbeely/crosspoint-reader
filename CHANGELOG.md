# Changelog

## [Unreleased]

### Added

#### PDA tile — personal digital assistant apps
- Replace the **GAMES** tile with a **PDA** tile in the 8-node apps radar grid
- **CalendarActivity** (`src/activities/apps/CalendarActivity.h/.cpp`): monthly grid calendar with event storage (`/.crosspoint/calendar.json`), day-detail view, and add-event flow (title + note via keyboard); max 64 events per file
- **VCard library** (`lib/VCard/VCard.h/.cpp`): pure C++ streaming vCard 3.0 parser (host-testable); extracts FN, TEL, EMAIL, ORG, NOTE; builds index cache at `/.crosspoint/contacts.bin` (magic VCFX v1)
- **ContactsActivity** (`src/activities/contacts/ContactsActivity.h/.cpp`): contact list + detail view backed by `/contacts.vcf`; index loaded in `onEnter`, detail fetched on demand; hold-Confirm search; also accessible from HomeActivity main menu
- **NotesActivity** (`src/activities/apps/NotesActivity.h/.cpp`): plaintext note files under `/.crosspoint/notes/`; file list + paginated view; hold-Confirm to create or append lines
- **TodoActivity** (`src/activities/apps/TodoActivity.h/.cpp`): task checklist from `/.crosspoint/todo.json`; toggle done/undone; hold-Confirm to add or delete tasks; writes to SD only on exit if dirty
- **ExpenseTrackerActivity** (`src/activities/apps/ExpenseTrackerActivity.h/.cpp`): expense log (`/.crosspoint/expenses.json`); summary view with per-category ASCII bar chart; list view; numeric add flow with 5 built-in categories; CSV export to `/expenses_export.csv`
- **WorldClockActivity** (`src/activities/apps/WorldClockActivity.h/.cpp`): multi-timezone NTP clock in **TOOLS** tile; up to 6 zones stored in `/.crosspoint/world_clocks.json`; refreshes every second
- Add `CONTACT = 9` sleep screen mode to `CrossPointSettings::SLEEP_SCREEN_MODE`; `SleepActivity::renderContactSleepScreen()` picks a random contact name + phone from the index to display
- Add **Contacts** entry to `HomeActivity` main menu for quick access without the apps grid

### Removed

- **GAMES tile** and all game activity source files: `SnakeActivity`, `MinesweeperActivity`, `SudokuActivity`, `DiceRollerActivity`, `GameOfLifeActivity` — replaced by the PDA tile
- Associated game i18n strings (`STR_SNAKE`, `STR_MINESWEEPER`, `STR_SUDOKU`, `STR_DICE_ROLLER`, `STR_GAME_OF_LIFE`, `STR_GAMES`) removed from all 23 translation YAML files

#### Apps framework (biscuit fork integration)
- Integrate biscuit fork app framework: 8-tile grid/radar apps menu (COMMS, TOOLS, CRYPTO, GAMES, READER, FILES, SYSTEM, SETTINGS) with curated app set
- **COMMS**: `MeshChatActivity` (ESP-NOW peer-to-peer chat), `KarmaAttackActivity` (karma rogue-AP), `MacRandomizerActivity` (WiFi MAC spoofer)
- **TOOLS**: `ClockActivity` (NTP clock, stopwatch, pomodoro timer), `CalculatorActivity` (four-function calculator), `MorseCodeActivity` (encode/decode), `UnitConverterActivity` (temperature, length, weight, volume)
- **CRYPTO**: `CipherActivity` (ROT13, Caesar, Vigenère, XOR), `OtpGeneratorActivity` (one-time pad tokens), `QrGeneratorActivity` (text-to-QR)
- **GAMES**: `SnakeActivity`, `MinesweeperActivity`, `SudokuActivity`, `DiceRollerActivity` (animated multi-die roller), `GameOfLifeActivity` (Conway's Game of Life)
- **READER**: Recent Books, file-browser book picker, OPDS catalog browser
- **FILES**: SD card file browser
- **SYSTEM**: `DeviceInfoActivity` (firmware version, heap, uptime), `TaskManagerActivity` (FreeRTOS heap + task memory view), `BackgroundManagerActivity` (background-task status list), `ReadingStatsActivity` (all-time reading session summary)
- **SETTINGS**: Settings and transfer/OTA

#### Wireless and recon utilities
- Add `ESPNOW` radio state to `RadioManager` with `ensureEspNow()` and `deinitEspNow()` so ESP-NOW lifecycle is managed centrally alongside WiFi and BLE
- Add `MacManager` utility (`src/util/MacManager.h/.cpp`) for reading, randomizing (locally-administered bit), and restoring the WiFi station MAC address via `esp_wifi_set_mac`
- Add `MacRandomizerActivity`: randomize or restore the factory MAC with a single button press; shows current full MAC on screen
- Add `KarmaAttackActivity`: WiFi probe-request sniffer (promiscuous `IRAM_ATTR` callback) that cycles into an open rogue AP with the most-probed SSID; live probe table with hit counts; 5 s sniff / 10 s AP cycle
- Add `TargetDB` (`src/util/TargetDB.h/.cpp`): in-memory target registry tracking APs, stations, and BLE devices with SSID, RSSI, channel, auth type, OS fingerprint, PMKID/EAPOL flags, and client associations; capped at 128 entries
- Add `FrameParser` (`src/util/FrameParser.h/.cpp`): 802.11 frame classifier and field extractor (beacon, probe-request/response, auth, EAPOL) that populates `TargetDB` from captured packets
- Add `PacketRingBuffer` (`src/util/PacketRingBuffer.h/.cpp`): 16-slot ISR-safe ring buffer for WiFi promiscuous captures; allows the WiFi driver task to enqueue frames without blocking

#### Security utilities
- Add `PasswordStore` (`src/stores/PasswordStore.h/.cpp`): encrypted on-device password vault (up to 50 entries) backed by an NVS-encrypted SD-card JSON file
- Add `DuressManager` (`src/util/DuressManager.h`): singleton that redirects `/biscuit/` SD paths to `/biscuit_safe/` when duress mode is active, allowing a decoy dataset to be served under coercion

#### UI themes
- Add **Noir** theme (`src/components/themes/noir/`): dark, high-contrast military-aesthetic UI theme
- Add **Radar** theme (`src/components/themes/radar/`): tactical radar-style home screen with animated RadarHomeRenderer; reuses Noir metrics for all secondary screens

#### Status bar and display
- Display last two MAC octets (e.g. `a3:1f`) in the `AppsMenuActivity` status bar so the current WiFi identity is always visible; refreshed every 30 s with system info
- Add real EPUB `<hr>` rendering so horizontal rules now display as visible separators instead of being ignored

#### Developer / CI
- Add `copilot-setup-steps.yml` workflow: pre-installs PlatformIO and clang-format-21 in the Copilot cloud agent environment so CI does not need to re-download the toolchain on every run
- Add host-native test suite under `test/` with mock headers for all HAL and UI dependencies; includes `test_dice_roller`, `test_morse_code`, `test_unit_converter`, and `test_preview` suites
- Add `tools/rotate_logo.py` helper script for rotating the splash-screen BMP logo

### Changed
- Streamline biscuit app roster to 20 focused apps; remove ~75 specialist network/offense/security tools that are out of scope for an e-reader device
- `MeshChatActivity` now delegates ESP-NOW init/deinit to `RadioManager.ensureEspNow()` / `RadioManager.shutdown()` for consistent radio coexistence
- Last-used category paths moved from `/biscuit/` to `/.crosspoint/` to match the firmware's canonical data directory
- Status bar branding updated from "biscuit." to "crosspoint."
- NVS preferences namespace updated from `"biscuit"` to `"crosspoint"` in `RadioManager`
- COMMS tile RADAR node count updated from 1 to 3 to reflect the three COMMS apps
- Logo (`src/images/Logo120.h`) updated to crosspoint splash-screen bitmap

### Fixed
- Render missing Unicode block redactions, black-square ornaments, Greek category letters, and turned-comma punctuation in reader fonts
- Serialize SD-card and display access on the shared SPI bus to prevent task-ownership crashes during state saves, sleep transitions, and other concurrent render/storage activity
- Guard SPI bus lock acquisition so a failed recursive mutex take no longer marks the lock as held and triggers a mismatched release
- Harden EPUB section-cache writes and promotion so truncated SD writes fail fast, temp caches are synced before rename, and invalid page-cache files are less likely to persist across reloads
- Reject invalid serialized string lengths before allocation so corrupted cache data cannot trigger oversized string resizes during reads
- Clear cached EPUB metadata for books inside deleted folders so stale `/.crosspoint/epub_*` directories are not left behind
- Repair brace mismatch and clang-format violations in `TextBlock.cpp` introduced during the CrossInk fork merge

## [v1.2.9.1] - 2026-05-03

### Changed
- Cleaned up EPUB table rendering by removing synthetic row/cell labels and defaulting table cells to readable left alignment
- Allow simple EPUB tables with full-width note rows so a single `colspan` cell spanning the whole table no longer forces the entire table back to paragraph fallback

### Fixed
- Fix power-button shortcut conflicts outside the reader so reader-only actions fall back to `Confirm` while Sleep, Refresh, Screenshot, Sync Progress, and File Transfer remain real power actions. Those that had short-press power button to act as sleep saw unstable behavior previously. This should be fixed now
- Fix a potential crash when using `Go to %` in EPUBs
- Fix a potential crash when entering sleep with Page Overlay enabled if the cached EPUB page data is invalid
