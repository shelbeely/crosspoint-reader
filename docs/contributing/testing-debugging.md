---
title: Testing & Debugging
parent: Contributing
nav_order: 4
---

# Testing and Debugging

Biscuit runs on real hardware, so debugging usually combines local build checks and on-device logs.

## Local checks

Make sure `clang-format` 21+ is installed and available in `PATH` before running the formatting step.
If needed, see [Getting Started](./getting-started.md).

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

## Simulator

The `env:simulator` build compiles the firmware to a native desktop application using SDL2. Use it for UI development and regression testing without needing hardware.

### Setup (macOS)

```sh
brew install sdl2
pio run -e simulator
```

### Setup (Linux)

```sh
sudo apt-get install libsdl2-dev
pio run -e simulator
```

The simulator is launched automatically by PlatformIO after a successful build (via `scripts/run_simulator.py`). The window renders the e-ink framebuffer in real time.

**Known simulator limits:**

- No image decoding: `PNGdec` and `JPEGDEC` are stubbed out; JPEG errors (`JPEGDEC fallback: open failed (err=-1)`) are expected.
- `esp_deep_sleep_start()` is a no-op — the process keeps running.
- `HalStorage` uses POSIX file I/O under `./fs_` instead of SdFat; multiple files can be open simultaneously (unlike real hardware).
- Simulator patches belong in the adjacent `crosspoint-simulator` repo, not here.

## Native unit tests

The `env:native` environment runs host-only unit tests via PlatformIO Unity (no display, no hardware).

Test source lives under `test/`. Each subdirectory is a self-contained test:

| Test | What it covers |
|------|---------------|
| `test_dice_roller/` | Dice roller probability distribution |
| `test_morse_code/` | Morse encode/decode |
| `test_unit_converter/` | Unit conversion accuracy |
| `streaming_json_parser/` | Streaming JSON parser |
| `release_json_parser/` | Release JSON parser (GitHub API responses) |
| `differential_rounding/` | Fixed-point rounding helpers |
| `hyphenation_eval/` | Hyphenation trie accuracy |

Run all native tests:

```sh
pio test -e native
```

Run a specific test:

```sh
pio test -e native -f test_unit_converter
```

## GitHub companion host tests

`test/run_github_test.sh` runs the `GitHubClient` parsers against fixture JSON files without any hardware or Arduino dependency. This works because the parser code is pure C++ with no `#ifdef ARDUINO` guards.

```sh
cd test
./run_github_test.sh
```

Similarly:
- `test/run_differential_rounding_test.sh`
- `test/run_hyphenation_eval.sh`
- `test/run_release_json_parser_test.sh`
- `test/run_streaming_json_parser_test.sh`

## Flash and monitor

Flash firmware:

```sh
pio run --target upload
```

Open serial monitor:

```sh
pio device monitor
```

Optional enhanced monitor:

```sh
python3 -m pip install pyserial colorama matplotlib
python3 scripts/debugging_monitor.py
```

`debugging_monitor.py` provides:
- Color-coded log output: `LOG_INF` (green), `LOG_DBG` (cyan), `LOG_ERR` (red)
- Real-time heap graph (matplotlib window) — useful for catching heap fragmentation
- Automatic timestamp correlation

## Common crash causes

**Stack overflow**  
Tasks with too little stack will corrupt adjacent memory and crash unpredictably. Symptoms: random crashes, garbled output, watchdog reset. Check: `uxTaskGetStackHighWaterMark(NULL)` in `loop()` — if the watermark is near zero, increase the stack.

Default task stack sizes:
- Simple rendering/state tasks: 2048 bytes
- Network, EPUB parsing, file I/O: 4096 bytes

**Heap fragmentation**  
The ESP32-C3 has ~380 KB SRAM and no PSRAM. Repeated small allocations and frees can fragment the heap so that large contiguous allocations fail even when total free memory is high. Symptoms: `malloc` returns null, display goes blank, random crashes after extended use.

Mitigation:
- Allocate large buffers once in `onEnter()`, reuse across frames, free in `onExit()`
- `std::vector::reserve(N)` before push loops
- Avoid `std::string` and Arduino `String` in hot paths

**Unaligned pointer cast**  
Casting a `uint8_t*` buffer to a wider type (`uint16_t*`, `uint32_t*`, struct pointer) is undefined behavior on RISC-V and can cause a fault or silently return wrong values. Always use `memcpy` to read multi-byte values from unaligned buffers.

**`xSemaphoreTake` from an ISR**  
ISR handlers run in interrupt context and cannot block. Calling `xSemaphoreTake()` from an ISR will crash or deadlock. Use the ISR-safe `xSemaphoreGiveFromISR()` variant to signal from an ISR and handle work in a normal task.

**One file open at a time (real hardware)**  
SdFat on real hardware allows only one open reader per file path. If your code tries to open the same file twice (e.g., a fallback that reopens without closing the first handle), it will fail silently. Always close the first `HalFile` / `FsFile` before reopening the same path.

**PAT in logs**  
The GitHub PAT stored in `/.crosspoint/github.json` must never appear in `LOG_*` output. If you add GitHub API logging, exclude the `Authorization` header value.

## Useful bug report contents

- Firmware version and build environment
- Exact steps to reproduce
- Expected vs actual behavior
- Serial logs from boot through failure
- Whether issue reproduces after clearing `.crosspoint/` cache on SD card

## Common troubleshooting references

- [User Guide troubleshooting section](../../USER_GUIDE.md#7-troubleshooting-issues--escaping-bootloop)
- [Webserver troubleshooting](../troubleshooting.md)
