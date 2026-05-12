#pragma once
#include "activities/Activity.h"

/**
 * MacRandomizerActivity
 *
 * Lets the user randomize or restore the WiFi station MAC address.
 * The radio is shut down before any MAC change and restarted to WiFi STA
 * afterwards so the new MAC takes effect.
 *
 * Layout (single-page info):
 *   Header: "MAC Randomizer"
 *   Current MAC (full)
 *   [R] Randomize  /  [W] Restore factory
 *   Status line (last action result)
 */
class MacRandomizerActivity final : public Activity {
 public:
  explicit MacRandomizerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("MacRandomizer", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  char currentMacStr[18] = "";   // "aa:bb:cc:dd:ee:ff"
  char statusMsg[48] = "";       // feedback after last action
  bool isRandomized = false;

  void refreshMac();
  void doRandomize();
  void doRestore();
};
