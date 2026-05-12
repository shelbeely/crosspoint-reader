#pragma once

#include <cstdint>
#include <memory>

#include <VCard.h>
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// ContactsActivity — contact list reader backed by /contacts.vcf on the SD card.
//
// Index cache: /.crosspoint/contacts.bin  (magic VCFX, version 1)
// Source file: /contacts.vcf
//
// Views:
//   LIST   — scrollable list of name + phone pairs.  Up/Down to navigate;
//            Confirm opens DETAIL; hold Confirm to open SEARCH.
//   DETAIL — shows all fields for the selected contact, read from SD on demand.
//   SEARCH — filtered subset of the list matching a user-entered string.

class ContactsActivity final : public Activity {
 public:
  explicit ContactsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Contacts", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr const char* kVcfPath = "/contacts.vcf";
  static constexpr const char* kCachePath = "/.crosspoint/contacts.bin";

  // Index loaded from cache (or rebuilt from .vcf on miss)
  VCardIndex* index = nullptr;    // heap-allocated array
  int indexCount = 0;

  // Filtered view (nullptr = show all)
  int* filteredIndices = nullptr;  // heap-allocated, indices into index[]
  int filteredCount = 0;
  bool searchActive = false;
  char searchQuery[32] = {};

  // Detail view — loaded on demand
  VCardRecord detail{};
  bool detailLoaded = false;
  int detailIndexEntry = -1;  // which index[] entry is shown in detail

  enum View { LIST, DETAIL };
  View view = LIST;

  int listSelector = 0;  // position in filtered or full list

  // Hold-confirm
  bool confirmHeld = false;
  bool confirmLongHandled = false;
  static constexpr uint16_t HOLD_MS = 600;
  unsigned long confirmPressMs = 0;

  // Helpers
  bool loadCacheOrRebuild();
  bool loadCache();
  void buildCache();
  void loadDetail(int indexEntry);

  void applyFilter(const char* query);
  void clearFilter();

  int getDisplayCount() const;
  int getDisplayIndex(int pos) const;

  void renderList();
  void renderDetail();

  void loopList();
  void loopDetail();

  void startSearch();
};
