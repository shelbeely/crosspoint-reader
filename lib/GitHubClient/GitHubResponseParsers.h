// Streaming SAX parsers for the small set of GitHub REST responses the
// Companion mode consumes. Pure C++ (no Arduino dependency) so they can
// be host-tested under `test/github/`.
//
// Each parser exposes `feed(const char*, size_t)` (chunk-friendly) and
// `result()`. Parsers retain at most `github::MAX_ITEMS_PER_LIST` items
// and silently drop any extras — the GitHub API supports `per_page` so
// the caller is responsible for asking for the right number.

#pragma once

#include <cstddef>
#include <cstdint>

#include "GitHubTypes.h"
#include "StreamingJsonParser.h"

namespace github {

// Maximum string length we tolerate on the wire (per individual JSON
// string token). Strings longer than this are truncated when copied
// into the result struct's fixed buffer; titles can legitimately exceed
// 128 chars and we want to keep going rather than aborting the parse.
//
// Must not exceed StreamingJsonParser::TOKEN_BUF_SIZE (which already
// truncates internally) but we declare it explicitly here so future
// changes to that constant are caught at compile time.
constexpr size_t MAX_FIELD_LEN = 512;

// Parses `GET /user`. Only extracts `login`.
class UserParser {
 public:
  UserParser();
  void reset();
  void feed(const char* data, size_t len);
  bool found() const { return loginFound; }
  const UserInfo& result() const { return user; }
  bool error() const;

 private:
  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void*, const char*, size_t) {}
  static void sOnBool(void*, bool) {}
  static void sOnNull(void*) {}
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void*) {}
  static void sOnArrayEnd(void*) {}

  UserInfo user{};
  StreamingJsonParser parser;
  uint8_t depth = 0;
  bool nextStringIsLogin = false;
  bool loginFound = false;
};

// Parses `GET /search/issues?...`. Walks the top-level `items[]` array
// and extracts up to `MAX_ITEMS_PER_LIST` issue records.
//
// Note: GitHub returns the repo as `repository_url`
// (`https://api.github.com/repos/{owner}/{repo}`), not as a
// `full_name`. We extract the trailing `owner/repo` segment.
//
// `pull_request` is an object (not a key) — the presence of the key
// itself is the signal that the item is a PR. We track this per-item.
class IssueListParser {
 public:
  IssueListParser();
  void reset();
  void feed(const char* data, size_t len);
  size_t count() const { return itemCount; }
  const IssueItem& item(size_t i) const { return items[i]; }
  bool error() const;

 private:
  enum class Position : uint8_t { TOP, IN_ITEMS, IN_ITEM, IN_USER, IN_OTHER_OBJECT };
  enum class LastKey : uint8_t {
    NONE,
    NUMBER,
    TITLE,
    REPOSITORY_URL,
    STATE,
    USER,
    LOGIN,
    PULL_REQUEST,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void*, bool) {}
  static void sOnNull(void*) {}
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void commitItem();
  void resetCurrentItem();
  void extractRepoFromUrl(const char* url, size_t len);

  IssueItem items[MAX_ITEMS_PER_LIST]{};
  size_t itemCount = 0;
  IssueItem current{};

  StreamingJsonParser parser;
  Position position = Position::TOP;
  LastKey lastKey = LastKey::NONE;
  uint8_t depth = 0;
  uint8_t itemDepth = 0;     // depth at the start of an item object
  uint8_t userDepth = 0;     // depth at the start of the nested user object
  bool inItemsArray = false;
};

// Parses `GET /repos/{owner}/{repo}/actions/runs?...`. Top-level
// `workflow_runs[]` array.
class WorkflowRunsParser {
 public:
  WorkflowRunsParser();
  void reset();
  void feed(const char* data, size_t len);
  size_t count() const { return runCount; }
  const WorkflowRunItem& item(size_t i) const { return runs[i]; }
  bool error() const;

 private:
  enum class Position : uint8_t { TOP, IN_RUNS, IN_RUN, IN_OTHER_OBJECT };
  enum class LastKey : uint8_t {
    NONE,
    ID,
    NAME,
    STATUS,
    CONCLUSION,
    HEAD_BRANCH,
    RUN_NUMBER,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void*, bool) {}
  static void sOnNull(void*) {}
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void commitRun();

  WorkflowRunItem runs[MAX_ITEMS_PER_LIST]{};
  size_t runCount = 0;
  WorkflowRunItem currentRun{};

  StreamingJsonParser parser;
  Position position = Position::TOP;
  LastKey lastKey = LastKey::NONE;
  uint8_t depth = 0;
  uint8_t runDepth = 0;
};

// Parses `GET /repos/{owner}/{repo}/actions/runs/{id}/jobs`. Top-level
// `jobs[]` array.
class WorkflowJobsParser {
 public:
  WorkflowJobsParser();
  void reset();
  void feed(const char* data, size_t len);
  size_t count() const { return jobCount; }
  const WorkflowJobItem& item(size_t i) const { return jobs[i]; }
  bool error() const;

 private:
  enum class Position : uint8_t { TOP, IN_JOBS, IN_JOB, IN_OTHER_OBJECT };
  enum class LastKey : uint8_t { NONE, ID, NAME, STATUS, CONCLUSION };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void*, bool) {}
  static void sOnNull(void*) {}
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void commitJob();

  WorkflowJobItem jobs[MAX_ITEMS_PER_LIST]{};
  size_t jobCount = 0;
  WorkflowJobItem currentJob{};

  StreamingJsonParser parser;
  Position position = Position::TOP;
  LastKey lastKey = LastKey::NONE;
  uint8_t depth = 0;
  uint8_t jobDepth = 0;
};

}  // namespace github
