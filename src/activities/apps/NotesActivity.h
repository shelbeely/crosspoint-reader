#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// NotesActivity — plaintext note manager.
//
// Notes are stored as .txt files under /.crosspoint/notes/ on the SD card.
// The filename (without extension) is the note title.
//
// Views:
//   LIST   — scrollable list of note filenames.  Up/Down to select.
//            Confirm opens VIEW; hold Confirm prompts for a new note title.
//   VIEW   — shows the note content, paginated.
//            Hold Confirm appends a new line (via KeyboardEntryActivity).
//            Back returns to LIST (file changes written during append).

class NotesActivity final : public Activity {
 public:
  explicit NotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Notes", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr const char* kNotesDir = "/.crosspoint/notes";
  static constexpr int MAX_NOTES = 64;
  static constexpr int MAX_LINES_PER_PAGE = 12;

  struct NoteEntry {
    char filename[64];   // filename without extension, used as title
    char path[96];       // full path including extension
  };

  NoteEntry noteList[MAX_NOTES];
  int noteCount = 0;
  int listSelector = 0;

  enum View { LIST, VIEW };
  View view = LIST;

  // VIEW state
  int currentNoteIdx = -1;
  std::vector<std::string> noteLines;
  int viewPage = 0;

  // Hold-confirm detection
  bool confirmHeld = false;
  bool confirmLongHandled = false;
  static constexpr uint16_t HOLD_MS = 600;
  unsigned long confirmPressMs = 0;

  void loadNoteList();
  void loadNoteContent(int idx);
  void appendLineToNote(const std::string& line);

  void renderList();
  void renderView();

  void loopList();
  void loopView();

  void startCreateNote();
  void startAppendLine();
};
