---
title: Libraries
parent: Contributing
nav_order: 5
---

# Libraries

Every subdirectory under `lib/` is a self-contained C++ library that can be compiled independently (some have host-only tests under `test/`). This page is a quick reference card for each one.

## Content and format parsing

### `Epub/`

EPUB 2 and 3 parser, CSS engine, text layout pipeline, and hyphenation trie. Entry point: `Epub.h`. This is the largest and most complex library in the project.

Key subsystems inside `lib/Epub/Epub/`:

- **OPF/TOC parser** — locates the package document and builds the spine and table of contents
- **Chapter HTML parser** — `ChapterHtmlSlimParser` strips markup and emits styled text tokens
- **Layout engine** — word-wraps tokens into `Page` objects at the configured viewport width
- **Hyphenation** — trie-based algorithm; generated header under `hyphenation/generated/`
- **CSS engine** — parses inline and linked stylesheets, applies to layout

Caching: parsed layout is written to `/.crosspoint/epub_<hash>/sections/*.bin` so subsequent page turns skip the expensive parse+layout step. Cache is busted whenever relevant settings change (font, size, viewport, hyphenation, etc.).

**If you change `section.bin` or `book.bin` layouts**, bump the format version in the corresponding reader code and document the change in `docs/file-formats.md`.

### `Txt/`

Plain-text (`.txt`) file reader. Minimal layout: line-wraps at viewport width. Entry point: `Txt.h`.

### `Xtc/`

XTC (CrossPoint Binary) format reader. XTC is a pre-laid-out binary book format optimized for very fast page turns. Entry point: `Xtc.h`.

### `Markdown/`

Block-level Markdown parser for reading `.md` / `.markdown` files. Uses `lib/Markdown::BlockParser`. Cache lives under `.crosspoint/md_<hash>/` with magic bytes `MDKI`, version 1. Page breaks are forbidden mid fenced-code-block. Entry point: `MarkdownParser.h`.

### `XmlParserUtils/` and `expat/`

XML/HTML parsing for EPUB content documents. `expat/` is the embedded Expat XML library. `XmlParserUtils/` provides helpers on top of Expat. Build flag `XML_GE=0` disables general entity expansion to save flash.

### `OpdsParser/`

OPDS (Open Publication Distribution System) catalog XML parser. Used by the OPDS browser to list and download ebooks from catalog servers. Entry point: `OpdsParser.h`.

### `VCard/`

vCard `.vcf` parser for the Contacts PDA feature. Reads `/contacts.vcf` from the SD card root. Index is cached at `/.crosspoint/contacts.bin` (magic `VCFX`, version 1). Entry point: `VCard.h`.

---

## Rendering and display

### `GfxRenderer/`

2-D drawing API that writes into the e-ink framebuffer. All activity UI is built with these calls. Entry point: `GfxRenderer.h`.

Key areas:

- **Pixel, line, rect, rounded-rect, polygon drawing** — both filled and outlined, with dithering for gray levels
- **Bitmap rendering** — BMP, 1-bit, crop/fit modes
- **Text rendering** — renders via `EpdFontFamily`, handles UTF-8, kerning, word-wrap, rotation
- **Render modes** — `BW` (1-bit), `GRAYSCALE_LSB`, `GRAYSCALE_MSB` (for anti-aliased text two-pass)
- **Orientation** — portrait/landscape/inverted; transforms coordinates transparently

`GfxRenderer` does not push to the display itself — call `displayBuffer()` when a frame is ready.

### `Bitmap/` (inside `GfxRenderer/`)

In-memory bitmap representation. Used for cover art and image decoding intermediates.

### `PngToBmpConverter/`

Decodes PNG files (via `PNGdec`) to in-memory `Bitmap` for cover art. Entry point: `PngToBmpConverter.h`.

### `JpegToBmpConverter/`

Decodes JPEG files (via `JPEGDEC`) to in-memory `Bitmap`. Entry point: `JpegToBmpConverter.h`.

---

## Font system

### `EpdFont/`

Font data structures and all built-in font arrays. Entry points: `builtinFonts/all.h` (includes every compiled font), `EpdFontFamily.h`.

Built-in fonts compiled into flash:
- **Lexend Deca** — UI font (14 px regular/bold/italic/bold-italic)
- **Chareink** — reader font (8/10/12/14/16/18/20 px, size variants controlled by `OMIT_*_FONT` flags)
- **Bitter** — alternative reader serif font
- **Material Symbols Rounded** — icon font at 20 px and 24 px

The Material Symbols icon font codepoints are subsetted at build time. The codepoint-to-`UIIcon` enum table lives in `src/components/MaterialIcons.h`. Font IDs are in `src/fontIds.h`.

To add or change the icon subset, edit `lib/EpdFont/scripts/convert-builtin-fonts.sh` and regenerate.

---

## Hardware abstraction (HAL)

### `hal/`

Wrappers over `open-x4-sdk` SDK classes. App code must use these; never call SDK classes directly. See [HAL layer](./hal.md) for full details.

| Header | Class | Role |
|--------|-------|------|
| `HalDisplay.h` | `HalDisplay` | E-ink panel, framebuffer, refresh modes |
| `HalGPIO.h` | `HalGPIO` | Buttons, USB detection, SPI setup, device type |
| `HalPowerManager.h` | `HalPowerManager` | CPU freq, battery %, RAII power lock |
| `HalStorage.h` | `HalStorage`, `HalFile` | SD card I/O (`FsFile` alias) |
| `HalSystem.h` | `HalSystem` namespace | Panic info, crash dump |
| `HalTiltSensor.h` | `HalTiltSensor` | QMI8658 IMU, tilt-page-turn gestures |
| `HalSpiBus.h` | `HalSpiBus` | SPI bus sharing between display and SD |

---

## Compression and archives

### `ZipFile/`

ZIP archive reader for EPUB containers (EPUBs are ZIP files). Entry point: `ZipFile.h`.

### `InflateReader/`

Streaming DEFLATE decompressor. Used by `ZipFile` to expand compressed entries. Entry point: `InflateReader.h`.

### `uzlib/`

Embedded µzlib DEFLATE implementation (C library). Used internally by `InflateReader`.

---

## Text and encoding utilities

### `Utf8/`

UTF-8 codepoint iteration, byte-length helpers, and safe truncation. Entry point: `Utf8.h`.

Do not cast `string_view::data()` directly to C string APIs — it is not null-terminated. Use `Utf8` helpers or `snprintf` with explicit lengths.

---

## Serialization and settings

### `JsonParser/`

Minimal streaming JSON parser (no heap allocation). Used for settings, credential stores, and OPDS responses. Entry point: `JsonParser.h`.

### `Serialization/`

Binary serialization helpers (read/write length-prefixed strings, integers, booleans) used for all `*.bin` cache files. Entry point: `Serialization.h`.

---

## Networking and sync

### `KOReaderSync/`

KOReader progress synchronization protocol client. Sends and receives reading position over HTTP. Entry point: `KOReaderSync.h`.

### `GitHubClient/`

GitHub REST API client used by the GitHub companion feature. URL/body builders are `static` and host-testable (no Arduino dependency). HTTP methods are `#ifdef ARDUINO` gated. Entry point: `GitHubClient.h`.

Credentials (`/.crosspoint/github.json`) use a single PAT, MAC-XOR obfuscated. **The PAT must never appear in logs.** `LOG_DBG` calls in `GitHubClient.cpp` must never include the token.

Host tests: `test/run_github_test.sh` — runs without hardware.

---

## Application support

### `I18n/`

Translation lookup. All user-visible strings must use `tr(STR_KEY)`. Source is YAML in `lib/I18n/translations/`; run `scripts/gen_i18n.py` to regenerate C++ headers. Entry point: `I18n.h`.

### `AppVersion/`

Version string constants derived from `platformio.ini` and injected at build time by `scripts/git_branch.py`. Entry point: `AppVersion.h`.

### `Logging/`

`LOG_INF(tag, fmt, ...)`, `LOG_DBG(tag, fmt, ...)`, `LOG_ERR(tag, fmt, ...)` macros. Conditional on `ENABLE_SERIAL_LOG` and `LOG_LEVEL`. Entry point: `Logging.h`.

### `FsHelpers/`

Path manipulation and directory traversal helpers wrapping `HalStorage`/`HalFile`. Entry point: `FsHelpers.h`.

---

## Third-party embedded libraries

| Library | Version / source | Purpose |
|---------|-----------------|---------|
| `expat/` | Bundled Expat | XML parsing for EPUB |
| `uzlib/` | µzlib | DEFLATE decompression |
| `bblanchon/ArduinoJson` | 7.4.2 (via PlatformIO) | JSON for settings/stores |
| `ricmoo/QRCode` | 0.0.1 | QR code generation |
| `bitbank2/PNGdec` | ^1.0.0 | PNG decoding |
| `bitbank2/JPEGDEC` | pinned commit | JPEG decoding |
| `links2004/WebSockets` | 2.7.3 | WebSocket upload server |
