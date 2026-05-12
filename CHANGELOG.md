# Changelog

## [Unreleased]

### Added
- Add real EPUB `<hr>` rendering so horizontal rules now display as visible separators instead of being ignored
- Add `ESPNOW` radio state to `RadioManager` with `ensureEspNow()` and `deinitEspNow()` so ESP-NOW lifecycle is managed centrally alongside WiFi and BLE
- Integrate biscuit fork app framework: 8-tile grid/radar apps menu (COMMS, TOOLS, CRYPTO, GAMES, READER, FILES, SYSTEM, SETTINGS) with curated app set
- Add `MacManager` utility (`src/util/MacManager.h/.cpp`) for reading, randomizing, and restoring the WiFi station MAC address
- Add `MacRandomizerActivity`: randomize or restore the factory MAC with a single button press; shows current full MAC on screen
- Add `KarmaAttackActivity`: passive WiFi probe-request sniffer that cycles into an open AP bearing the most-probed SSID; displays live probe table with hit counts
- Display last two MAC octets (e.g. `a3:1f`) in the `AppsMenuActivity` status bar so the current identity is always visible

### Changed
- Streamline biscuit app roster to 20 focused apps; remove ~75 specialist network/offense/security tools that are out of scope for an e-reader device
- `MeshChatActivity` now delegates ESP-NOW init/deinit to `RadioManager.ensureEspNow()` / `RadioManager.shutdown()` for consistent radio coexistence
- Last-used category paths moved from `/biscuit/` to `/.crosspoint/` to match the firmware's canonical data directory
- Status bar branding updated from "biscuit." to "crosspoint."
- NVS preferences namespace updated from `"biscuit"` to `"crosspoint"` in `RadioManager`

### Fixed
- Render missing Unicode block redactions, black-square ornaments, Greek category letters, and turned-comma punctuation in reader fonts
- Serialize SD-card and display access on the shared SPI bus to prevent task-ownership crashes during state saves, sleep transitions, and other concurrent render/storage activity
- Guard SPI bus lock acquisition so a failed recursive mutex take no longer marks the lock as held and triggers a mismatched release
- Harden EPUB section-cache writes and promotion so truncated SD writes fail fast, temp caches are synced before rename, and invalid page-cache files are less likely to persist across reloads
- Reject invalid serialized string lengths before allocation so corrupted cache data cannot trigger oversized string resizes during reads
- Clear cached EPUB metadata for books inside deleted folders so stale `/.crosspoint/epub_*` directories are not left behind

## [v1.2.9.1] - 2026-05-03

### Changed
- Cleaned up EPUB table rendering by removing synthetic row/cell labels and defaulting table cells to readable left alignment
- Allow simple EPUB tables with full-width note rows so a single `colspan` cell spanning the whole table no longer forces the entire table back to paragraph fallback

### Fixed
- Fix power-button shortcut conflicts outside the reader so reader-only actions fall back to `Confirm` while Sleep, Refresh, Screenshot, Sync Progress, and File Transfer remain real power actions. Those that had short-press power button to act as sleep saw unstable behavior previously. This should be fixed now
- Fix a potential crash when using `Go to %` in EPUBs
- Fix a potential crash when entering sleep with Page Overlay enabled if the cached EPUB page data is invalid
