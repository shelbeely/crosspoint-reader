// High-level GitHub REST client used by the Companion mode. Wraps the
// Arduino-ESP32 `HTTPClient` + `NetworkClientSecure` pair that the rest
// of the codebase already uses (`src/network/HttpDownloader.cpp`).
//
// Design constraints from `SCOPE.md`:
//   - Single TLS connection at a time. The connection is torn down
//     after each request — no persistent socket, no background polling.
//   - Stream JSON parse via the parsers in `GitHubResponseParsers`.
//     Never buffer a whole response into a `String`.
//   - Cap items per list request (`per_page=20`).
//   - Honour `X-RateLimit-Reset` on 403/429.
//   - The PAT MUST NOT be logged. `LOG_DBG` lines that touch the token
//     emit `<redacted>` in its place.
//
// All endpoint methods return `Result` so the UI can distinguish
// "no token configured" from "rate-limited" from "HTTP 4xx" from "TLS
// failed". The detail-view layer renders a different banner per case.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "GitHubRateLimiter.h"
#include "GitHubResponseParsers.h"
#include "GitHubTypes.h"

namespace github {

enum class Result : uint8_t {
  Ok = 0,
  NoToken,
  WifiUnavailable,
  RateLimited,    // local bucket says wait
  Suspended,      // server-driven suspend (X-RateLimit-Reset in the future)
  HttpError,      // HTTP 4xx/5xx other than 403/429
  ParseError,     // JSON parser reported an error
  TlsError,       // connection or TLS handshake failed
  InvalidArgs,
};

// Returned by methods that fetch a single object. The body is
// populated only when `result == Ok`.
struct UserResult {
  Result result;
  UserInfo user;
};

class GitHubClient {
 public:
  // Default constructor uses the global token store (`GITHUB_STORE`)
  // and instantiates an internal RateLimiter. The two-argument form is
  // used by tests (and could be used in future to share a limiter
  // across clients).
  GitHubClient();
  GitHubClient(std::string token, RateLimiter* limiter);

  // The base URL defaults to `https://api.github.com`. GHES users can
  // override via the planned settings UI.
  void setApiBase(const std::string& base);
  const std::string& getApiBase() const { return apiBase; }

  // Whether to pin the Mozilla CA bundle. Defaults to `false` (uses
  // `setInsecure()` because Arduino-ESP32 doesn't ship a CA bundle by
  // default and shipping one is its own work item). The plan permits
  // `setInsecure()` "behind an explicit setting" — this is that setting.
  // Phase 5 will add a real pinned bundle.
  void setInsecureTls(bool insecure) { insecureTls = insecure; }

  // GET /user — validates the token and returns the login name.
  UserResult getAuthenticatedUser();

  // GET /search/issues?q=<query>&per_page=N. Caller supplies the raw
  // query string (URL-encoded). N is clamped to MAX_ITEMS_PER_LIST.
  Result searchIssues(const std::string& urlEncodedQuery, IssueListParser& outParser, size_t perPage = MAX_ITEMS_PER_LIST);

  // GET /repos/{owner}/{repo}/actions/runs?per_page=N&status=...
  // `statusFilter` may be empty (no status filter) or one of GitHub's
  // accepted values ("completed","in_progress","queued","failure"...).
  Result listWorkflowRuns(const std::string& owner, const std::string& repo, const std::string& statusFilter,
                          WorkflowRunsParser& outParser, size_t perPage = MAX_ITEMS_PER_LIST);

  // GET /repos/{owner}/{repo}/actions/runs/{id}/jobs
  Result listWorkflowJobs(const std::string& owner, const std::string& repo, uint64_t runId,
                          WorkflowJobsParser& outParser, size_t perPage = MAX_ITEMS_PER_LIST);

  // POST /repos/{owner}/{repo}/issues/{n}/assignees
  // Assigns the bot handle (or any login) to the issue. Used by
  // "Assign to Copilot" — the caller supplies the configured handle so
  // GHE/different bot names work.
  Result assignIssue(const std::string& owner, const std::string& repo, uint32_t issueNumber,
                     const std::string& assigneeLogin);

  // POST /repos/{owner}/{repo}/actions/runs/{id}/rerun-failed-jobs
  Result rerunFailedJobs(const std::string& owner, const std::string& repo, uint64_t runId);

  // POST /repos/{owner}/{repo}/issues/{n}/comments — used by "Request
  // PR summary" (issue API also serves PRs). The body is sent as
  // {"body": "<commentBody>"} and the comment text is JSON-escaped.
  Result postComment(const std::string& owner, const std::string& repo, uint32_t issueOrPrNumber,
                     const std::string& commentBody);

  // Last-known rate-limit info from the most recent response headers.
  const RateLimitInfo& getLastRateLimit() const { return lastRateLimit; }

  // Override the token at runtime (e.g. after the user enters one via
  // the keyboard activity). Safe to call between requests.
  void setToken(const std::string& newToken) { token = newToken; }

  // Test seam: returns the URL the client would build for a given
  // search query. Pure string manipulation, no I/O.
  static std::string buildSearchIssuesUrl(const std::string& apiBase, const std::string& urlEncodedQuery,
                                          size_t perPage);
  static std::string buildWorkflowRunsUrl(const std::string& apiBase, const std::string& owner,
                                          const std::string& repo, const std::string& statusFilter, size_t perPage);
  static std::string buildWorkflowJobsUrl(const std::string& apiBase, const std::string& owner,
                                          const std::string& repo, uint64_t runId, size_t perPage);

  // Test seam: encodes a comment body as a JSON object {"body":"..."}.
  static std::string buildCommentBody(const std::string& commentBody);
  // Test seam: encodes an assignees POST body as {"assignees":["login"]}.
  static std::string buildAssigneesBody(const std::string& login);

 private:
  // Returns the wall-clock epoch in seconds. On Arduino we use
  // `time(nullptr)`; on host tests this is overridable via `nowFn_`.
  uint32_t now() const;

  // Internal request entry-points. `bodyParser` may be null.
  // `parserCb`'s "feed" function is called with each chunk from the
  // wire. `outStatus` is the HTTP response code (0 on TLS failure).
  Result doRequest(const char* method, const std::string& url, const std::string& jsonBody,
                   void (*feedFn)(void* ctx, const char* data, size_t len), void* feedCtx, int* outStatus);

  // Translates an HTTP status code + this client's state into a
  // `Result` enum, also updating `lastRateLimit` and the limiter as
  // appropriate.
  Result interpretStatus(int status);

  std::string token;
  std::string apiBase;
  RateLimiter* limiter;
  RateLimiter ownedLimiter;  // used when caller didn't supply one
  bool insecureTls;
  RateLimitInfo lastRateLimit{};
};

}  // namespace github
