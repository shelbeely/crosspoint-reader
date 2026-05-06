# Sample README

This is a representative Markdown file used for **manual on-device verification**
of the `MarkdownReaderActivity` introduced in this fork.

## What it exercises

- Multiple heading levels (`#` through `####`)
- Paragraphs that are long enough to wrap across several lines on the
  800×480 e-ink display, exercising the word-wrap path through every
  inline style.
- Inline `code` spans, **bold**, and *italic*.
- A [link to the upstream project](https://github.com/crosspoint-reader/crosspoint-reader)
  that should render with an underline.

## Lists

### Bullets

- one
- two
- three with *italic* and **bold** mixed in
  - nested item to verify indent

### Numbered

1. first
2. second
10. tenth — verify that two-digit numbers do not break the layout

## Quotes and rules

> A blockquote line. The renderer should give it a left indent so it is
> visibly distinct from the surrounding prose.

---

## Code

A fenced block:

```
int main() {
  printf("hello\n");
  return 0;
}
```

A standalone image stub:

![a diagram](nothing.png)

End of sample.
