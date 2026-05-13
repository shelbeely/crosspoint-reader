#pragma once

#include <cstdint>

// VCard — streaming vCard 3.0 parser (pure C++, no Arduino, host-testable).
//
// Usage:
//   VCard parser;
//   parser.reset(sink, &sinkCtx, maxContacts);
//   feedLine("FN:Alice Smith");
//   feedLine("TEL:+1-555-0100");
//   feedLine("END:VCARD");
//   ...
//
// The sink callback is invoked once per completed contact record.
//
// Index cache format (/.crosspoint/contacts.bin):
//   4 bytes  magic  0x56434658 ("VCFX")
//   1 byte   version (1)
//   4 bytes  uint32 record count
//   N × sizeof(VCardIndex) records
//
// VCardIndex is used directly from firmware code to display the contact list
// without re-parsing the .vcf file.

struct VCardRecord {
  char fn[48];     // Full name (FN)
  char tel[24];    // First telephone number
  char email[64];  // First email address
  char org[48];    // Organisation (ORG)
  char note[96];   // Note (NOTE), truncated
};

struct VCardIndex {
  uint32_t vcfOffset;  // byte offset of the BEGIN:VCARD line in the .vcf file
  char name[48];       // same as VCardRecord::fn
  char phone[24];      // same as VCardRecord::tel
};

class VCard {
 public:
  static constexpr uint32_t CACHE_MAGIC = 0x56434658u;  // "VCFX"
  static constexpr uint8_t CACHE_VERSION = 1;
  static constexpr int MAX_CONTACTS = 128;

  // Sink callback — called once per completed VCARD block
  using Sink = void (*)(void* ctx, const VCardRecord& record, uint32_t vcfOffset);

  VCard() = default;

  // Reset the parser state.  `sink` and `sinkCtx` are optional; pass nullptr
  // to skip the per-record callback and only update statistics.
  void reset(Sink sink = nullptr, void* sinkCtx = nullptr);

  // Feed one line of a .vcf file (without the trailing newline).
  // `lineOffset` is the byte offset of the first character of this line in
  // the source file — used to record the BEGIN:VCARD position.
  void feedLine(const char* line, size_t len, uint32_t lineOffset = 0);

  int getRecordCount() const { return recordCount; }

 private:
  Sink sink = nullptr;
  void* sinkCtx = nullptr;

  bool inVCard = false;
  uint32_t beginOffset = 0;
  VCardRecord current{};
  int recordCount = 0;

  void commitRecord();
  static void copyField(char* dst, size_t dstLen, const char* src, size_t srcLen);
};
