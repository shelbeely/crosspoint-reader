#include "Markdown.h"

#include <FsHelpers.h>
#include <Logging.h>

Markdown::Markdown(std::string path, std::string cacheBasePathArg)
    : filepath(std::move(path)), cacheBasePath(std::move(cacheBasePathArg)) {
  // Cache subdirectory is hashed from the absolute file path. Moving or
  // renaming the file produces a new hash and a fresh cache, matching the
  // behaviour of the EPUB and TXT readers.
  const size_t hash = std::hash<std::string>{}(filepath);
  cachePath = cacheBasePath + "/md_" + std::to_string(hash);
}

bool Markdown::load() {
  if (loaded) return true;

  if (!Storage.exists(filepath.c_str())) {
    LOG_ERR("MD", "File does not exist: %s", filepath.c_str());
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("MD", filepath, file)) {
    LOG_ERR("MD", "Failed to open file: %s", filepath.c_str());
    return false;
  }

  fileSize = file.size();
  file.close();

  loaded = true;
  LOG_DBG("MD", "Loaded Markdown file: %s (%zu bytes)", filepath.c_str(), fileSize);
  return true;
}

std::string Markdown::getTitle() const {
  size_t lastSlash = filepath.find_last_of('/');
  std::string filename = (lastSlash != std::string::npos) ? filepath.substr(lastSlash + 1) : filepath;

  // Strip .md / .markdown extension for display.
  if (FsHelpers::hasMarkdownExtension(filename)) {
    size_t dot = filename.find_last_of('.');
    if (dot != std::string::npos) filename = filename.substr(0, dot);
  }
  return filename;
}

void Markdown::setupCacheDir() const {
  if (!Storage.exists(cacheBasePath.c_str())) {
    Storage.mkdir(cacheBasePath.c_str());
  }
  if (!Storage.exists(cachePath.c_str())) {
    Storage.mkdir(cachePath.c_str());
  }
}

bool Markdown::readContent(uint8_t* buffer, size_t offset, size_t length, size_t& outBytesRead) const {
  outBytesRead = 0;
  if (!loaded) return false;

  FsFile file;
  if (!Storage.openFileForRead("MD", filepath, file)) {
    return false;
  }
  if (!file.seek(offset)) {
    return false;
  }
  outBytesRead = file.read(buffer, length);
  return true;
}
