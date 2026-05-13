---
title: E-paper display
parent: Contributing
nav_order: 10
---

# E-paper display

This document is the complete reference for anyone building a new UI or activity that draws to the screen. It covers the full stack from physical hardware to the drawing API, refresh strategy, fonts, and the threading model.

---

## Hardware overview

| Property | X4 | X3 |
|----------|----|----|
| Panel resolution | 800 × 480 px | 792 × 528 px |
| Color depth | 1-bit mono (black/white) | 1-bit mono |
| Grayscale rendering | 4-level via two-pass anti-aliasing | 4-level (with resync) |
| Interface | SPI (SCLK=8, MOSI=10, CS=21, DC=4, RST=5, BUSY=6) | same |
| Framebuffer size | 48 000 bytes (`800×480 / 8`) | 52 272 bytes max |

The framebuffer is a flat array of bits, one bit per pixel, most-significant bit first. A `1` bit is white; a `0` bit is black.

**Single-buffer constraint:** `EINK_DISPLAY_SINGLE_BUFFER_MODE=1` is set in `platformio.ini` and must never be disabled. On the ESP32-C3, a second 48 KB buffer would consume roughly 25 % of the ~380 KB available SRAM and cause heap fragmentation.

---

## Layer overview

```
Activity::render()            ← your code draws here
   └─ GfxRenderer             ← 2-D drawing API  (lib/GfxRenderer/)
        └─ HalDisplay         ← HAL wrapper       (lib/hal/HalDisplay.h)
             └─ EInkDisplay   ← SDK driver        (open-x4-sdk/libs/display/)
                  └─ SPI bus → physical e-ink panel
```

**Rule:** App code (anything in `src/` or non-HAL `lib/`) must only call `GfxRenderer` methods or `HalDisplay` methods where `GfxRenderer` does not provide the needed primitive. Never call `EInkDisplay` directly; it will not compile in the simulator.

---

## HalDisplay

`lib/hal/HalDisplay.h` — global singleton `display`.

### Refresh modes

| Enum | Approx. duration | When to use |
|------|-----------------|-------------|
| `HalDisplay::FAST_REFRESH` | ~300 ms | Normal page turns, most UI transitions |
| `HalDisplay::HALF_REFRESH` | ~1 720 ms | Balanced quality; clears moderate ghosting |
| `HalDisplay::FULL_REFRESH` | ~3 s | Remove deep persistent ghosting |

Use `FAST_REFRESH` for everything unless the UI has been on screen long enough to ghost, or the user has explicitly triggered a screen clean.

### Key methods

| Method | Purpose |
|--------|---------|
| `begin()` | Initialize hardware and SPI (called once in `main.cpp`) |
| `clearScreen(color)` | Fill framebuffer (white = `0xFF`, black = `0x00`). Does **not** push to panel. |
| `displayBuffer(mode, turnOff)` | Push framebuffer to panel with the given refresh mode |
| `refreshDisplay(mode, turnOff)` | Alias for `displayBuffer`; identical behaviour |
| `deepSleep()` | Put the panel into standby (low power) |
| `getFrameBuffer()` | Raw pointer to the framebuffer bytes |
| `copyGrayscale*()` | Grayscale two-pass helpers (called by `GfxRenderer`) |
| `displayGrayBuffer()` | Flush the merged grayscale result to the panel |
| `getDisplayWidth()` / `getDisplayHeight()` | Runtime panel dimensions (handles X3 vs X4) |

You almost never need to call `HalDisplay` directly. `GfxRenderer::displayBuffer()` handles refresh internally.

---

## GfxRenderer

`lib/GfxRenderer/GfxRenderer.h` — global singleton `renderer` (constructed in `main.cpp`).

`GfxRenderer` owns the 2-D drawing API. All draw calls write into the framebuffer in memory. Nothing appears on the physical screen until you call `displayBuffer()`.

### Coordinate system

Logical coordinates depend on the current orientation:

| Orientation | Logical width | Logical height | Notes |
|-------------|--------------|----------------|-------|
| `Portrait` (default) | 480 | 800 | (0,0) is top-left |
| `LandscapeClockwise` | 800 | 480 | rotated 180° |
| `PortraitInverted` | 480 | 800 | inverted |
| `LandscapeCounterClockwise` | 800 | 480 | native panel orientation |

```cpp
renderer.setOrientation(GfxRenderer::Portrait);
int w = renderer.getScreenWidth();   // 480
int h = renderer.getScreenHeight();  // 800
```

Orientation is set once per activity in `onEnter()` and restored in `onExit()` if changed.

### Viewable margins

A small margin around the screen edge is not reliably visible due to panel bezel. Use these constants when computing layout:

```cpp
GfxRenderer::VIEWABLE_MARGIN_TOP    = 9
GfxRenderer::VIEWABLE_MARGIN_RIGHT  = 3
GfxRenderer::VIEWABLE_MARGIN_BOTTOM = 3
GfxRenderer::VIEWABLE_MARGIN_LEFT   = 3
```

Use `getOrientedViewableTRBL()` to get these values already adjusted for the current orientation.

### Colors

`GfxRenderer` uses a dithered color model. The `Color` enum maps to Bayer-matrix dither levels:

| Color | Value | Visual |
|-------|-------|--------|
| `Color::Clear` | `0x00` | Transparent (no-op pixel) |
| `Color::White` | `0x01` | White |
| `Color::LightGray` | `0x05` | ~25 % fill |
| `Color::DarkGray` | `0x0A` | ~75 % fill |
| `Color::Black` | `0x10` | Black |

Dithered fills use a 4×4 Bayer matrix pattern. They look best in `BW` render mode. For sharp solid shapes, use the `bool state` (black = `true`, white = `false`) variants of `drawRect`/`fillRect` instead.

### Render modes

| Mode | Purpose |
|------|---------|
| `GfxRenderer::BW` | Standard 1-bit rendering (default) |
| `GfxRenderer::GRAYSCALE_LSB` | First pass of two-pass grayscale pipeline |
| `GfxRenderer::GRAYSCALE_MSB` | Second pass; call `displayGrayBuffer()` to flush |

For most UI screens, stay in `BW` mode. Only switch to grayscale for cover images or artwork. See [Grayscale rendering](#grayscale-rendering) below.

### Screen operations

```cpp
renderer.clearScreen();                               // fill with white
renderer.clearScreen(0x00);                           // fill with black
renderer.displayBuffer(HalDisplay::FAST_REFRESH);     // push to panel
renderer.invertScreen();                              // XOR entire buffer
renderer.setInverted(true);                           // invert at push time
```

### Drawing primitives

All coordinates are in logical screen pixels.

```cpp
// Single pixel
renderer.drawPixel(x, y, /*black=*/true);

// Lines
renderer.drawLine(x1, y1, x2, y2);                   // 1px wide, black
renderer.drawLine(x1, y1, x2, y2, lineWidth, black);  // thick line

// Rectangles
renderer.drawRect(x, y, w, h);                        // outline, 1px
renderer.drawRect(x, y, w, h, lineWidth, black);      // outline, thick
renderer.fillRect(x, y, w, h);                        // solid black fill
renderer.fillRect(x, y, w, h, /*state=*/false);       // solid white fill
renderer.fillRectDither(x, y, w, h, Color::LightGray);// dithered fill

// Rounded rectangles
renderer.drawRoundedRect(x, y, w, h, lineWidth, radius, black);
renderer.fillRoundedRect(x, y, w, h, radius, Color::Black);
// Per-corner control:
renderer.fillRoundedRect(x, y, w, h, radius,
    /*roundTL=*/true, /*roundTR=*/true,
    /*roundBL=*/false, /*roundBR=*/false,
    Color::Black);

// Mask pixels outside rounded corners to a color (useful for clipping)
renderer.maskRoundedRectOutsideCorners(x, y, w, h, radius, Color::White);

// Arcs (quarter-circle corners)
renderer.drawArc(maxRadius, cx, cy, xDir, yDir, lineWidth, black);

// Polygons
int xs[] = {x0, x1, x2};
int ys[] = {y0, y1, y2};
renderer.fillPolygon(xs, ys, 3);

// 1-bit bitmaps (raw byte arrays, MSB first)
renderer.drawImage(bitmap, x, y, width, height);       // black pixels set; white clears
renderer.drawIcon(bitmap, x, y, width, height);        // same, alias for icons
```

### Bitmap drawing

`Bitmap` is a decoded image object (used by the cover art pipeline):

```cpp
renderer.drawBitmap(bitmap, x, y, maxWidth, maxHeight);          // dithered
renderer.drawBitmap1Bit(bitmap, x, y, maxWidth, maxHeight);      // flat 1-bit
```

---

## Text and fonts

### Font IDs

Every font is registered with an integer ID. Font IDs are compile-time hashes defined in `src/fontIds.h`. Never hard-code the numeric value — always use the `#define` name.

Built-in UI fonts:

| ID macro | Typeface | Size | Styles |
|----------|----------|------|--------|
| `SMALL_FONT_ID` | Inter | 8 px | Regular |
| `UI_10_FONT_ID` | Inter | 10 px | Regular, Bold |
| `UI_12_FONT_ID` | Inter | 12 px | Regular, Bold |
| `CHAREINK_*_FONT_ID` | CharEInk | 8–20 px | Regular, Bold, Italic, Bold-Italic |
| `LEXENDDECA_*_FONT_ID` | Lexend Deca | 8–20 px | Regular, Bold, Italic, Bold-Italic |
| `BITTER_*_FONT_ID` | Bitter | 8–20 px | Regular, Bold, Italic, Bold-Italic |
| `MATERIAL_SYMBOLS_20_FONT_ID` | Material Symbols Rounded | 20 px | Icon glyphs |
| `MATERIAL_SYMBOLS_24_FONT_ID` | Material Symbols Rounded | 24 px | Icon glyphs |

`_*_` represents sizes 8, 10, 12, 14, 16, 18, 20 (not all sizes are included in every build; some are guarded by `#ifndef OMIT_*_FONT`).

### Font styles

```cpp
EpdFontFamily::REGULAR
EpdFontFamily::BOLD
EpdFontFamily::ITALIC
EpdFontFamily::BOLD_ITALIC
EpdFontFamily::UNDERLINE     // modifier flag, OR with a base style
EpdFontFamily::STRIKETHROUGH // modifier flag, OR with a base style
```

### Text drawing

```cpp
// Draw at position (x, y) — y is the baseline
renderer.drawText(LEXENDDECA_14_FONT_ID, x, y, "Hello");
renderer.drawText(LEXENDDECA_14_FONT_ID, x, y, "Bold", true, EpdFontFamily::BOLD);

// Centered on x-axis
renderer.drawCenteredText(LEXENDDECA_14_FONT_ID, y, "Centered");

// Rotated 90° clockwise (for side-button labels)
renderer.drawTextRotated90CW(UI_12_FONT_ID, x, y, "Label");
```

### Text measurement

Measure before drawing to compute layout:

```cpp
int w = renderer.getTextWidth(LEXENDDECA_14_FONT_ID, "text");
int h = renderer.getTextHeight(LEXENDDECA_14_FONT_ID);       // cap-height
int lh = renderer.getLineHeight(LEXENDDECA_14_FONT_ID);      // full line advance

// Truncate to fit a column
std::string t = renderer.truncatedText(LEXENDDECA_14_FONT_ID, longStr, maxWidth);

// Word-wrap into at most N lines
std::vector<std::string> lines =
    renderer.wrappedText(LEXENDDECA_14_FONT_ID, longStr, maxWidth, /*maxLines=*/3);

// Kerning and space advance (needed for precise paragraph layout)
int sp = renderer.getSpaceAdvance(fontId, leftCp, rightCp, EpdFontFamily::REGULAR);
int k  = renderer.getKerning(fontId, leftCp, rightCp, EpdFontFamily::REGULAR);
```

### SD card fonts

Users can install `.epdf` font files on the SD card. If a user has selected an SD font, `sdFontSystem` registers it with the renderer at the same font ID as the replaced built-in. Your code does not need to change — calls to `drawText` will automatically use the SD font.

If you call `ensureSdCardFontReady(fontId, text)` before measuring text, the renderer will pre-cache glyph metrics for that string (this avoids SD reads mid-layout). This is handled automatically inside the EPUB/Markdown rendering pipeline; you only need it if you are building a custom layout engine.

### Material Symbols icons

Material Symbols Rounded is a font where each codepoint renders a vector icon. Use the `UIIcon` enum (defined in `src/components/themes/BaseTheme.h`) or look up codepoints in `lib/EpdFont/scripts/convert-builtin-fonts.sh`.

```cpp
// Draw the bookmark icon glyph directly with the icon font
renderer.drawText(MATERIAL_SYMBOLS_24_FONT_ID, x, y, "\xEE\xA0\x80"); // UTF-8 encoded codepoint
```

In practice, icons are drawn by the theme helpers (`GUI.drawList(...)`, `GUI.drawButtonHints(...)`) which handle icon rendering internally.

---

## UITheme and BaseTheme components

The `GUI` macro gives access to the current theme:

```cpp
#include "components/UITheme.h"

// Get layout metrics (row heights, padding, etc.)
const ThemeMetrics& m = UITheme::getInstance().getMetrics();

// Draw reusable components
GUI.drawHeader(renderer, Rect(0, 0, w, m.headerHeight), "Title");
GUI.drawList(renderer, contentRect, itemCount, selectedIndex,
    [](int i) { return items[i].name; });           // title
    [](int i) { return items[i].subtitle; });       // subtitle (optional)
GUI.drawButtonHints(renderer, "Back", "Select", "", "");
GUI.drawTabBar(renderer, tabRect, tabs, /*hasFocus=*/true);
GUI.drawStatusBar(renderer, progress, page, pageCount, title);
GUI.drawSpinner(renderer, cx, cy, "Loading...", spinnerFrame);
```

`ThemeMetrics` contains all the layout constants you need:

| Field | Typical value | Purpose |
|-------|--------------|---------|
| `headerHeight` | 45 | Height of the title bar |
| `contentSidePadding` | 20 | Left/right margin for content |
| `listRowHeight` | 30 | Single-line list row |
| `listWithSubtitleRowHeight` | 50 | List row with subtitle |
| `buttonHintsHeight` | 40 | Bottom button hint bar |
| `tabBarHeight` | 50 | Tab strip |
| `progressBarHeight` | 16 | Reading progress bar |
| `statusBarHorizontalMargin` | 5 | Status bar side margin |

---

## Grayscale rendering

Grayscale works by running two rendering passes through the same scene and merging the results into the panel's dual-RAM:

```
Pass 1 (GRAYSCALE_LSB):  draw scene at half intensity → copyGrayscaleLsbBuffers()
Pass 2 (GRAYSCALE_MSB):  draw scene at full intensity → copyGrayscaleMsbBuffers()
displayGrayBuffer()  →  merged result pushed to panel
```

```cpp
// Example: render a cover image in grayscale
renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
renderer.clearScreen();
drawCoverScene(renderer);
renderer.copyGrayscaleLsbBuffers();

renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
renderer.clearScreen();
drawCoverScene(renderer);
renderer.copyGrayscaleMsbBuffers();

renderer.displayGrayBuffer();           // or: renderer.cleanupGrayscaleWithFrameBuffer()
renderer.setRenderMode(GfxRenderer::BW);
```

`storeBwBuffer()` / `restoreBwBuffer()` let you preserve the current B&W framebuffer around a grayscale rendering operation so the surrounding UI can be restored afterwards without a full redraw.

---

## Refresh strategy

| Situation | Recommended mode |
|-----------|-----------------|
| Normal UI navigation | `FAST_REFRESH` |
| After displaying a cover image | `HALF_REFRESH` |
| After long grayscale session | `HALF_REFRESH` |
| User-triggered screen clean | `FULL_REFRESH` |
| Power-on first frame | `FULL_REFRESH` (already done in `BootActivity`) |

Avoid `FULL_REFRESH` in loops or on every transition — 3 seconds of refresh is highly visible. Batch it to moments the user expects a pause.

### Fading fix

Some panels accumulate residual charge over time, causing faint "ghost" images to appear in white areas. If your activity is static for more than a minute, enabling the fading fix causes the next refresh to run an extra ghost-clearing pass:

```cpp
renderer.setFadingFix(true);
```

This is enabled automatically in the EPUB reader; you normally do not need to manage it yourself.

---

## Threading model

The display pipeline runs on a **dedicated render task**, not the main loop task.

```
Main loop task (FreeRTOS)     Render task (FreeRTOS)
──────────────────────────    ─────────────────────────────
activity.loop()               [blocked on semaphore]
requestUpdate()  ────────────►  acquires RenderLock
                               activity.render(lock)   ← draw calls here
                               renderer.displayBuffer() ← pushes to panel
                               releases RenderLock
                              [blocked again]
```

**Never call draw methods from `loop()`.** Drawing inside `loop()` races with the render task and will corrupt the framebuffer.

### RenderLock

`RenderLock` is an RAII wrapper around the render semaphore. It is passed into `render(RenderLock&&)` by the framework — you do not create it yourself inside `render()`.

If you need to push a display update synchronously (e.g., from `onEnter()`), use `requestUpdateAndWait()`:

```cpp
void MyActivity::onEnter() {
    Activity::onEnter();
    // ... allocate resources ...
    requestUpdateAndWait();   // blocks until first render completes
}
```

If you hold a `RenderLock` (e.g., you are inside `render()`) and need to call `displayBuffer()` manually, call it directly on `renderer` — do not call `requestUpdate()` recursively.

---

## Minimal activity template

```cpp
#include <GfxRenderer.h>
#include "activities/Activity.h"
#include "components/UITheme.h"
#include "fontIds.h"

class HelloActivity : public Activity {
 public:
  HelloActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity(renderer, input) {}

  void onEnter() override {
    Activity::onEnter();
    requestUpdate();
  }

  void loop() override {
    if (mappedInputManager.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    }
  }

  void render(RenderLock&&) override {
    const ThemeMetrics& m = UITheme::getInstance().getMetrics();
    const int w = renderer.getScreenWidth();

    renderer.clearScreen();

    // Header bar
    GUI.drawHeader(renderer,
        Rect(0, 0, w, m.headerHeight), "Hello");

    // Content
    renderer.drawText(LEXENDDECA_14_FONT_ID,
        m.contentSidePadding,
        m.headerHeight + 30,
        "My new UI screen");

    // Button hints
    GUI.drawButtonHints(renderer, "Back", "", "", "");

    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
};
```

---

## Quick reference

### Common layout recipe

```cpp
const ThemeMetrics& m = UITheme::getInstance().getMetrics();
const int W = renderer.getScreenWidth();
const int H = renderer.getScreenHeight();

// Reserve zones:
const int headerBottom  = m.headerHeight;
const int hintsTop      = H - m.buttonHintsHeight;
const int contentTop    = headerBottom + m.verticalSpacing;
const int contentBottom = hintsTop - m.verticalSpacing;
const int contentLeft   = m.contentSidePadding;
const int contentRight  = W - m.contentSidePadding;
const int contentWidth  = contentRight - contentLeft;
```

### Checking how many list items fit

```cpp
int rows = UITheme::getNumberOfItemsPerPage(
    renderer,
    /*hasHeader=*/true,
    /*hasTabBar=*/false,
    /*hasButtonHints=*/true,
    /*hasSubtitle=*/false);
```

### Drawing a progress/scroll indicator

```cpp
// Reading progress bar (bottom of screen in reader)
GUI.drawStatusBar(renderer,
    /*bookProgress=*/0.42f,
    /*currentPage=*/5,
    /*pageCount=*/120,
    /*title=*/"Chapter 3");
```
