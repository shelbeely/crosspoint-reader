# Project Vision & Scope (Fork)

> **Note on this fork.** This is a fork of [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)
> that intentionally diverges from the upstream "focused reader, nothing else" mission. Issues and pull requests against
> upstream should follow the upstream `SCOPE.md`. This document only governs **this fork**.

The goal of this fork is to make the Xteink X4 a **dual-mode device**: a focused e-reader **and** an ambient
GitHub companion for programmers who already work with GitHub Copilot. Both modes share the same hardware, the same
rendering engine, and the same hard resource budget; neither mode is allowed to compromise the other.

## 1. Core Mission

Provide a lightweight, high-performance firmware for the Xteink X4 (ESP32-C3) that does two things well:

1. **Reading.** EPUB, plain text, and now Markdown documents — laid out for an e-ink display, optimised for legibility
   and battery life. This remains the default mode and works with no network configured.
2. **GitHub companion.** An ambient, glanceable view of GitHub Copilot work, reviews, CI, and the issue queue, plus
   a small explicit set of write actions. Always poll-based and on-demand — never a background streaming client.

If the two modes ever come into conflict, **reading wins**. The companion features are additive and must degrade
gracefully (or stay dormant) when memory, battery, or connectivity is constrained.

## 2. Scope

### In-Scope — Reader

*These features improve the primary reading experience.*

* **User Experience:** User-friendly interfaces and interactions, both inside the reader and navigating the firmware.
  Includes button mapping, book loading, and book navigation like bookmarks.
* **Document Rendering:** Support for rendering documents (primarily EPUB) and improvements to the rendering engine.
* **Markdown Rendering:** First-class support for `.md` / `.markdown` documents loaded from the SD card, lowering to
  the same layout primitives as EPUB/text. (Required by the companion features below — issue and PR bodies are
  Markdown — but valuable on its own for reading documentation on the device.)
* **Format Optimization:** Efficient parsing of EPUB (CSS/Images) and other documents within the device's capabilities.
* **Typography & Legibility:** Custom font support, hyphenation engines, and adjustable line spacing.
* **E-Ink Driver Refinement:** Reducing full-screen flashes (ghosting management) and improving general rendering.
* **Library Management:** Simple, intuitive ways to organize and navigate a collection of books.
* **Local Transfer:** Simple, "pull" based book loading via a basic web-server or public and widely-used standards.
* **Language Support:** Multiple languages both in the reader and in the interfaces.
* **Reference Tools:** Local dictionary lookup, providing quick offline definitions to enhance comprehension.

### In-Scope — GitHub Companion

*These features are explicitly added by this fork. They were Out-of-Scope upstream.*

This is the **bounded** set of GitHub functionality that is in scope. Anything not listed here remains out-of-scope
unless and until this document is updated.

**Read-only dashboards** (each fetched on demand from a small explicit watchlist of repositories):

* **Assigned tasks** — issues assigned to the user (covers issues assigned to the GitHub Copilot bot when configured).
* **Active Copilot sessions / PRs** — open PRs authored by the user, filterable by Copilot authorship.
* **Needs review** — PRs the user is requested to review.
* **GitHub Actions status** — recent workflow runs per watched repo.
* **Failed CI summaries** — for failed runs, the first error line per failed job, fetched via Range requests on the
  log endpoints. Full-log viewing is out-of-scope.
* **Issue queue** — open issues across the watched repositories.

**Write actions** (each guarded by an explicit on-screen confirmation dialog):

* **Assign issue to Copilot.**
* **Rerun failed workflow jobs.**
* **Dismiss card** (local-only — hides the item from the device until its underlying state changes).
* **Request PR summary** (posts a configured trigger comment, e.g. `@copilot summary`).

**Connectivity model:**

* A single user PAT, stored in SPIFFS, never logged.
* Wi-Fi is brought up for the request and torn down after — same on-demand pattern as the existing OPDS / WebDAV code.
* Polling is manual or coarse-timer (default off). No background streaming, no websockets, no push.
* Repos to watch are an explicit user-configured list; there is no "scan all my repos" mode.

**Resource budget for the entire GitHub feature set:**

The companion mode (client + active activity + cached state) is held to a hard cap of **≈60 KB peak heap** above the
reader's existing baseline. If a feature cannot fit, it does not ship — even if it is in the in-scope list above. The
reader's existing free-heap floor (≥50 KB) is non-negotiable and applies in companion mode too.

### Out-of-Scope

*These items remain rejected because they would compromise the device's stability or its dual mission.*

* **General web browsing.** No browser, no embedded HTML rendering for arbitrary URLs.
* **Chat / conversational UI.** No Copilot Chat, no LLM completions on-device. The device displays state and triggers
  actions; it is not an IDE.
* **Inline code editing.** No code authoring on the device.
* **Background streaming or push.** No always-on socket, no webhook receiver. Polling only, on demand or on a coarse
  user-configured timer.
* **Repository scanning.** No "fetch all repos visible to my token" mode — the watchlist is the only input to CI /
  issues / actions queries.
* **Notepads / Calculators / Games.** Reader and companion are the device's only modes.
* **Media Playback.** No audio players or audiobooks.
* **Complex reader annotation.** No typed-out notes — the input hardware is not suited to it.

### In-Scope — Technically Unsupported

*Aligned with this fork's goals but impractical on the current hardware.*

* **PDF Rendering.** Fixed-layout, requires panning/zooming on e-ink — poor reading experience.
* **Markdown features beyond v1.** Tables and arbitrary HTML passthrough are deferred. Inline images are rendered as
  alt-text stubs.

## 3. Idea Evaluation

For reader features, the upstream guideline still applies: does it improve the core reading experience without
distracting from it?

For companion features, the additional question is: does it fit within the **read-only-plus-confirmed-write-actions**
model, the **on-demand polling** model, and the **60 KB peak heap** cap? If any of those would have to flex, the
feature is out-of-scope or needs a discussion before any code.

> **Note to Contributors:** If you are unsure whether your idea fits the scope, please open a Discussion *before* you
> start coding. Reader-side ideas should also fit the upstream scope unless this document explicitly diverges.
