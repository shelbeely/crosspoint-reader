# File Formats

## `book.bin`

### Version 3

ImHex Pattern:

```c++
import std.mem;
import std.string;
import std.core;

// === Configuration ===
#define EXPECTED_VERSION 3
#define MAX_STRING_LENGTH 65535

// === String Structure ===

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

// === Metadata Structure ===

struct Metadata {
    String title [[comment("Book title")]];
    String author [[comment("Book author")]];
    String coverItemHref [[comment("Path to cover image")]];
    String textReferenceHref [[comment("Path to guided first text reference")]];
} [[comment("Book metadata information")]];

// === Spine Entry Structure ===

struct SpineEntry {
    String href [[comment("Resource path")]];
    u32 cumulativeSize [[comment("Cumulative size in bytes"), color("FF6B6B")]];
    s16 tocIndex [[comment("Index into TOC (-1 if none)"), color("4ECDC4")]];
} [[comment("Spine entry defining reading order")]];

// === TOC Entry Structure ===

struct TocEntry {
    String title [[comment("Chapter/section title")]];
    String href [[comment("Resource path")]];
    String anchor [[comment("Fragment identifier")]];
    u8 level [[comment("Nesting level (0-255)"), color("95E1D3")]];
    s16 spineIndex [[comment("Index into spine (-1 if none)"), color("F38181")]];
} [[comment("Table of contents entry")]];

// === Book Bin Structure ===

struct BookBin {
    // Header
    u8 version [[comment("Format version"), color("FFD93D")]];
    
    // Version validation
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }
    
    u32 lutOffset [[comment("Offset to lookup tables"), color("6BCB77")]];
    u16 spineCount [[comment("Number of spine entries"), color("4D96FF")]];
    u16 tocCount [[comment("Number of TOC entries"), color("FF6B9D")]];
    
    // Metadata section
    Metadata metadata [[comment("Book metadata")]];
    
    // Validate LUT offset alignment
    u32 currentOffset = $;
    if (currentOffset != lutOffset) {
        std::warning(std::format("LUT offset mismatch: expected 0x{:X}, got 0x{:X}", lutOffset, currentOffset));
    }
    
    // Lookup Tables
    u32 spineLut[spineCount] [[comment("Spine entry offsets"), color("4D96FF")]];
    u32 tocLut[tocCount] [[comment("TOC entry offsets"), color("FF6B9D")]];
    
    // Data Entries
    SpineEntry spines[spineCount] [[comment("Spine entries (reading order)")]];
    TocEntry toc[tocCount] [[comment("Table of contents entries")]];
};

// === File Parsing ===

BookBin book @ 0x00;

// Validate we've consumed the entire file
u32 fileSize = std::mem::size();
u32 parsedSize = $;

if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## `section.bin`

### Version 8

ImHex Pattern:

```c++
import std.mem;
import std.string;
import std.core;

// === Configuration ===
#define EXPECTED_VERSION 8
#define MAX_STRING_LENGTH 65535

// === String Structure ===

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

// === Page Structure ===

enum StorageType : u8 {
    PageLine = 1
};

enum WordStyle : u8 {
    REGULAR = 0,
    BOLD = 1,
    ITALIC = 2,
    BOLD_ITALIC = 3
};

enum BlockStyle : u8 {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
};

struct PageLine {
  s16 xPos;
  s16 yPos;
  u16 wordCount;
  String words[wordCount];
  u16 wordXPos[wordCount];
  WordStyle wordStyle[wordCount];
  BlockStyle blockStyle;
};

struct PageElement {
    u8 pageElementType;
    if (pageElementType == 1) {
        PageLine pageLine [[inline]];
    } else {
        std::error(std::format("Unknown page element type: {}", pageElementType));
    }
};

struct Page {
    u16 elementCount;
    PageElement elements[elementCount] [[inline]];
};

// === Section Bin Structure ===

struct SectionBin {
    // Header
    u8 version [[comment("Format version"), color("FFD93D")]];
    
    // Version validation
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }
    
    // Cache busting parameters
    s32 fontId;
    float lineCompression;
    bool extraParagraphSpacing;
    u16 viewportWidth;
    u16 vieportHeight;
    u16 pageCount;
    u32 lutOffset;
    
    Page page[pageCount];
    
    // Validate LUT offset alignment
    u32 currentOffset = $;
    if (currentOffset != lutOffset) {
        std::warning(std::format("LUT offset mismatch: expected 0x{:X}, got 0x{:X}", lutOffset, currentOffset));
    }
    
    // Lookup Tables
    u32 lut[pageCount];
};

// === File Parsing ===

SectionBin book @ 0x00;

// Validate we've consumed the entire file
u32 fileSize = std::mem::size();
u32 parsedSize = $;

if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## `md_<hash>/index.bin` (fork-only)

Per-document Markdown page index produced by `MarkdownReaderActivity`.
Stores the file offset where each rendered page begins, so reopening a
Markdown document is instant after the first parse pass.

The cache directory is `.crosspoint/md_<hash>/` where `<hash>` is
`std::hash<std::string>{}(absoluteFilePath)`. Renaming or moving the
file produces a new hash and a fresh cache, matching the EPUB and TXT
readers' behaviour.

### Version 1

| Field | Type | Notes |
| --- | --- | --- |
| `magic` | `uint32_t` | `0x4D444B49` ("MDKI") |
| `version` | `uint8_t` | `1` |
| `fileSize` | `uint32_t` | Source `.md`/`.markdown` file size; mismatch invalidates the cache |
| `viewportWidth` | `int32_t` | Pixels; invalidated on orientation/margin change |
| `viewportHeight` | `int32_t` | Pixels |
| `fontId` | `int32_t` | `SETTINGS.getReaderFontId()` value at index time |
| `screenMargin` | `int32_t` | `SETTINGS.screenMargin` at index time |
| `numPages` | `uint32_t` | Number of `pageOffset` entries that follow |
| `pageOffsets[N]` | `uint32_t` * N | Byte offset into the source file where each page begins |

Pages break only at block boundaries in v1. A single block taller than
the viewport is rendered to one page and clipped; this is documented as
deferred work in `SCOPE.md` ("Markdown features beyond v1").

### Companion `progress.bin`

Same 4-byte format as the TXT reader (little-endian `uint32_t` page
index). Written on every `render()` call but only when the page actually
changed (debounced via the existing renderer flow).
