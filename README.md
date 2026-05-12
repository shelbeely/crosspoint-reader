# crosspoint-reader.

Custom firmware for the **Xteink X4** e-paper device. Full-featured e-reader with wireless tools, games, and utilities built on top of a solid EPUB/TXT reading core.

![Dashboard](./docs/images/homescreen.jpeg)

## What is this

CrossPoint Reader turns the Xteink X4 into both a great e-reader and a useful handheld device. The home screen focuses on books; a tile-based apps menu adds communication, productivity, games, and system tools alongside. Live system info (battery, heap, uptime, WiFi status) is always visible.

The 4.26" e-ink display is readable in direct sunlight, retains its image without power, and gives the device days of battery life. Seven physical buttons provide navigation without a touchscreen. WiFi and BLE 5.0 enable wireless tools. A MicroSD card stores everything.

## Hardware

| Spec | Value |
|------|-------|
| SoC | ESP32-C3 (RISC-V, 160MHz) |
| RAM | 380KB SRAM (no PSRAM) |
| Flash | 16MB |
| Display | 4.26" 800×480 e-ink, 1-bit mono |
| Input | 7 buttons (4 front, 3 side) |
| WiFi | 2.4GHz 802.11 b/g/n |
| BLE | 5.0 (shared radio with WiFi) |
| Storage | MicroSD (FAT32) |
| Port | USB-C (serial + power) |

## Apps

The apps menu is a grid of eight tiles. Each opens a category with its own app list.

| Tile | Apps | Purpose |
|------|------|---------|
| **COMMS** | 3 | Wireless communication and MAC tools |
| **TOOLS** | 4 | Productivity utilities |
| **CRYPTO** | 3 | Cipher and one-time code tools |
| **GAMES** | 5 | Offline games |
| **READER** | 4 | Books, OPDS, reading progress |
| **FILES** | 1 | SD card file browser |
| **SYSTEM** | 3 | Device diagnostics |
| **SETTINGS** | 2 | Preferences and file transfer |

### COMMS

| App | What it does |
|-----|-------------|
| Mesh Chat | ESP-NOW peer-to-peer text chat, no WiFi router needed, ~200 m range |
| Karma Attack | Rogue AP that responds to all WiFi probe requests with a matching SSID |
| MAC Randomizer | Randomize or restore the WiFi station MAC address |

### TOOLS

| App | What it does |
|-----|-------------|
| Clock | NTP clock, stopwatch, pomodoro timer |
| Calculator | Four-function calculator |
| Morse Code | Encode and decode morse |
| Unit Converter | Temperature, length, weight, volume |

### CRYPTO

| App | What it does |
|-----|-------------|
| Cipher Tools | ROT13, Caesar, Vigenère, XOR |
| OTP Generator | One-time pad random tokens |
| QR Generator | Generate QR codes from text |

### GAMES

| App | What it does |
|-----|-------------|
| Snake | Classic snake |
| Minesweeper | Classic minesweeper |
| Sudoku | Number puzzle |
| Dice Roller | Animated multi-die roller |
| Game of Life | Conway's cellular automaton |

### READER

| App | What it does |
|-----|-------------|
| Open Book | Browse and open an ebook |
| Recent Books | Continue where you left off |
| OPDS Browser | Download books from OPDS servers |
| Reading Stats | Pages read, books completed, streaks |

Full EPUB 2/3 rendering, TXT and Markdown reading, KOReader Sync, and Calibre wireless transfer are all supported.

### FILES

| App | What it does |
|-----|-------------|
| File Browser | Browse and view files on the SD card |

### SYSTEM

| App | What it does |
|-----|-------------|
| Device Info | Chip, flash, RAM, firmware, WiFi, screen info |
| Task Manager | FreeRTOS heap and task memory view |
| Background | Background-task status list |

### SETTINGS

| App | What it does |
|-----|-------------|
| Settings | Display, reader, controls, system configuration |
| WiFi Transfer | Upload/download files via WiFi (STA, AP, or Calibre) |

## Themes

Seven UI themes, selectable in Settings:

- **Classic** — original CrossPoint style
- **Lyra** — rounded elements, modern feel (default)
- **Lyra 3 Covers** — Lyra with a three-cover home screen layout
- **RoundedRaff** — rounded variant
- **Military** — inverted headers, sharp corners, dashed separators, uppercase labels
- **Noir** — dark, high-contrast military-aesthetic theme
- **Radar** — tactical radar-style home screen with animated ring display

## SD card structure

```
/.crosspoint/
  settings.bin        # User preferences
  state.bin           # Runtime/session state
  epub_<hash>/        # EPUB section cache (one folder per book)
  md_<hash>/          # Markdown cache (one folder per file)
  lastused_N.txt      # Last-used app name per tile (0–7)
  progress.bin        # (inside epub cache) Reading progress
```

## Installing

### Web flasher (recommended)

1. Connect your Xteink X4 via USB-C data cable (not charge-only)
2. Wake the device by pressing Power
3. Go to https://xteink.dve.al/ and flash the firmware

To revert to stock firmware, use the same site or press "Swap boot partition" at https://xteink.dve.al/debug.

### Manual

```bash
git clone --recursive https://github.com/shelbeely/crosspoint-reader
cd crosspoint-reader
pio run --target upload
```

## Development

### Prerequisites

- PlatformIO Core or VS Code + PlatformIO IDE
- Python 3.8+
- USB-C data cable
- Xteink X4

### Building

```powershell
# Windows PowerShell
$env:PYTHONUTF8=1
pio run -j 16
```

```bash
# Linux / macOS
pio run -j 16
```

### Adding translations

Translations live in `lib/I18n/translations/`. Each language is a YAML file. Add or edit strings, then regenerate:

```bash
python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/
```

See [i18n docs](./docs/i18n.md) for details.

### Debugging

```bash
python3 -m pip install pyserial colorama matplotlib
python3 scripts/debugging_monitor.py
```

The debug monitor shows color-coded logs and a real-time memory graph.

### Architecture

The firmware uses an activity-based UI architecture. Every screen is an `Activity` subclass with `onEnter()`, `loop()`, `render()`, and `onExit()`. Activities are managed by `ActivityManager` (push/pop/replace). WiFi and BLE share one radio, arbitrated by `RadioManager`.

See [architecture docs](./docs/contributing/architecture.md) for the full overview.

## Repository map

| Directory | Purpose |
|-----------|---------|
| `src/` | Application entry point (`main.cpp`), settings/state singletons, all activity implementations, network server, UI components |
| `lib/` | Self-contained C++ libraries: EPUB engine, fonts, rendering, I18n, HAL abstractions, parsers, and more |
| `open-x4-sdk/` | Git submodule — hardware SDK for the Xteink X4 (display driver, input manager, SD card, battery) |
| `scripts/` | Build-time Python scripts (HTML embedding, i18n codegen, font conversion, debug monitor) |
| `docs/` | User guide and contributor documentation |
| `test/` | Host-runnable unit tests and test EPUBs (no hardware required) |
| `tools/` | Developer utilities (font subsetting, icon conversion) |
| `bin/` | Shell helper scripts (e.g. `clang-format-fix`) |
| `include/` | Global include-path headers shared across `src/` and `lib/` |

## Key concepts

| Concept | What it is |
|---------|-----------|
| **Activity** | A screen controller. Subclass of `Activity` with `onEnter()`, `loop()`, `render()`, `onExit()`. The Android Activity pattern, shrunk to an embedded device. |
| **ActivityManager** | Singleton that owns the activity stack. Push, pop, or replace activities; runs the render task. |
| **HAL** | Hardware Abstraction Layer — `lib/hal/Hal*.h` wrappers over the `open-x4-sdk` SDK classes. App code must use HAL, never SDK directly. |
| **GfxRenderer** | 2-D drawing API that writes into the e-ink framebuffer. Owned by `ActivityManager`; passed to every activity. |
| **SETTINGS** | `CrossPointSettings::getInstance()` macro. User preferences — font, layout, sleep mode, etc. Persisted to `/.crosspoint/settings.bin`. |
| **APP\_STATE** | `CrossPointState::getInstance()` macro. Runtime/session state — current book, sleep context. Persisted to `/.crosspoint/state.bin`. |
| **`tr(STR_*)`** | All user-visible UI strings must go through the `tr()` I18n lookup. Source strings live in `lib/I18n/translations/*.yaml`; the C++ header is generated by `scripts/gen_i18n.py`. Hardcoding UI strings is a bug. |

## Documentation

| Doc | What it covers |
|-----|---------------|
| [Architecture](./docs/contributing/architecture.md) | System overview, runtime lifecycle, reader pipeline, networking |
| [HAL layer](./docs/contributing/hal.md) | Hardware abstraction, each `Hal*` class, display pipeline |
| [Activity system](./docs/contributing/activities.md) | Lifecycle, memory rules, result passing, render concurrency |
| [Adding an activity](./docs/contributing/adding-an-activity.md) | Step-by-step guide for new screens |
| [Libraries](./docs/contributing/libraries.md) | Reference card for every library under `lib/` |
| [Settings & state](./docs/contributing/settings-and-state.md) | SETTINGS / APP\_STATE singletons, stores, persistence rules |
| [Build system](./docs/contributing/build-system.md) | PlatformIO environments, pre-build scripts, compile flags |
| [Testing & debugging](./docs/contributing/testing-debugging.md) | Simulator, native tests, serial logs, common crashes |
| [File formats](./docs/file-formats.md) | Binary cache format specs (book.bin, section.bin, contacts.bin, …) |
| [Webserver endpoints](./docs/webserver-endpoints.md) | HTTP API reference for file transfer mode |
| [i18n](./docs/i18n.md) | Translation workflow |
| [SD card fonts](./docs/sd-card-fonts.md) | Loading custom fonts from SD |
| [User Guide](./USER_GUIDE.md) | End-user instructions and troubleshooting |

## Credits

Built on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) by the CrossPoint contributors. CrossPoint was inspired by [diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader) by atomic14.

## License

MIT
