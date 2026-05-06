# Code Heavy

Document that stresses the fenced-code-block path. Each fenced block is
parsed as one `BlockKind::CodeBlock` per source line and is rendered with
italic styling and a fixed left indent.

## A short example

```
gpio_set_level(BUTTON_PIN, 1);
```

## A longer example

The block below should not be parsed as headings or bullets even though
several lines start with characters that would normally trigger block
markers.

```
# This is a comment, not a heading.
- This is a list-style comment in code.
> not a quote either
```

## Inline code in prose

When the user calls `vTaskDelay(1)` from inside the activity loop, the
renderer prewarm pass runs first and then the actual draw pass executes.
Constants like `CHUNK_SIZE` and `CACHE_VERSION` live in the anonymous
namespace at the top of `MarkdownReaderActivity.cpp`.

## Edge cases

```
```

(An empty code block.)

```
single line
```
