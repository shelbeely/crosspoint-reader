// Small fixed-size result structs used by `lib/GitHubClient` and its
// streaming parsers. All buffer sizes are tuned to fit comfortably in
// the e-ink viewport while staying inside the 60 KB Companion-mode
// peak-heap budget declared in `SCOPE.md`.
//
// Strings are fixed `char[]` (not `std::string`) so the parsers can run
// without heap allocations during the SAX walk — this matches the
// pattern used by `lib/JsonParser/ReleaseJsonParser`.

#pragma once

#include <cstddef>
#include <cstdint>

namespace github {

// Maximum items the device will retain from any list response. The plan
// caps requests at "20 items per request"; we round up so a request with
// 20 items always fits.
constexpr size_t MAX_ITEMS_PER_LIST = 20;

// Buffer sizes are deliberately conservative. Titles longer than 128
// chars are truncated for display — acceptable because the e-ink status
// bar already truncates long titles.
constexpr size_t MAX_LOGIN_LEN = 64;
constexpr size_t MAX_TITLE_LEN = 128;
constexpr size_t MAX_REPO_FULL_NAME_LEN = 96;  // "owner/repo", GitHub allows up to 40+1+100 but rarely seen
constexpr size_t MAX_BRANCH_LEN = 64;
constexpr size_t MAX_STATUS_LEN = 16;       // "queued"/"in_progress"/"completed"/...
constexpr size_t MAX_CONCLUSION_LEN = 16;   // "success"/"failure"/"cancelled"/...
constexpr size_t MAX_NAME_LEN = 64;         // workflow/job name
constexpr size_t MAX_LOG_URL_LEN = 256;     // pre-signed log URL is long but bounded

struct UserInfo {
  char login[MAX_LOGIN_LEN];
};

// One entry in a `search/issues` response. Used for assigned tasks,
// needs-review, and active-PRs lists.
struct IssueItem {
  uint32_t number = 0;
  bool isPullRequest = false;            // discriminates issues from PRs
  char title[MAX_TITLE_LEN];
  char repoFullName[MAX_REPO_FULL_NAME_LEN];  // "owner/repo" parsed from repository_url
  char authorLogin[MAX_LOGIN_LEN];
  char state[MAX_STATUS_LEN];                  // "open"/"closed"
};

// One entry in a workflow-runs response.
struct WorkflowRunItem {
  uint64_t id = 0;
  char name[MAX_NAME_LEN];                     // workflow name
  char status[MAX_STATUS_LEN];
  char conclusion[MAX_CONCLUSION_LEN];         // "success"/"failure"/"" while in_progress
  char headBranch[MAX_BRANCH_LEN];
  uint32_t runNumber = 0;
};

// One entry in a workflow-jobs response.
struct WorkflowJobItem {
  uint64_t id = 0;
  char name[MAX_NAME_LEN];
  char status[MAX_STATUS_LEN];
  char conclusion[MAX_CONCLUSION_LEN];
};

// Rate-limit bookkeeping returned in the response headers.
struct RateLimitInfo {
  int remaining = -1;       // -1 == unknown
  int limit = -1;
  uint32_t resetEpoch = 0;  // X-RateLimit-Reset (Unix epoch seconds)
};

}  // namespace github
