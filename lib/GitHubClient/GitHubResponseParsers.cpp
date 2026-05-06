#include "GitHubResponseParsers.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace github {

namespace {

// Copy at most dstSize-1 bytes and NUL-terminate. Truncation is silent
// because most fields (titles, names) can legitimately exceed the
// device's display width and we'd rather render a truncated value than
// drop the whole record.
void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  if (dstSize == 0) return;
  size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

uint64_t parseU64(const char* s, size_t len) {
  uint64_t v = 0;
  for (size_t i = 0; i < len; ++i) {
    char c = s[i];
    if (c < '0' || c > '9') break;
    v = v * 10 + static_cast<uint64_t>(c - '0');
  }
  return v;
}

uint32_t parseU32(const char* s, size_t len) {
  return static_cast<uint32_t>(parseU64(s, len));
}

bool keyEq(const char* k, size_t len, const char* lit) {
  size_t litLen = strlen(lit);
  return len == litLen && memcmp(k, lit, litLen) == 0;
}

}  // namespace

// ============================================================================
// UserParser
// ============================================================================

UserParser::UserParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}) {
  reset();
}

void UserParser::reset() {
  parser.reset();
  user = {};
  depth = 0;
  nextStringIsLogin = false;
  loginFound = false;
}

void UserParser::feed(const char* data, size_t len) { parser.feed(data, len); }
bool UserParser::error() const { return parser.hasError(); }

void UserParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<UserParser*>(ctx);
  if (self->depth < 255) self->depth++;
}

void UserParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<UserParser*>(ctx);
  if (self->depth > 0) self->depth--;
  // After the top-level object closes any further keys are ignored.
}

void UserParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<UserParser*>(ctx);
  // Only watch top-level `login` — nested objects (plan/owner) may
  // contain login fields too and we must not pick them up.
  self->nextStringIsLogin = (self->depth == 1 && keyEq(key, len, "login"));
}

void UserParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<UserParser*>(ctx);
  if (self->nextStringIsLogin) {
    safeCopy(self->user.login, sizeof(self->user.login), value, len);
    self->loginFound = true;
    self->nextStringIsLogin = false;
  }
}

// ============================================================================
// IssueListParser
// ============================================================================

IssueListParser::IssueListParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}) {
  reset();
}

void IssueListParser::reset() {
  parser.reset();
  itemCount = 0;
  for (auto& it : items) it = {};
  resetCurrentItem();
  position = Position::TOP;
  lastKey = LastKey::NONE;
  depth = 0;
  itemDepth = 0;
  userDepth = 0;
  inItemsArray = false;
}

void IssueListParser::feed(const char* data, size_t len) { parser.feed(data, len); }
bool IssueListParser::error() const { return parser.hasError(); }

void IssueListParser::resetCurrentItem() { current = {}; }

void IssueListParser::commitItem() {
  if (itemCount < MAX_ITEMS_PER_LIST) {
    items[itemCount++] = current;
  }
  resetCurrentItem();
}

void IssueListParser::extractRepoFromUrl(const char* url, size_t len) {
  // `repository_url` is `https://api.github.com/repos/{owner}/{repo}`.
  // Find the segment after `/repos/` and copy the rest up to the next
  // `/` or end-of-string. Robust against subdomain changes (e.g.
  // GitHub Enterprise's `https://api.example.com/api/v3/repos/...`).
  static const char marker[] = "/repos/";
  const size_t markerLen = sizeof(marker) - 1;
  if (len < markerLen) return;
  // Search from the end is unnecessary — `/repos/` appears once.
  for (size_t i = 0; i + markerLen <= len; ++i) {
    if (memcmp(url + i, marker, markerLen) == 0) {
      const size_t start = i + markerLen;
      size_t end = start;
      // Allow exactly one `/` separating owner and repo. Stop on the
      // second `/` or any non-path character.
      int slashes = 0;
      while (end < len) {
        char c = url[end];
        if (c == '/') {
          if (++slashes > 1) break;
        } else if (c == '?' || c == '#') {
          break;
        }
        ++end;
      }
      safeCopy(current.repoFullName, sizeof(current.repoFullName), url + start, end - start);
      return;
    }
  }
}

void IssueListParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<IssueListParser*>(ctx);
  if (self->depth < 255) self->depth++;
  if (self->position == Position::IN_ITEMS && self->lastKey == LastKey::NONE) {
    // Entering a top-level item.
    self->position = Position::IN_ITEM;
    self->itemDepth = self->depth;
    self->resetCurrentItem();
  } else if (self->position == Position::IN_ITEM && self->lastKey == LastKey::USER) {
    self->position = Position::IN_USER;
    self->userDepth = self->depth;
    self->lastKey = LastKey::NONE;
  } else if (self->position == Position::IN_ITEM && self->lastKey == LastKey::PULL_REQUEST) {
    // Presence of the `pull_request` object signals a PR. Mark it and
    // descend into a "skip" container so nested keys don't pollute
    // lastKey.
    self->current.isPullRequest = true;
    self->position = Position::IN_OTHER_OBJECT;
    self->lastKey = LastKey::NONE;
  } else if (self->position == Position::IN_ITEM) {
    // Some other nested object (labels, milestone, reactions, ...)
    self->position = Position::IN_OTHER_OBJECT;
    self->lastKey = LastKey::NONE;
  }
}

void IssueListParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<IssueListParser*>(ctx);
  if (self->position == Position::IN_ITEM && self->depth == self->itemDepth) {
    self->commitItem();
    self->position = Position::IN_ITEMS;
  } else if (self->position == Position::IN_USER && self->depth == self->userDepth) {
    self->position = Position::IN_ITEM;
  } else if (self->position == Position::IN_OTHER_OBJECT && self->depth <= self->itemDepth + 1) {
    // Climbed back out of the nested object into the item.
    self->position = Position::IN_ITEM;
  }
  if (self->depth > 0) self->depth--;
  self->lastKey = LastKey::NONE;
}

void IssueListParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<IssueListParser*>(ctx);
  if (self->position == Position::TOP && self->lastKey == LastKey::NONE && self->inItemsArray) {
    self->position = Position::IN_ITEMS;
  }
  // Reset lastKey on entering arrays inside an item (e.g. `labels`)
  // to keep `commitItem` defensive against array-valued keys.
  if (self->position == Position::IN_ITEM) {
    self->lastKey = LastKey::NONE;
  }
}

void IssueListParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<IssueListParser*>(ctx);
  if (self->position == Position::IN_ITEMS) {
    self->position = Position::TOP;
    self->inItemsArray = false;
  }
}

void IssueListParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<IssueListParser*>(ctx);
  if (self->position == Position::TOP && self->depth == 1) {
    if (keyEq(key, len, "items")) self->inItemsArray = true;
    self->lastKey = LastKey::NONE;
    return;
  }
  if (self->position == Position::IN_ITEM) {
    if (keyEq(key, len, "number"))
      self->lastKey = LastKey::NUMBER;
    else if (keyEq(key, len, "title"))
      self->lastKey = LastKey::TITLE;
    else if (keyEq(key, len, "repository_url"))
      self->lastKey = LastKey::REPOSITORY_URL;
    else if (keyEq(key, len, "state"))
      self->lastKey = LastKey::STATE;
    else if (keyEq(key, len, "user"))
      self->lastKey = LastKey::USER;
    else if (keyEq(key, len, "pull_request"))
      self->lastKey = LastKey::PULL_REQUEST;
    else
      self->lastKey = LastKey::NONE;
    return;
  }
  if (self->position == Position::IN_USER) {
    // Only match "login" at the top of the user object — `plan.login`
    // and other nested fields must not leak through.
    if (self->depth == self->userDepth && keyEq(key, len, "login")) {
      self->lastKey = LastKey::LOGIN;
    } else {
      self->lastKey = LastKey::NONE;
    }
    return;
  }
  self->lastKey = LastKey::NONE;
}

void IssueListParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<IssueListParser*>(ctx);
  switch (self->lastKey) {
    case LastKey::TITLE:
      safeCopy(self->current.title, sizeof(self->current.title), value, len);
      break;
    case LastKey::REPOSITORY_URL:
      self->extractRepoFromUrl(value, len);
      break;
    case LastKey::STATE:
      safeCopy(self->current.state, sizeof(self->current.state), value, len);
      break;
    case LastKey::LOGIN:
      safeCopy(self->current.authorLogin, sizeof(self->current.authorLogin), value, len);
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void IssueListParser::sOnNumber(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<IssueListParser*>(ctx);
  if (self->lastKey == LastKey::NUMBER) {
    self->current.number = parseU32(value, len);
  }
  self->lastKey = LastKey::NONE;
}

// ============================================================================
// WorkflowRunsParser
// ============================================================================

WorkflowRunsParser::WorkflowRunsParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}) {
  reset();
}

void WorkflowRunsParser::reset() {
  parser.reset();
  runCount = 0;
  for (auto& r : runs) r = {};
  currentRun = {};
  position = Position::TOP;
  lastKey = LastKey::NONE;
  depth = 0;
  runDepth = 0;
}

void WorkflowRunsParser::feed(const char* data, size_t len) { parser.feed(data, len); }
bool WorkflowRunsParser::error() const { return parser.hasError(); }

void WorkflowRunsParser::commitRun() {
  if (runCount < MAX_ITEMS_PER_LIST) {
    runs[runCount++] = currentRun;
  }
  currentRun = {};
}

void WorkflowRunsParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<WorkflowRunsParser*>(ctx);
  if (self->depth < 255) self->depth++;
  if (self->position == Position::IN_RUNS && self->lastKey == LastKey::NONE) {
    self->position = Position::IN_RUN;
    self->runDepth = self->depth;
    self->currentRun = {};
  } else if (self->position == Position::IN_RUN) {
    self->position = Position::IN_OTHER_OBJECT;
    self->lastKey = LastKey::NONE;
  }
}

void WorkflowRunsParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<WorkflowRunsParser*>(ctx);
  if (self->position == Position::IN_RUN && self->depth == self->runDepth) {
    self->commitRun();
    self->position = Position::IN_RUNS;
  } else if (self->position == Position::IN_OTHER_OBJECT && self->depth <= self->runDepth + 1) {
    self->position = Position::IN_RUN;
  }
  if (self->depth > 0) self->depth--;
  self->lastKey = LastKey::NONE;
}

void WorkflowRunsParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<WorkflowRunsParser*>(ctx);
  if (self->position == Position::TOP && self->lastKey == LastKey::NONE) {
    // Top-level arrays we don't recognise; ignored.
  } else if (self->position == Position::IN_RUN) {
    self->lastKey = LastKey::NONE;
  }
}

void WorkflowRunsParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<WorkflowRunsParser*>(ctx);
  if (self->position == Position::IN_RUNS) {
    self->position = Position::TOP;
  }
}

void WorkflowRunsParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<WorkflowRunsParser*>(ctx);
  if (self->position == Position::TOP && self->depth == 1) {
    if (keyEq(key, len, "workflow_runs")) {
      // The next array start marks IN_RUNS — but we use lastKey to
      // gate that. Easier: switch to IN_RUNS unconditionally on the
      // following array.
      self->position = Position::IN_RUNS;
    }
    self->lastKey = LastKey::NONE;
    return;
  }
  if (self->position == Position::IN_RUN) {
    if (keyEq(key, len, "id"))
      self->lastKey = LastKey::ID;
    else if (keyEq(key, len, "name"))
      self->lastKey = LastKey::NAME;
    else if (keyEq(key, len, "status"))
      self->lastKey = LastKey::STATUS;
    else if (keyEq(key, len, "conclusion"))
      self->lastKey = LastKey::CONCLUSION;
    else if (keyEq(key, len, "head_branch"))
      self->lastKey = LastKey::HEAD_BRANCH;
    else if (keyEq(key, len, "run_number"))
      self->lastKey = LastKey::RUN_NUMBER;
    else
      self->lastKey = LastKey::NONE;
    return;
  }
  self->lastKey = LastKey::NONE;
}

void WorkflowRunsParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<WorkflowRunsParser*>(ctx);
  switch (self->lastKey) {
    case LastKey::NAME:
      safeCopy(self->currentRun.name, sizeof(self->currentRun.name), value, len);
      break;
    case LastKey::STATUS:
      safeCopy(self->currentRun.status, sizeof(self->currentRun.status), value, len);
      break;
    case LastKey::CONCLUSION:
      safeCopy(self->currentRun.conclusion, sizeof(self->currentRun.conclusion), value, len);
      break;
    case LastKey::HEAD_BRANCH:
      safeCopy(self->currentRun.headBranch, sizeof(self->currentRun.headBranch), value, len);
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void WorkflowRunsParser::sOnNumber(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<WorkflowRunsParser*>(ctx);
  switch (self->lastKey) {
    case LastKey::ID:
      self->currentRun.id = parseU64(value, len);
      break;
    case LastKey::RUN_NUMBER:
      self->currentRun.runNumber = parseU32(value, len);
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

// ============================================================================
// WorkflowJobsParser
// ============================================================================

WorkflowJobsParser::WorkflowJobsParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}) {
  reset();
}

void WorkflowJobsParser::reset() {
  parser.reset();
  jobCount = 0;
  for (auto& j : jobs) j = {};
  currentJob = {};
  position = Position::TOP;
  lastKey = LastKey::NONE;
  depth = 0;
  jobDepth = 0;
}

void WorkflowJobsParser::feed(const char* data, size_t len) { parser.feed(data, len); }
bool WorkflowJobsParser::error() const { return parser.hasError(); }

void WorkflowJobsParser::commitJob() {
  if (jobCount < MAX_ITEMS_PER_LIST) {
    jobs[jobCount++] = currentJob;
  }
  currentJob = {};
}

void WorkflowJobsParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<WorkflowJobsParser*>(ctx);
  if (self->depth < 255) self->depth++;
  if (self->position == Position::IN_JOBS && self->lastKey == LastKey::NONE) {
    self->position = Position::IN_JOB;
    self->jobDepth = self->depth;
    self->currentJob = {};
  } else if (self->position == Position::IN_JOB) {
    self->position = Position::IN_OTHER_OBJECT;
    self->lastKey = LastKey::NONE;
  }
}

void WorkflowJobsParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<WorkflowJobsParser*>(ctx);
  if (self->position == Position::IN_JOB && self->depth == self->jobDepth) {
    self->commitJob();
    self->position = Position::IN_JOBS;
  } else if (self->position == Position::IN_OTHER_OBJECT && self->depth <= self->jobDepth + 1) {
    self->position = Position::IN_JOB;
  }
  if (self->depth > 0) self->depth--;
  self->lastKey = LastKey::NONE;
}

void WorkflowJobsParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<WorkflowJobsParser*>(ctx);
  if (self->position == Position::IN_JOB) {
    self->lastKey = LastKey::NONE;
  }
}

void WorkflowJobsParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<WorkflowJobsParser*>(ctx);
  if (self->position == Position::IN_JOBS) {
    self->position = Position::TOP;
  }
}

void WorkflowJobsParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<WorkflowJobsParser*>(ctx);
  if (self->position == Position::TOP && self->depth == 1) {
    if (keyEq(key, len, "jobs")) self->position = Position::IN_JOBS;
    self->lastKey = LastKey::NONE;
    return;
  }
  if (self->position == Position::IN_JOB) {
    if (keyEq(key, len, "id"))
      self->lastKey = LastKey::ID;
    else if (keyEq(key, len, "name"))
      self->lastKey = LastKey::NAME;
    else if (keyEq(key, len, "status"))
      self->lastKey = LastKey::STATUS;
    else if (keyEq(key, len, "conclusion"))
      self->lastKey = LastKey::CONCLUSION;
    else
      self->lastKey = LastKey::NONE;
    return;
  }
  self->lastKey = LastKey::NONE;
}

void WorkflowJobsParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<WorkflowJobsParser*>(ctx);
  switch (self->lastKey) {
    case LastKey::NAME:
      safeCopy(self->currentJob.name, sizeof(self->currentJob.name), value, len);
      break;
    case LastKey::STATUS:
      safeCopy(self->currentJob.status, sizeof(self->currentJob.status), value, len);
      break;
    case LastKey::CONCLUSION:
      safeCopy(self->currentJob.conclusion, sizeof(self->currentJob.conclusion), value, len);
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void WorkflowJobsParser::sOnNumber(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<WorkflowJobsParser*>(ctx);
  if (self->lastKey == LastKey::ID) {
    self->currentJob.id = parseU64(value, len);
  }
  self->lastKey = LastKey::NONE;
}

}  // namespace github
