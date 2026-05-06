#pragma once

// Filesystem wrapper for a Markdown document.
//
// Mirrors the role of `lib/Txt/Txt.h`: knows the file's path, the cache
// directory it should use (`.crosspoint/md_<hash>/`), and provides chunked
// random-access reads over the file. Pagination, parsing, and rendering
// live in `MarkdownReaderActivity` and `lib/Markdown/MarkdownParser`.

#include <HalStorage.h>

#include <memory>
#include <string>

class Markdown {
  std::string filepath;
  std::string cacheBasePath;
  std::string cachePath;
  bool loaded = false;
  size_t fileSize = 0;

 public:
  explicit Markdown(std::string path, std::string cacheBasePath);

  bool load();
  [[nodiscard]] const std::string& getPath() const { return filepath; }
  [[nodiscard]] const std::string& getCachePath() const { return cachePath; }
  [[nodiscard]] std::string getTitle() const;
  [[nodiscard]] size_t getFileSize() const { return fileSize; }

  void setupCacheDir() const;

  // Read a window of the file into the caller-supplied buffer.
  // Returns true on success and indicates how many bytes were read via
  // outBytesRead (which may be less than length when reading near EOF).
  [[nodiscard]] bool readContent(uint8_t* buffer, size_t offset, size_t length, size_t& outBytesRead) const;
};
