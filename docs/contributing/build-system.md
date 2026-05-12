---
title: Build system
parent: Contributing
nav_order: 9
---

# Build system

The project uses [PlatformIO](https://platformio.org/) as its build system. All environments are defined in `platformio.ini`. Python 3.8+ is required for the pre-build scripts.

## Environments

Run `pio run -e <env>` to build a specific environment. The default (`pio run`) builds `tiny`, `xlarge`, and `no_emoji`.

| Environment | When to use |
|-------------|-------------|
| `tiny` | Standard release build. Omits 18 px (xlarge) and 20 px (huge) font sizes. Default for most devices. |
| `xlarge` | Release build for users who need larger fonts. Omits 8/10/12 px sizes to fit. |
| `no_emoji` | Includes all font sizes but omits emoji glyphs. Smallest build that has every size. |
| `slim` | Smallest binary: serial logging disabled (`ENABLE_SERIAL_LOG` unset). For space-constrained situations. |
| `simulator` | macOS/Linux desktop simulator (SDL2). Use for UI development without hardware. |
| `native` | Host-only unit tests via PlatformIO Unity. No hardware, no display. |
| `debug` / `test` | CI-specific variants with debug logging and release-candidate versioning. Rarely used locally. |

## Building

```sh
# Build all default environments
pio run

# Build a specific environment
pio run -e tiny

# Build faster with parallelism
pio run -j 16 -e tiny
```

On Windows PowerShell, set UTF-8 mode first:

```powershell
$env:PYTHONUTF8=1
pio run -j 16
```

## Flashing

```sh
pio run -e tiny --target upload
```

## Pre-build scripts

PlatformIO runs these Python scripts automatically before each build. You should rarely need to invoke them manually, but knowing what they do helps when something goes wrong.

| Script | What it does |
|--------|-------------|
| `scripts/build_html.py` | Reads HTML files from `data/html/` and generates `src/network/html/*.generated.h` as C string literals embedded in the firmware. Do not edit `.generated.h` files directly. |
| `scripts/gen_i18n.py` | Reads YAML translation files from `lib/I18n/translations/` and generates C++ headers in `lib/I18n/`. Re-run manually after adding or changing translation keys: `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/`. |
| `scripts/git_branch.py` | Injects the current git branch / commit hash into `CROSSPOINT_VERSION` and `CROSSINK_VERSION` compile definitions. In CI, `CROSSPOINT_RC_HASH` env var overrides the git hash. |
| `scripts/patch_jpegdec.py` | Applies a compatibility patch to the JPEGDEC library after it is downloaded by PlatformIO. Required because the library is not ESP32-C3 friendly out of the box. |
| `scripts/patch_websockets.py` | Applies a compatibility patch to the WebSockets library. |
| `scripts/rename_firmware.py` | Post-build: renames the output `.bin` file to include the firmware variant and version. |

Other useful scripts (not pre-build):

| Script | Purpose |
|--------|---------|
| `scripts/debugging_monitor.py` | Enhanced serial monitor with color-coded log levels and a live heap/memory graph. |
| `scripts/generate_hyphenation_trie.py` | Regenerates hyphenation trie headers under `lib/Epub/Epub/hyphenation/generated/` from language data files. |
| `scripts/update_hyphenation.sh` | Shell wrapper to regenerate all hyphenation tries. |
| `scripts/generate_test_epub.py` | Generates test EPUB files used by the test suite. |

## Personal overrides — `platformio.local.ini`

Create `platformio.local.ini` in the repo root for machine-specific overrides (upload port, monitor port, personal flags). This file is `.gitignore`d and should never be committed.

```ini
; platformio.local.ini — not committed
[env:tiny]
upload_port = /dev/ttyUSB0
monitor_port = /dev/ttyUSB0
```

## Partition table

`partitions.csv` defines the flash layout for the 16 MB flash chip:

| Name | Type | Offset | Size | Purpose |
|------|------|--------|------|---------|
| `nvs` | data/nvs | 0x9000 | 20 KB | Non-volatile storage (RadioManager disclaimer, etc.) |
| `otadata` | data/ota | 0xE000 | 8 KB | OTA boot selection |
| `app0` | app/ota_0 | 0x10000 | 6.25 MB | Primary firmware slot |
| `app1` | app/ota_1 | 0x650000 | 6.25 MB | Secondary firmware slot (OTA) |
| `spiffs` | data/spiffs | 0xC90000 | 3.375 MB | Internal filesystem (currently unused; reading data lives on SD) |
| `coredump` | data/coredump | 0xFF0000 | 64 KB | Crash dump storage |

The two `app` partitions enable OTA updates: the device boots from one slot and writes new firmware to the other, then swaps on next boot.

## Key compile flags

| Flag | Default | Effect |
|------|---------|--------|
| `EINK_DISPLAY_SINGLE_BUFFER_MODE=1` | Always on | Allocate one framebuffer (48 KB) instead of two. Mandatory on ESP32-C3. |
| `OMIT_TEENSY_FONT` | xlarge env | Exclude 8 px Chareink font variant |
| `OMIT_TINY_FONT` | xlarge env | Exclude 10 px Chareink font variant |
| `OMIT_SMALL_FONT` | xlarge env | Exclude 12 px Chareink font variant |
| `OMIT_XLARGE_FONT` | tiny env | Exclude 18 px Chareink font variant |
| `OMIT_HUGE_FONT` | tiny, no_emoji envs | Exclude 20 px Chareink font variant |
| `OMIT_EMOJI_FONTS` | no_emoji env | Exclude emoji/symbol glyph data from reading fonts |
| `PNG_MAX_BUFFERED_PIXELS=3200` | Always | Limit PNG scanline buffer to ~13 KB (was 65 KB; caused heap fragmentation on ESP32-C3) |
| `LOG_LEVEL` | 1 (release), 2 (debug) | 0=off, 1=info, 2=debug |
| `ENABLE_SERIAL_LOG` | On except slim | Enable `LOG_*` output to UART |
| `SIMULATOR` | simulator env only | Gate SDL2/POSIX simulator code paths |
| `XML_GE=0` | Always | Disable Expat general entity expansion (saves flash) |

## Adding a new library dependency

1. Check for known vulnerabilities before adding:

   ```sh
   # Example: checking a hypothetical npm package would use gh-advisory-database
   # For PlatformIO/Arduino, check https://github.com/advisories manually or via the advisory database tool
   ```

2. Add the dependency to `lib_deps` in the `[base]` section of `platformio.ini`:

   ```ini
   authorname/LibraryName @ ^1.2.3
   ```

3. If the library needs a compatibility patch, add a patch script under `scripts/` and register it as a `pre:` step in `extra_scripts`.

4. Prefer libraries already in use (ArduinoJson, expat, SdFat) over introducing new ones. Each new dependency increases binary size and maintenance burden.

## Linting

```sh
./bin/clang-format-fix               # auto-format C++ with clang-format 21
pio check --fail-on-defect high      # cppcheck static analysis
```

The pre-commit hook (`.githooks/pre-commit`) runs `clang-format` on staged files. Enable it once per clone:

```sh
git config core.hooksPath .githooks
chmod +x .githooks/pre-commit
```
