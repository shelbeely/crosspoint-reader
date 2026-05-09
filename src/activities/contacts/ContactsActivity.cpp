#include "ContactsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <VCard.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ContactsActivity::onEnter() {
  Activity::onEnter();

  screen = Screen::List;
  selectorIndex = 0;

  const bool ok = parser.ensureIndex();
  contactCount = ok ? static_cast<int>(parser.getContactCount()) : 0;

  requestUpdate();
}

void ContactsActivity::onExit() { Activity::onExit(); }

// ---------------------------------------------------------------------------
// Detail loading
// ---------------------------------------------------------------------------

void ContactsActivity::loadDetail(uint16_t index) {
  VCardIndexEntry entry = {};
  if (!parser.getIndexEntry(index, entry)) {
    LOG_ERR("CON", "Failed to read index entry %u", index);
    return;
  }
  detailOffset = entry.offset;
  if (!parser.loadContact(VCARD_DEFAULT_PATH, detailOffset, currentContact)) {
    // Fallback: at least show the name from the index
    memset(&currentContact, 0, sizeof(currentContact));
    strncpy(currentContact.name, entry.name, sizeof(currentContact.name) - 1);
    LOG_ERR("CON", "Failed to stream contact detail for index %u", index);
  }
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void ContactsActivity::loop() {
  if (screen == Screen::List) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && contactCount > 0) {
      loadDetail(static_cast<uint16_t>(selectorIndex));
      screen = Screen::Detail;
      requestUpdate();
      return;
    }

    const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);

    buttonNavigator.onNextRelease([this] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, contactCount);
      requestUpdate();
    });

    buttonNavigator.onPreviousRelease([this] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, contactCount);
      requestUpdate();
    });

    buttonNavigator.onNextContinuous([this, pageItems] {
      selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, contactCount, pageItems);
      requestUpdate();
    });

    buttonNavigator.onPreviousContinuous([this, pageItems] {
      selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, contactCount, pageItems);
      requestUpdate();
    });

  } else {
    // Detail screen: Back returns to list
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      screen = Screen::List;
      requestUpdate();
    }
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void ContactsActivity::render(RenderLock&& lock) {
  if (screen == Screen::List) {
    renderList();
  } else {
    renderDetail();
  }
}

void ContactsActivity::renderList() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CONTACTS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (contactCount == 0) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_CONTACTS));
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, contentTop + 50, tr(STR_CONTACTS_HINT));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, contactCount, selectorIndex,
        [this](int index) {
          VCardIndexEntry entry = {};
          if (parser.getIndexEntry(static_cast<uint16_t>(index), entry)) {
            return std::string(entry.name);
          }
          return std::string(tr(STR_UNNAMED));
        },
        nullptr, nullptr);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), contactCount > 0 ? tr(STR_OPEN) : "",
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void ContactsActivity::renderDetail() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CONTACT_DETAIL));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 10;
  const int x = metrics.contentSidePadding;

  // Name (larger font)
  if (currentContact.name[0]) {
    renderer.drawText(UI_12_FONT_ID, x, y, currentContact.name, true, EpdFontFamily::BOLD);
    y += 32;
  }

  // Org
  if (currentContact.org[0]) {
    renderer.drawText(UI_10_FONT_ID, x, y, currentContact.org);
    y += 24;
  }

  y += 8;  // small spacer

  // Phone
  if (currentContact.phone[0]) {
    char buf[96];
    snprintf(buf, sizeof(buf), "TEL: %s", currentContact.phone);
    renderer.drawText(SMALL_FONT_ID, x, y, buf);
    y += 22;
  }

  // Email
  if (currentContact.email[0]) {
    char buf[128];
    snprintf(buf, sizeof(buf), "EMAIL: %s", currentContact.email);
    renderer.drawText(SMALL_FONT_ID, x, y, buf);
    y += 22;
  }

  // Note
  if (currentContact.note[0]) {
    y += 8;
    renderer.drawText(SMALL_FONT_ID, x, y, currentContact.note);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
