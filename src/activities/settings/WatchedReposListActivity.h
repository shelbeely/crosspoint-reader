#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Settings activity that lets the user view, add, and remove the
 * `owner/repo` slugs tracked by `WATCHED_REPOS`. The list is the only
 * source the GitHub Companion uses to decide which repos to poll.
 *
 * Mirrors `OpdsServerListActivity`: a list of real entries followed by a
 * virtual "Add Repository" item, with an in-activity confirmation state
 * (modelled on `ClearCacheActivity`'s WARNING state) for removals.
 */
class WatchedReposListActivity final : public Activity {
 public:
  explicit WatchedReposListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("WatchedReposList", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode { LIST, CONFIRM_REMOVE };

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  Mode mode = Mode::LIST;
  bool showAddError = false;

  int getItemCount() const;
  bool isAddItemSelected() const;
  void handleSelection();
  void promptAddRepo();
  void confirmRemoveSelected();
};
