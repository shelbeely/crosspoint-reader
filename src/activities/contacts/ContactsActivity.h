#pragma once

#include <VCard.h>

#include <string>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class ContactsActivity final : public Activity {
 public:
  explicit ContactsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Contacts", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Screen { List, Detail };

  Screen screen = Screen::List;
  ButtonNavigator buttonNavigator;

  // List state
  VCardParser parser;
  int selectorIndex = 0;
  int contactCount = 0;

  // Detail state — populated when user opens a contact
  VCardContact currentContact = {};
  uint32_t detailOffset = 0;

  void loadDetail(uint16_t index);
  void renderList();
  void renderDetail();
};
