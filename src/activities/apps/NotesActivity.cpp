#include "NotesActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <cstring>
#include <algorithm>
#include <variant>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void NotesActivity::onEnter() {
  Activity::onEnter();
  Storage.ensureDirectoryExists(kNotesDir);
  view = LIST;
  listSelector = 0;
  loadNoteList();
  requestUpdate();
}

void NotesActivity::onExit() {
  noteLines.clear();
  Activity::onExit();
}

void NotesActivity::loop() {
  switch (view) {
    case LIST: loopList(); break;
    case VIEW: loopView(); break;
  }
}

void NotesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (view) {
    case LIST: renderList(); break;
    case VIEW: renderView(); break;
  }
  renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------

void NotesActivity::loadNoteList() {
  noteCount = 0;
  auto files = Storage.listFiles(kNotesDir, MAX_NOTES);
  for (const auto& f : files) {
    if (noteCount >= MAX_NOTES) break;
    // Only .txt files
    int len = f.length();
    if (len < 5) continue;
    if (f.substring(len - 4) != ".txt") continue;
    auto& entry = noteList[noteCount];
    snprintf(entry.path, sizeof(entry.path), "%s/%s", kNotesDir, f.c_str());
    // Title = filename without .txt
    int titleLen = len - 4;
    if (titleLen >= (int)sizeof(entry.filename)) titleLen = (int)sizeof(entry.filename) - 1;
    strncpy(entry.filename, f.c_str(), titleLen);
    entry.filename[titleLen] = '\0';
    noteCount++;
  }
}

void NotesActivity::loadNoteContent(int idx) {
  noteLines.clear();
  viewPage = 0;
  if (idx < 0 || idx >= noteCount) return;

  FsFile file;
  if (!Storage.openFileForRead("NON", noteList[idx].path, file)) return;

  char buf[256];
  std::string line;
  line.reserve(128);
  uint8_t ch;
  while (file.read(&ch, 1) == 1) {
    if (ch == '\n') {
      noteLines.push_back(line);
      line.clear();
    } else if (ch != '\r') {
      line += (char)ch;
    }
  }
  if (!line.empty()) noteLines.push_back(line);
  file.close();
}

void NotesActivity::appendLineToNote(const std::string& line) {
  if (currentNoteIdx < 0 || currentNoteIdx >= noteCount) return;

  // Append to SD file
  FsFile file = Storage.open(noteList[currentNoteIdx].path, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("NON", "Cannot open note for append: %s", noteList[currentNoteIdx].path);
    return;
  }
  file.write((const uint8_t*)line.c_str(), line.size());
  file.write((const uint8_t*)"\n", 1);
  file.close();

  // Update in-memory lines
  noteLines.push_back(line);
  // Scroll to last page
  if ((int)noteLines.size() > MAX_LINES_PER_PAGE) {
    viewPage = ((int)noteLines.size() - 1) / MAX_LINES_PER_PAGE;
  }
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

void NotesActivity::loopList() {
  // Hold-confirm → create new note
  if (mappedInput.isHeld(MappedInputManager::Button::Confirm)) {
    if (!confirmHeld) {
      confirmHeld = true;
      confirmPressMs = millis();
    } else if (!confirmLongHandled && millis() - confirmPressMs >= HOLD_MS) {
      confirmLongHandled = true;
      startCreateNote();
      return;
    }
  } else {
    if (confirmHeld && !confirmLongHandled && noteCount > 0) {
      // Tap confirm → open note
      currentNoteIdx = listSelector;
      loadNoteContent(currentNoteIdx);
      view = VIEW;
      requestUpdate();
    }
    confirmHeld = false;
    confirmLongHandled = false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) && listSelector < noteCount - 1) {
    listSelector++;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) && listSelector > 0) {
    listSelector--;
    requestUpdate();
  }
}

void NotesActivity::loopView() {
  const int totalPages = noteLines.empty() ? 1 : ((int)noteLines.size() + MAX_LINES_PER_PAGE - 1) / MAX_LINES_PER_PAGE;

  // Hold-confirm → append line
  if (mappedInput.isHeld(MappedInputManager::Button::Confirm)) {
    if (!confirmHeld) {
      confirmHeld = true;
      confirmPressMs = millis();
    } else if (!confirmLongHandled && millis() - confirmPressMs >= HOLD_MS) {
      confirmLongHandled = true;
      startAppendLine();
      return;
    }
  } else {
    confirmHeld = false;
    confirmLongHandled = false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    view = LIST;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right) && viewPage < totalPages - 1) {
    viewPage++;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) && viewPage > 0) {
    viewPage--;
    requestUpdate();
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void NotesActivity::renderList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, tr(STR_NOTES));

  if (noteCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, metrics.topPadding + metrics.headerHeight + 40, tr(STR_NO_NOTES));
  } else {
    int y = metrics.topPadding + metrics.headerHeight + 4;
    const int itemH = metrics.listRowHeight;
    const int visCount = (pageH - y - metrics.buttonHintsHeight) / itemH;
    int first = listSelector - visCount / 2;
    if (first < 0) first = 0;
    if (first + visCount > noteCount) first = std::max(0, noteCount - visCount);
    for (int i = first; i < first + visCount && i < noteCount; i++) {
      bool sel = (i == listSelector);
      if (sel) renderer.fillRect(0, y, pageW, itemH, true);
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y + 6, noteList[i].filename, !sel);
      y += itemH;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), "^", "v");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void NotesActivity::renderView() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();

  const char* title = (currentNoteIdx >= 0 && currentNoteIdx < noteCount)
                          ? noteList[currentNoteIdx].filename
                          : tr(STR_NOTES);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, title);

  int y = metrics.topPadding + metrics.headerHeight + 8;
  const int lh = renderer.getLineHeight(SMALL_FONT_ID) + 2;
  const int first = viewPage * MAX_LINES_PER_PAGE;
  const int last = std::min(first + MAX_LINES_PER_PAGE, (int)noteLines.size());

  if (noteLines.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, y + 20, tr(STR_NO_NOTES));
  } else {
    for (int i = first; i < last; i++) {
      renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y, noteLines[i].c_str());
      y += lh;
    }
  }

  const int totalPages = noteLines.empty() ? 1 : ((int)noteLines.size() + MAX_LINES_PER_PAGE - 1) / MAX_LINES_PER_PAGE;
  if (totalPages > 1) {
    char pg[16];
    snprintf(pg, sizeof(pg), "%d/%d", viewPage + 1, totalPages);
    renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() - metrics.buttonHintsHeight - 14, pg);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_APPEND_LINE), "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------------------------------------------------------------------------
// Create / Append flows
// ---------------------------------------------------------------------------

void NotesActivity::startCreateNote() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_NOTE_TITLE), "", 59),
      [this](const ActivityResult& r) {
        if (r.isCancelled || !std::holds_alternative<KeyboardResult>(r.data)) {
          requestUpdate();
          return;
        }
        const std::string& title = std::get<KeyboardResult>(r.data).text;
        if (title.empty()) { requestUpdate(); return; }

        // Create the file
        char path[96];
        snprintf(path, sizeof(path), "%s/%s.txt", kNotesDir, title.c_str());
        FsFile file = Storage.open(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (file) file.close();

        loadNoteList();
        // Select the new note
        for (int i = 0; i < noteCount; i++) {
          if (strncmp(noteList[i].filename, title.c_str(), title.length() + 1) == 0) {
            listSelector = i;
            break;
          }
        }
        requestUpdate();
      });
}

void NotesActivity::startAppendLine() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_APPEND_LINE), "", 127),
      [this](const ActivityResult& r) {
        if (!r.isCancelled && std::holds_alternative<KeyboardResult>(r.data)) {
          const std::string& line = std::get<KeyboardResult>(r.data).text;
          if (!line.empty()) appendLineToNote(line);
        }
        requestUpdate();
      });
}
