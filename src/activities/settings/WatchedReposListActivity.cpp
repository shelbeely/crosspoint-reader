#include "WatchedReposListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "WatchedReposStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

int WatchedReposListActivity::getItemCount() const {
  // Always append the virtual "Add Repository" item, even at the cap, so
  // the user gets a discoverable explanation when they try to add more.
  return static_cast<int>(WATCHED_REPOS.getCount()) + 1;
}

bool WatchedReposListActivity::isAddItemSelected() const {
  return selectedIndex == static_cast<int>(WATCHED_REPOS.getCount());
}

void WatchedReposListActivity::onEnter() {
  Activity::onEnter();

  WATCHED_REPOS.loadFromFile();
  selectedIndex = 0;
  mode = Mode::LIST;
  showAddError = false;
  requestUpdate();
}

void WatchedReposListActivity::onExit() { Activity::onExit(); }

void WatchedReposListActivity::loop() {
  if (mode == Mode::CONFIRM_REMOVE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      const auto count = WATCHED_REPOS.getCount();
      if (selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < count) {
        if (!WATCHED_REPOS.removeRepo(static_cast<size_t>(selectedIndex))) {
          LOG_ERR("WRL", "removeRepo(%d) failed", selectedIndex);
        } else {
          LOG_DBG("WRL", "Removed repo at index %d", selectedIndex);
        }
        const int newCount = static_cast<int>(WATCHED_REPOS.getCount());
        if (selectedIndex >= newCount) {
          selectedIndex = newCount;  // moves to "Add Repository" if list shrank
        }
      }
      mode = Mode::LIST;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      mode = Mode::LIST;
      requestUpdate();
      return;
    }
    return;
  }

  if (showAddError) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      showAddError = false;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int itemCount = getItemCount();
  buttonNavigator.onNext([this, itemCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, itemCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    requestUpdate();
  });
}

void WatchedReposListActivity::handleSelection() {
  if (isAddItemSelected()) {
    promptAddRepo();
  } else {
    confirmRemoveSelected();
  }
}

void WatchedReposListActivity::promptAddRepo() {
  auto handler = [this](const ActivityResult& result) {
    // Reload after the keyboard returns so we're in sync with disk state.
    WATCHED_REPOS.loadFromFile();
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    const auto& kb = std::get<KeyboardResult>(result.data);
    if (kb.text.empty()) {
      requestUpdate();
      return;
    }
    if (!WATCHED_REPOS.addFromSlug(kb.text)) {
      // Do not log the user-entered slug; surface a generic UI error.
      LOG_ERR("WRL", "addFromSlug rejected entry");
      showAddError = true;
    } else {
      WATCHED_REPOS.saveToFile();
      selectedIndex = static_cast<int>(WATCHED_REPOS.getCount()) - 1;
      if (selectedIndex < 0) selectedIndex = 0;
    }
    requestUpdate();
  };

  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ADD_REPO), "", 80, InputType::Text),
      handler);
}

void WatchedReposListActivity::confirmRemoveSelected() {
  mode = Mode::CONFIRM_REMOVE;
  requestUpdate();
}

void WatchedReposListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WATCHED_REPOS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const auto& repos = WATCHED_REPOS.getRepos();
  const int repoCount = static_cast<int>(repos.size());
  const int itemCount = repoCount + 1;

  if (repoCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_NO_WATCHED_REPOS));
  }

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex,
      [&repos, repoCount](int index) {
        if (index < repoCount) {
          return repos[index].owner + "/" + repos[index].repo;
        }
        return std::string(I18n::getInstance().get(StrId::STR_ADD_REPO));
      },
      [](int) { return std::string(""); });

  if (mode == Mode::CONFIRM_REMOVE) {
    GUI.drawPopup(renderer, tr(STR_REMOVE_REPO_CONFIRM));
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_DELETE), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (showAddError) {
    GUI.drawPopup(renderer, tr(STR_ADD_REPO_FAILED));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
