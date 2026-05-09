#include "VCard.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cctype>
#include <cstring>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint32_t getFileSize(FsFile& f) { return static_cast<uint32_t>(f.fileSize()); }

// Case-insensitive prefix check
static bool hasPrefix(const char* line, const char* prefix) {
  while (*prefix) {
    if (std::tolower(static_cast<unsigned char>(*line)) !=
        std::tolower(static_cast<unsigned char>(*prefix))) {
      return false;
    }
    ++line;
    ++prefix;
  }
  return true;
}

// Skip vCard property name and optional parameters, return pointer to value
// e.g. "FN:John" -> "John",  "TEL;TYPE=CELL:555" -> "555"
static const char* skipToValue(const char* line) {
  const char* colon = line;
  while (*colon && *colon != ':') ++colon;
  if (*colon == ':') return colon + 1;
  return nullptr;
}

// ---------------------------------------------------------------------------
// VCardParser::copyField
// ---------------------------------------------------------------------------

void VCardParser::copyField(char* dst, size_t dstSize, const char* src) {
  if (!src || dstSize == 0) return;
  size_t i = 0;
  while (src[i] && i < dstSize - 1) {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

// ---------------------------------------------------------------------------
// VCardParser::parseLine — update a VCardContact with one vCard property line
// ---------------------------------------------------------------------------

void VCardParser::parseLine(const char* line, size_t len, VCardContact& contact) {
  if (len == 0) return;

  // FN — full name
  if (hasPrefix(line, "FN:") || hasPrefix(line, "FN;")) {
    const char* val = skipToValue(line);
    if (val && contact.name[0] == '\0') {
      copyField(contact.name, sizeof(contact.name), val);
    }
    return;
  }
  // TEL — telephone
  if (hasPrefix(line, "TEL") && contact.phone[0] == '\0') {
    const char* val = skipToValue(line);
    if (val) copyField(contact.phone, sizeof(contact.phone), val);
    return;
  }
  // EMAIL
  if (hasPrefix(line, "EMAIL") && contact.email[0] == '\0') {
    const char* val = skipToValue(line);
    if (val) copyField(contact.email, sizeof(contact.email), val);
    return;
  }
  // ORG
  if ((hasPrefix(line, "ORG:") || hasPrefix(line, "ORG;")) && contact.org[0] == '\0') {
    const char* val = skipToValue(line);
    if (val) {
      // ORG value may have multiple components separated by ';'; use first
      char tmp[48];
      size_t i = 0;
      while (val[i] && val[i] != ';' && i < sizeof(tmp) - 1) {
        tmp[i] = val[i];
        ++i;
      }
      tmp[i] = '\0';
      copyField(contact.org, sizeof(contact.org), tmp);
    }
    return;
  }
  // NOTE
  if ((hasPrefix(line, "NOTE:") || hasPrefix(line, "NOTE;")) && contact.note[0] == '\0') {
    const char* val = skipToValue(line);
    if (val) copyField(contact.note, sizeof(contact.note), val);
    return;
  }
}

// ---------------------------------------------------------------------------
// VCardParser::buildIndex
// ---------------------------------------------------------------------------

bool VCardParser::buildIndex(const char* vcfPath) {
  FsFile vcf;
  if (!Storage.openFileForRead("VCF", vcfPath, vcf)) {
    LOG_ERR("VCF", "Cannot open %s for indexing", vcfPath);
    return false;
  }

  const uint32_t vcfSize = getFileSize(vcf);
  vcf.seekSet(0);

  // Open index for writing
  FsFile idx;
  Storage.remove(VCARD_CACHE_PATH);
  if (!Storage.openFileForWrite("VCF", VCARD_CACHE_PATH, idx)) {
    LOG_ERR("VCF", "Cannot create index %s", VCARD_CACHE_PATH);
    return false;
  }

  // Write placeholder header; we will overwrite it at the end with the real count
  VCardIndexHeader hdr = {};
  hdr.magic = VCARD_INDEX_MAGIC;
  hdr.version = VCARD_INDEX_VERSION;
  hdr.vcfSize = vcfSize;
  hdr.contactCount = 0;
  idx.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));

  // Parse the .vcf file line by line using a small stack buffer
  static constexpr size_t LINE_BUF = 256;
  char lineBuf[LINE_BUF];
  size_t lineLen = 0;
  uint16_t count = 0;
  bool inCard = false;
  uint32_t cardOffset = 0;
  char cardName[64] = {};

  auto flushEntry = [&]() {
    if (count >= VCARD_MAX_CONTACTS) return;
    VCardIndexEntry entry = {};
    copyField(entry.name, sizeof(entry.name), cardName[0] ? cardName : "(No Name)");
    entry.offset = cardOffset;
    idx.write(reinterpret_cast<const uint8_t*>(&entry), sizeof(entry));
    ++count;
  };

  uint32_t bytePos = 0;

  while (true) {
    int ch = vcf.read();
    if (ch < 0) break;

    const char c = static_cast<char>(ch);
    ++bytePos;

    if (c == '\n') {
      // Strip trailing \r
      if (lineLen > 0 && lineBuf[lineLen - 1] == '\r') --lineLen;
      lineBuf[lineLen] = '\0';

      if (!inCard && hasPrefix(lineBuf, "BEGIN:VCARD")) {
        inCard = true;
        // cardOffset = position of 'B' in "BEGIN:VCARD"
        cardOffset = bytePos - lineLen - 2;  // approximate start of this line
        if (cardOffset > bytePos) cardOffset = 0;  // safety clamp
        cardName[0] = '\0';
      } else if (inCard) {
        if (hasPrefix(lineBuf, "FN:") || hasPrefix(lineBuf, "FN;")) {
          const char* val = skipToValue(lineBuf);
          if (val) copyField(cardName, sizeof(cardName), val);
        } else if (hasPrefix(lineBuf, "END:VCARD")) {
          flushEntry();
          inCard = false;
        }
      }

      lineLen = 0;
    } else if (lineLen < LINE_BUF - 1) {
      lineBuf[lineLen++] = c;
    }
  }

  // Re-seek to start and write final header with real count
  hdr.contactCount = count;
  idx.seekSet(0);
  idx.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));

  contactCount = count;
  LOG_DBG("VCF", "Index built: %u contacts", count);
  return true;
}

// ---------------------------------------------------------------------------
// VCardParser::ensureIndex
// ---------------------------------------------------------------------------

bool VCardParser::ensureIndex(const char* vcfPath) {
  // Open the .vcf file to read its size
  FsFile vcf;
  if (!Storage.openFileForRead("VCF", vcfPath, vcf)) {
    LOG_DBG("VCF", "No contacts file at %s", vcfPath);
    contactCount = 0;
    return false;
  }
  const uint32_t vcfSize = getFileSize(vcf);

  // Try to read existing index header
  FsFile idx;
  bool needRebuild = true;
  if (Storage.openFileForRead("VCF", VCARD_CACHE_PATH, idx)) {
    VCardIndexHeader hdr = {};
    if (idx.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) == sizeof(hdr)) {
      if (hdr.magic == VCARD_INDEX_MAGIC && hdr.version == VCARD_INDEX_VERSION &&
          hdr.vcfSize == vcfSize) {
        contactCount = hdr.contactCount;
        needRebuild = false;
        LOG_DBG("VCF", "Index cache valid: %u contacts", contactCount);
      }
    }
  }

  if (needRebuild) {
    LOG_DBG("VCF", "Rebuilding contacts index...");
    return buildIndex(vcfPath);
  }
  return true;
}

// ---------------------------------------------------------------------------
// VCardParser::getIndexEntry
// ---------------------------------------------------------------------------

bool VCardParser::getIndexEntry(uint16_t index, VCardIndexEntry& out) const {
  if (index >= contactCount) return false;

  FsFile idx;
  if (!Storage.openFileForRead("VCF", VCARD_CACHE_PATH, idx)) {
    LOG_ERR("VCF", "Cannot open index for read");
    return false;
  }

  const uint32_t entryOffset = sizeof(VCardIndexHeader) + static_cast<uint32_t>(index) * sizeof(VCardIndexEntry);
  if (!idx.seekSet(entryOffset)) {
    LOG_ERR("VCF", "Seek failed for entry %u", index);
    return false;
  }

  return idx.read(reinterpret_cast<uint8_t*>(&out), sizeof(out)) == sizeof(out);
}

// ---------------------------------------------------------------------------
// VCardParser::loadContact
// ---------------------------------------------------------------------------

bool VCardParser::loadContact(const char* vcfPath, uint32_t offset, VCardContact& out) const {
  FsFile vcf;
  if (!Storage.openFileForRead("VCF", vcfPath, vcf)) {
    LOG_ERR("VCF", "Cannot open %s for detail load", vcfPath);
    return false;
  }
  if (!vcf.seekSet(offset)) {
    LOG_ERR("VCF", "Seek to offset %u failed", offset);
    return false;
  }

  memset(&out, 0, sizeof(out));

  static constexpr size_t LINE_BUF = 256;
  char lineBuf[LINE_BUF];
  size_t lineLen = 0;
  bool inCard = false;

  while (true) {
    int ch = vcf.read();
    if (ch < 0) break;

    const char c = static_cast<char>(ch);
    if (c == '\n') {
      if (lineLen > 0 && lineBuf[lineLen - 1] == '\r') --lineLen;
      lineBuf[lineLen] = '\0';

      if (!inCard) {
        if (hasPrefix(lineBuf, "BEGIN:VCARD")) inCard = true;
      } else {
        if (hasPrefix(lineBuf, "END:VCARD")) break;
        parseLine(lineBuf, lineLen, out);
      }
      lineLen = 0;
    } else if (lineLen < LINE_BUF - 1) {
      lineBuf[lineLen++] = c;
    }
  }

  return true;
}
