---
title: HAL layer
parent: Contributing
nav_order: 7
---

# HAL layer

The Hardware Abstraction Layer (HAL) isolates application code from the low-level hardware SDK. This makes the codebase testable in the simulator (which substitutes SDL2 implementations for the hardware drivers) and keeps app code free of board-specific details.

## The rule

**App code must use HAL classes. Never use `open-x4-sdk` SDK classes directly.**

If you call `EInkDisplay`, `InputManager`, `SDCardManager`, or other SDK types in `src/` or `lib/` (outside of `lib/hal/`), that code will not compile in the simulator and will be harder to port.

## `open-x4-sdk/` submodule

The hardware SDK lives in `open-x4-sdk/`, which is a git submodule. It provides:

- `EInkDisplay` — e-ink panel driver (display, refresh, framebuffer)
- `InputManager` — raw button polling
- `SDCardManager` / SdFat — SD card filesystem
- `BatteryMonitor` — battery voltage / percentage via ADC or I2C fuel gauge

To update the submodule:

```sh
git submodule update --remote open-x4-sdk
```

After an SDK update, verify that any changes to SDK APIs are reflected in the HAL wrapper before merging.

## HAL classes

All HAL headers live in `lib/hal/`.

### `HalDisplay` — `HalDisplay.h`

Wraps `EInkDisplay`. Owns the single e-ink panel and its framebuffer.

| Method | Purpose |
|--------|---------|
| `begin()` | Initialize display hardware and SPI |
| `displayBuffer(mode)` | Push the current framebuffer to the panel with the given refresh mode |
| `clearScreen(color)` | Fill the framebuffer (does not push to panel) |
| `deepSleep()` | Put the display into deep sleep (low power standby) |
| `getFrameBuffer()` | Raw pointer to the 48 KB framebuffer |
| `copyGrayscale*()` | Grayscale two-pass rendering helpers |

**Refresh modes:**

| Mode | Duration | Use case |
|------|----------|---------|
| `FAST_REFRESH` | ~300 ms (custom LUT) | Normal page turns |
| `HALF_REFRESH` | ~1720 ms | Balanced quality |
| `FULL_REFRESH` | ~3 s | Remove deep ghosting |

**Display pipeline:**

```
GfxRenderer::displayBuffer()
  └─ HalDisplay::displayBuffer()
       └─ EInkDisplay (open-x4-sdk)
            └─ SPI bus (pins: SCLK=8, MOSI=10, CS=21, DC=4, RST=5, BUSY=6)
                 └─ physical e-ink panel (800×480, 1-bit mono)
```

**Single-buffer constraint:** `EINK_DISPLAY_SINGLE_BUFFER_MODE=1` is set in `platformio.ini`. The framebuffer is 48 000 bytes (`800 × 480 / 8`). A second buffer would consume another 48 KB of the ~380 KB SRAM — that is ~25 % of available RAM — causing heap fragmentation and memory errors. Never disable this flag.

Global singleton: `extern HalDisplay display;` (defined in `lib/hal/HalDisplay.h`, instantiated in `open-x4-sdk`).

---

### `HalGPIO` — `HalGPIO.h`

Wraps `InputManager`. Handles all button input, USB connection detection, SPI pin initialization, and deep-sleep wake configuration.

| Method | Purpose |
|--------|---------|
| `begin()` | Init GPIO pins, SPI bus, device-type detection |
| `update()` | Poll button state (call once per loop iteration) |
| `wasPressed(idx)` / `wasReleased(idx)` | Edge detection |
| `isPressed(idx)` | Level detection |
| `startDeepSleep()` | Configure wake-up pins and enter deep sleep |
| `verifyPowerButtonWakeup(ms, shortOk)` | Validate power button hold duration after wakeup |
| `isUsbConnected()` | USB-C connection state |
| `getWakeupReason()` | `PowerButton`, `AfterFlash`, `AfterUSBPower`, `Other` |

Button index constants: `BTN_BACK=0`, `BTN_CONFIRM=1`, `BTN_LEFT=2`, `BTN_RIGHT=3`, `BTN_UP=4`, `BTN_DOWN=5`, `BTN_POWER=6`.

In activity code, use `MappedInputManager::Button::*` enums instead of these raw indices. `MappedInputManager` applies front-button layout remapping on top of `HalGPIO`.

`HalGPIO` also auto-detects the hardware variant:
- `DeviceType::X4` — standard Xteink X4 (ESP32-C3, ADC battery)
- `DeviceType::X3` — older X3 hardware (different I2C devices)

Global singleton: `extern HalGPIO gpio;`

---

### `HalPowerManager` — `HalPowerManager.h`

CPU frequency scaling and battery monitoring.

| Method | Purpose |
|--------|---------|
| `begin()` | Initialize battery monitor and default clock speed |
| `setPowerSaving(bool)` | Scale CPU to 10 MHz (idle) or restore normal speed |
| `startDeepSleep(gpio)` | Enter deep sleep (calls `gpio.startDeepSleep()`) |
| `getBatteryPercentage()` | Battery % (0–100); cached, polled every 1500 ms |

**`HalPowerManager::Lock`** is an RAII helper that temporarily disables CPU scaling for latency-sensitive operations (e.g., display refresh, SD writes). Create it on the stack; when it goes out of scope, power saving is re-enabled.

Global singleton: `extern HalPowerManager powerManager;`

---

### `HalStorage` — `HalStorage.h`

Thread-safe SD card I/O built on SdFat. All file operations in application code must go through `HalStorage` / `HalFile`.

`HalFile` is aliased as `FsFile` for downstream code that was written against the raw SdFat API — so existing code that uses `FsFile` continues to work without changes.

**Important hardware constraint:** On real hardware, SdFat allows only one reader per file path at a time. If a fallback path needs to reopen the same file, the first handle must be closed before the second is opened.

```cpp
HalFile file;
if (Storage.openFileForRead("MyModule", "/some/file.bin", file)) {
    // read...
    file.close();
}
```

Access macro: `Storage` (`HalStorage::getInstance()`).

---

### `HalSystem` — `HalSystem.h`

Panic capture and crash dump helpers.

| Function | Purpose |
|----------|---------|
| `begin()` | Install panic hooks |
| `checkPanic()` | On boot: if previous run crashed, dump panic info to SD |
| `clearPanic()` | Acknowledge panic (call after user sees crash screen) |
| `getPanicInfo(full)` | Get panic string for display |
| `isRebootFromPanic()` | True if last reset was a panic |

Crash dumps are written to SD so they survive the power cycle. `CrashActivity` reads them via `HalSystem::getPanicInfo()`.

---

### `HalTiltSensor` — `HalTiltSensor.h`

QMI8658 IMU driver providing tilt-page-turn gestures.

| Method | Purpose |
|--------|---------|
| `begin()` | Probe I2C bus, configure gyroscope |
| `wake()` | Enable QMI8658 sensor engine |
| `deepSleep()` | Put QMI8658 into standby |
| `update(mode, orientation, inReader)` | Poll gyro and update gesture state machine |
| `wasTiltedForward()` | Consumed once: next-page gesture |
| `wasTiltedBack()` | Consumed once: previous-page gesture |
| `hadActivity()` | Non-consuming: any tilt since last call (resets sleep timer) |
| `clearPendingEvents()` | Discard buffered events (call when leaving reader) |

Global singleton: `extern HalTiltSensor halTiltSensor;`

---

### `HalSpiBus` — `HalSpiBus.h`

The display (SPI) and SD card (SPI) share the same MISO line. `HalSpiBus` arbitrates access to prevent contention.

This is transparent to app code — `HalDisplay` and `HalStorage` call `HalSpiBus` internally.

---

## Simulator compatibility

When building with `env:simulator`, the `lib/hal/` directory is **replaced** by implementations from the `crosspoint-simulator` repo (pulled as a PlatformIO lib dependency). The simulator uses SDL2 for rendering and POSIX file I/O for storage. Deep sleep is a no-op. Image decoding is stubbed out.

This means any code that uses only `lib/hal/` APIs will work in the simulator without modification.
