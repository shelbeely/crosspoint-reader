#include "ContactsActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>

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

void ContactsActivity::onEnter() {
  Activity::onEnter();

  // Allocate index heap buffer
  index = new VCardIndex[VCard::MAX_CONTACTS];
  indexCount = 0;
  filteredIndices = new int[VCard::MAX_CONTACTS];
  filteredCount = 0;
  searchActive = false;
  searchQuery[0] = '\0';
  view = LIST;
  listSelector = 0;

  if (!loadCacheOrRebuild()) {
    LOG_ERR("CON", "Failed to load/build contacts index");
  }
  requestUpdate();
}

void ContactsActivity::onExit() {
  delete[] index;
  index = nullptr;
  delete[] filteredIndices;
  filteredIndices = nullptr;
  Activity::onExit();
}

void ContactsActivity::loop() {
  switch (view) {
    case LIST:   loopList();   break;
    case DETAIL: loopDetail(); break;
  }
}

void ContactsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (view) {
    case LIST:   renderList();   break;
    case DETAIL: renderDetail(); break;
  }
  renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
// Index loading / building
// ---------------------------------------------------------------------------

bool ContactsActivity::loadCacheOrRebuild() {
  if (loadCache()) return true;

  // No valid cache — parse .vcf and write cache
  if (!Storage.exists(kVcfPath)) {
    LOG_INF("CON", "No contacts.vcf found at %s", kVcfPath);
    return false;
  }
  buildCache();
  return loadCache();
}

bool ContactsActivity::loadCache() {
  FsFile file;
  if (!Storage.openFileForRead("CON", kCachePath, file)) return false;

  uint32_t magic = 0;
  uint8_t version = 0;
  uint32_t count = 0;

  serialization::readPod(file, magic);
  if (magic != VCard::CACHE_MAGIC) { file.close(); return false; }
  serialization::readPod(file, version);
  if (version != VCard::CACHE_VERSION) { file.close(); return false; }
  serialization::readPod(file, count);
  if (count > (uint32_t)VCard::MAX_CONTACTS) { file.close(); return false; }

  indexCount = 0;
  for (uint32_t i = 0; i < count; i++) {
    if (file.read((uint8_t*)&index[i], sizeof(VCardIndex)) != (int)sizeof(VCardIndex)) {
      break;
    }
    indexCount++;
  }
  file.close();
  LOG_INF("CON", "Loaded %d contacts from cache", indexCount);
  return true;
}

namespace {

struct BuildCtx {
  VCardIndex* index;
  int* count;
};

void buildSink(void* ctx, const VCardRecord& rec, uint32_t offset) {
  auto* c = static_cast<BuildCtx*>(ctx);
  if (*c->count >= VCard::MAX_CONTACTS) return;
  auto& idx = c->index[*c->count];
  idx.vcfOffset = offset;
  snprintf(idx.name, sizeof(idx.name), "%s", rec.fn);
  snprintf(idx.phone, sizeof(idx.phone), "%s", rec.tel);
  (*c->count)++;
}

}  // namespace

void ContactsActivity::buildCache() {
  FsFile vcf;
  if (!Storage.openFileForRead("CON", kVcfPath, vcf)) {
    LOG_ERR("CON", "Cannot open contacts.vcf for parsing");
    return;
  }

  BuildCtx ctx{index, &indexCount};
  indexCount = 0;

  VCard parser;
  parser.reset(buildSink, &ctx);

  char lineBuf[256];
  uint32_t fileOffset = 0;
  uint32_t lineStart = 0;
  int lineLen = 0;

  uint8_t ch;
  while (vcf.read(&ch, 1) == 1) {
    if (ch == '\n') {
      lineBuf[lineLen] = '\0';
      parser.feedLine(lineBuf, (size_t)lineLen, lineStart);
      lineStart = fileOffset + 1;
      lineLen = 0;
    } else if (lineLen < (int)sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = (char)ch;
    }
    fileOffset++;
  }
  // Handle file without final newline
  if (lineLen > 0) {
    lineBuf[lineLen] = '\0';
    parser.feedLine(lineBuf, (size_t)lineLen, lineStart);
  }
  vcf.close();
  LOG_INF("CON", "Parsed %d contacts from vcf", indexCount);

  // Write cache
  Storage.ensureDirectoryExists("/.crosspoint");
  FsFile cacheFile;
  if (!Storage.openFileForWrite("CON", kCachePath, cacheFile)) {
    LOG_ERR("CON", "Cannot write contacts cache");
    return;
  }
  uint32_t magic = VCard::CACHE_MAGIC;
  uint8_t ver = VCard::CACHE_VERSION;
  uint32_t cnt = (uint32_t)indexCount;
  serialization::writePod(cacheFile, magic);
  serialization::writePod(cacheFile, ver);
  serialization::writePod(cacheFile, cnt);
  for (int i = 0; i < indexCount; i++) {
    cacheFile.write((const uint8_t*)&index[i], sizeof(VCardIndex));
  }
  cacheFile.close();
  LOG_INF("CON", "Wrote contacts cache (%d entries)", indexCount);
}

// ---------------------------------------------------------------------------
// Detail loading (on-demand from .vcf)
// ---------------------------------------------------------------------------

void ContactsActivity::loadDetail(int indexEntry) {
  if (indexEntry < 0 || indexEntry >= indexCount) return;
  if (detailIndexEntry == indexEntry && detailLoaded) return;

  detailLoaded = false;
  memset(&detail, 0, sizeof(detail));
  detailIndexEntry = indexEntry;

  FsFile vcf;
  if (!Storage.openFileForRead("CON", kVcfPath, vcf)) return;

  vcf.seek(index[indexEntry].vcfOffset);

  VCard parser;
  struct OneRecord {
    VCardRecord* dst;
    bool done;
  } ctx{&detail, false};

  parser.reset([](void* c, const VCardRecord& rec, uint32_t) {
    auto* oc = static_cast<OneRecord*>(c);
    if (!oc->done) { *oc->dst = rec; oc->done = true; }
  }, &ctx);

  char lineBuf[256];
  int lineLen = 0;
  uint8_t ch;
  while (vcf.read(&ch, 1) == 1) {
    if (ch == '\n') {
      lineBuf[lineLen] = '\0';
      parser.feedLine(lineBuf, (size_t)lineLen);
      lineLen = 0;
      if (ctx.done) break;  // stop after first VCARD
    } else if (lineLen < (int)sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = (char)ch;
    }
  }
  vcf.close();
  detailLoaded = ctx.done;
}

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------

void ContactsActivity::applyFilter(const char* query) {
  snprintf(searchQuery, sizeof(searchQuery), "%s", query);
  filteredCount = 0;
  searchActive = (query[0] != '\0');

  if (!searchActive) return;

  // Case-insensitive substring match on name or phone
  for (int i = 0; i < indexCount && filteredCount < VCard::MAX_CONTACTS; i++) {
    // Lower-case comparison — we manually check char-by-char
    auto ciStrStr = [](const char* hay, const char* needle) -> bool {
      if (!needle[0]) return true;
      for (const char* h = hay; *h; h++) {
        const char* n = needle;
        const char* hh = h;
        while (*n && *hh && ((*hh | 32) == (*n | 32))) { hh++; n++; }
        if (!*n) return true;
      }
      return false;
    };
    if (ciStrStr(index[i].name, query) || ciStrStr(index[i].phone, query)) {
      filteredIndices[filteredCount++] = i;
    }
  }
  listSelector = 0;
}

void ContactsActivity::clearFilter() {
  searchActive = false;
  searchQuery[0] = '\0';
  filteredCount = 0;
  listSelector = 0;
}

int ContactsActivity::getDisplayCount() const {
  return searchActive ? filteredCount : indexCount;
}

int ContactsActivity::getDisplayIndex(int pos) const {
  return searchActive ? filteredIndices[pos] : pos;
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

void ContactsActivity::loopList() {
  const int total = getDisplayCount();

  // Hold-confirm → search
  if (mappedInput.isHeld(MappedInputManager::Button::Confirm)) {
    if (!confirmHeld) {
      confirmHeld = true;
      confirmPressMs = millis();
    } else if (!confirmLongHandled && millis() - confirmPressMs >= HOLD_MS) {
      confirmLongHandled = true;
      startSearch();
      return;
    }
  } else {
    if (confirmHeld && !confirmLongHandled && total > 0) {
      // Tap confirm → open detail
      int idx = getDisplayIndex(listSelector);
      loadDetail(idx);
      view = DETAIL;
      requestUpdate();
    }
    confirmHeld = false;
    confirmLongHandled = false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (searchActive) {
      clearFilter();
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down) && listSelector < total - 1) {
    listSelector++;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) && listSelector > 0) {
    listSelector--;
    requestUpdate();
  }
}

void ContactsActivity::loopDetail() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    view = LIST;
    requestUpdate();
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void ContactsActivity::renderList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();

  char hdr[48];
  if (searchActive) {
    snprintf(hdr, sizeof(hdr), "%s: %s", tr(STR_CONTACTS), searchQuery);
  } else {
    snprintf(hdr, sizeof(hdr), "%s", tr(STR_CONTACTS));
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight}, hdr);

  const int total = getDisplayCount();
  const int itemH = metrics.listWithSubtitleRowHeight;
  const int listTop = metrics.topPadding + metrics.headerHeight + 4;
  const int listBottom = pageH - metrics.buttonHintsHeight - 2;
  const int visCount = (listBottom - listTop) / itemH;

  if (total == 0) {
    const char* msg = (indexCount == 0 || searchActive) ? tr(STR_NO_CONTACTS) : tr(STR_CONTACTS);
    renderer.drawCenteredText(UI_10_FONT_ID, listTop + 40, msg);
  } else {
    // Scroll window
    int first = listSelector - visCount / 2;
    if (first < 0) first = 0;
    if (first + visCount > total) first = std::max(0, total - visCount);

    int y = listTop;
    for (int i = first; i < first + visCount && i < total; i++) {
      int idx = getDisplayIndex(i);
      bool sel = (i == listSelector);
      if (sel) renderer.fillRect(0, y, pageW, itemH, true);
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y + 4, index[idx].name, !sel,
                        EpdFontFamily::BOLD);
      if (index[idx].phone[0] != '\0') {
        renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y + 4 + renderer.getLineHeight(UI_10_FONT_ID) + 2,
                          index[idx].phone, !sel);
      }
      y += itemH;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "^", "v");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ContactsActivity::renderDetail() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageW, metrics.headerHeight},
                 detailLoaded ? detail.fn : index[detailIndexEntry].name);

  int y = metrics.topPadding + metrics.headerHeight + 10;
  const int lh = renderer.getLineHeight(UI_10_FONT_ID) + 4;
  const int pad = metrics.contentSidePadding;

  auto drawField = [&](const char* label, const char* value) {
    if (!value || value[0] == '\0') return;
    char line[96];
    snprintf(line, sizeof(line), "%s: %s", label, value);
    renderer.drawText(UI_10_FONT_ID, pad, y, line);
    y += lh;
  };

  if (detailLoaded) {
    drawField("Tel", detail.tel);
    drawField("Email", detail.email);
    drawField("Org", detail.org);
    if (detail.note[0] != '\0') {
      renderer.drawText(SMALL_FONT_ID, pad, y, detail.note);
    }
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, y + 20, tr(STR_LOADING_CONTACTS));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

void ContactsActivity::startSearch() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CONTACTS), searchQuery, 31),
      [this](const ActivityResult& r) {
        if (!r.isCancelled && std::holds_alternative<KeyboardResult>(r.data)) {
          const std::string& q = std::get<KeyboardResult>(r.data).text;
          if (q.empty()) {
            clearFilter();
          } else {
            applyFilter(q.c_str());
          }
        }
        requestUpdate();
      });
}
