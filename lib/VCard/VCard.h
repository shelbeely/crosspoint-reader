#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <string>

// Max contacts in the index cache. Keeps the index file bounded.
static constexpr uint16_t VCARD_MAX_CONTACTS = 2000;

// Default path for the vCard file on the SD card
static constexpr const char* VCARD_DEFAULT_PATH = "/contacts.vcf";

// Cache path for the contacts index
static constexpr const char* VCARD_CACHE_PATH = "/.crosspoint/contacts.bin";

// Index entry stored in the binary cache file
struct VCardIndexEntry {
  char name[64];       // Null-terminated display name (FN field)
  uint32_t offset;     // Byte offset of BEGIN:VCARD in the source .vcf file
};

// Fully-parsed contact loaded on demand from the .vcf file
struct VCardContact {
  char name[64];   // FN
  char phone[32];  // First TEL value
  char email[64];  // First EMAIL value
  char org[48];    // ORG
  char note[128];  // NOTE (truncated)
};

// Binary index file header
struct VCardIndexHeader {
  uint32_t magic;         // 0x56434658 ('VCFX')
  uint8_t version;        // Format version (currently 1)
  uint32_t vcfSize;       // Size of the .vcf file at index-build time (for cache invalidation)
  uint16_t contactCount;  // Number of index entries that follow
};

static constexpr uint32_t VCARD_INDEX_MAGIC = 0x56434658u;
static constexpr uint8_t VCARD_INDEX_VERSION = 1;

class VCardParser {
 public:
  VCardParser() = default;

  // Ensure the contacts index at VCARD_CACHE_PATH is up to date.
  // Rebuilds it if the .vcf file has changed or the index is missing.
  // Returns false if the .vcf file does not exist or indexing fails.
  bool ensureIndex(const char* vcfPath = VCARD_DEFAULT_PATH);

  // Return the number of contacts in the index (call after ensureIndex()).
  uint16_t getContactCount() const { return contactCount; }

  // Load one index entry by 0-based index. Returns false on error.
  bool getIndexEntry(uint16_t index, VCardIndexEntry& out) const;

  // Stream-parse a single contact from the .vcf file using the byte offset
  // stored in the index entry. Returns false on error.
  bool loadContact(const char* vcfPath, uint32_t offset, VCardContact& out) const;

 private:
  uint16_t contactCount = 0;

  bool buildIndex(const char* vcfPath);
  static void parseLine(const char* line, size_t len, VCardContact& contact);
  static void copyField(char* dst, size_t dstSize, const char* src);
};
