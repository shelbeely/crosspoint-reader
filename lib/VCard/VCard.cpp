#include "VCard.h"

#include <cstring>

// ---------------------------------------------------------------------------
// VCard streaming parser implementation
// ---------------------------------------------------------------------------

void VCard::reset(Sink newSink, void* newCtx) {
  sink = newSink;
  sinkCtx = newCtx;
  inVCard = false;
  beginOffset = 0;
  recordCount = 0;
  memset(&current, 0, sizeof(current));
}

void VCard::feedLine(const char* line, size_t len, uint32_t lineOffset) {
  if (len == 0) return;

  // Strip trailing \r if present
  if (len > 0 && line[len - 1] == '\r') len--;
  if (len == 0) return;

  // Case-insensitive comparison helper (property names are ASCII)
  auto iStartsWith = [&](const char* prefix, size_t prefixLen) -> bool {
    if (len < prefixLen) return false;
    for (size_t i = 0; i < prefixLen; i++) {
      char a = line[i];
      char b = prefix[i];
      if (a >= 'a' && a <= 'z') a -= 32;
      if (b >= 'a' && b <= 'z') b -= 32;
      if (a != b) return false;
    }
    return true;
  };

  if (!inVCard) {
    if (iStartsWith("BEGIN:VCARD", 11)) {
      inVCard = true;
      beginOffset = lineOffset;
      memset(&current, 0, sizeof(current));
    }
    return;
  }

  if (iStartsWith("END:VCARD", 9)) {
    commitRecord();
    inVCard = false;
    return;
  }

  // Find the colon (or semicolon before colon for property parameters)
  // Property format: NAME[;param]:value
  size_t colonPos = 0;
  for (colonPos = 0; colonPos < len; colonPos++) {
    if (line[colonPos] == ':') break;
  }
  if (colonPos >= len) return;  // no colon — skip malformed line

  // Property name ends at first ':' or ';'
  size_t nameEnd = colonPos;
  for (size_t i = 0; i < colonPos; i++) {
    if (line[i] == ';') { nameEnd = i; break; }
  }

  const char* value = line + colonPos + 1;
  size_t valueLen = (colonPos + 1 < len) ? len - colonPos - 1 : 0;

  // FN — formatted name
  if (nameEnd == 2 && iStartsWith("FN", 2)) {
    if (current.fn[0] == '\0') copyField(current.fn, sizeof(current.fn), value, valueLen);
    return;
  }
  // TEL
  if (nameEnd == 3 && iStartsWith("TEL", 3)) {
    if (current.tel[0] == '\0') copyField(current.tel, sizeof(current.tel), value, valueLen);
    return;
  }
  // EMAIL
  if (nameEnd == 5 && iStartsWith("EMAIL", 5)) {
    if (current.email[0] == '\0') copyField(current.email, sizeof(current.email), value, valueLen);
    return;
  }
  // ORG
  if (nameEnd == 3 && iStartsWith("ORG", 3)) {
    if (current.org[0] == '\0') copyField(current.org, sizeof(current.org), value, valueLen);
    return;
  }
  // NOTE
  if (nameEnd == 4 && iStartsWith("NOTE", 4)) {
    if (current.note[0] == '\0') copyField(current.note, sizeof(current.note), value, valueLen);
    return;
  }
  // N — structured name (fallback for FN if not present)
  if (nameEnd == 1 && iStartsWith("N", 1)) {
    if (current.fn[0] == '\0') {
      // N format: Family;Given;Middle;Prefix;Suffix
      // Build "Given Family" as a simple display name
      char buf[48] = {};
      size_t i = 0, out = 0;
      const char* family = value;
      size_t familyLen = 0;
      // Find first semicolon
      while (i < valueLen && value[i] != ';') i++;
      familyLen = i;
      if (i < valueLen) i++;  // skip ';'
      // Given name starts here
      const char* given = value + i;
      size_t givenLen = 0;
      while (i < valueLen && value[i] != ';') { i++; givenLen++; }

      if (givenLen > 0 && out + givenLen + 1 < sizeof(buf)) {
        memcpy(buf + out, given, givenLen);
        out += givenLen;
        if (familyLen > 0) buf[out++] = ' ';
      }
      if (familyLen > 0 && out + familyLen < sizeof(buf)) {
        memcpy(buf + out, family, familyLen);
        out += familyLen;
      }
      buf[out] = '\0';
      if (buf[0] != '\0') copyField(current.fn, sizeof(current.fn), buf, out);
    }
    return;
  }
}

void VCard::commitRecord() {
  recordCount++;
  if (sink) {
    sink(sinkCtx, current, beginOffset);
  }
}

void VCard::copyField(char* dst, size_t dstLen, const char* src, size_t srcLen) {
  size_t n = srcLen < dstLen - 1 ? srcLen : dstLen - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}
