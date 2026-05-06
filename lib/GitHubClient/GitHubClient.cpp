#include "GitHubClient.h"

#include <cstdio>
#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#include <HTTPClient.h>
#include <Logging.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <Stream.h>
#include <WiFi.h>

#include <ctime>
#include <memory>

#include "GitHubCredentialStore.h"
#else
// Host-build minimal stubs so the URL/body builder helpers are
// compilable and testable without Arduino headers. The HTTP-touching
// methods are guarded with #ifdef ARDUINO and not exercised on host.
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#endif

namespace github {

namespace {

#ifdef ARDUINO
// Stream adapter: forwards `write()` bytes to an arbitrary feed
// callback. Used so we can pass our streaming JSON parser to
// `HTTPClient::writeToStream` without ever buffering the response.
class ParserFeedStream final : public ::Stream {
 public:
  ParserFeedStream(void (*feedFn)(void*, const char*, size_t), void* ctx) : feedFn_(feedFn), ctx_(ctx) {}

  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t size) override {
    if (feedFn_ && ctx_) feedFn_(ctx_, reinterpret_cast<const char*>(buf), size);
    return size;
  }

  // We're write-only.
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }

 private:
  void (*feedFn_)(void*, const char*, size_t);
  void* ctx_;
};
#endif

// Minimal manual URL-encoder for the small set of characters the
// callers actually pass (issue search queries are already encoded by
// the activity layer, so the only encoding we do here is the safe
// passthrough of `[A-Za-z0-9._~-/:?=&@+%]` and percent-encoding of
// space + a few delimiters). Keeps the implementation tiny.
std::string encodePathSegment(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out.append(buf);
    }
  }
  return out;
}

// Append a JSON-escaped form of `s` to `out`. We escape only the
// characters JSON requires: `"`, `\`, and control chars (<0x20).
// Everything else is passed through (UTF-8 friendly).
void appendJsonEscaped(std::string& out, const std::string& s) {
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        out.append("\\\"");
        break;
      case '\\':
        out.append("\\\\");
        break;
      case '\b':
        out.append("\\b");
        break;
      case '\f':
        out.append("\\f");
        break;
      case '\n':
        out.append("\\n");
        break;
      case '\r':
        out.append("\\r");
        break;
      case '\t':
        out.append("\\t");
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out.append(buf);
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
}

size_t clampPerPage(size_t n) {
  if (n == 0) return 1;
  if (n > MAX_ITEMS_PER_LIST) return MAX_ITEMS_PER_LIST;
  return n;
}

}  // namespace

// ============================================================================
// Static URL/body builders (host-testable)
// ============================================================================

std::string GitHubClient::buildSearchIssuesUrl(const std::string& apiBase, const std::string& urlEncodedQuery,
                                               size_t perPage) {
  char tail[32];
  snprintf(tail, sizeof(tail), "&per_page=%zu", clampPerPage(perPage));
  return apiBase + "/search/issues?q=" + urlEncodedQuery + tail;
}

std::string GitHubClient::buildWorkflowRunsUrl(const std::string& apiBase, const std::string& owner,
                                               const std::string& repo, const std::string& statusFilter,
                                               size_t perPage) {
  std::string url = apiBase + "/repos/" + encodePathSegment(owner) + "/" + encodePathSegment(repo) +
                    "/actions/runs?";
  char buf[32];
  snprintf(buf, sizeof(buf), "per_page=%zu", clampPerPage(perPage));
  url += buf;
  if (!statusFilter.empty()) {
    url += "&status=";
    url += encodePathSegment(statusFilter);
  }
  return url;
}

std::string GitHubClient::buildWorkflowJobsUrl(const std::string& apiBase, const std::string& owner,
                                               const std::string& repo, uint64_t runId, size_t perPage) {
  char tail[64];
  snprintf(tail, sizeof(tail), "/actions/runs/%llu/jobs?per_page=%zu", static_cast<unsigned long long>(runId),
           clampPerPage(perPage));
  return apiBase + "/repos/" + encodePathSegment(owner) + "/" + encodePathSegment(repo) + tail;
}

std::string GitHubClient::buildCommentBody(const std::string& commentBody) {
  std::string out = "{\"body\":\"";
  appendJsonEscaped(out, commentBody);
  out += "\"}";
  return out;
}

std::string GitHubClient::buildAssigneesBody(const std::string& login) {
  std::string out = "{\"assignees\":[\"";
  appendJsonEscaped(out, login);
  out += "\"]}";
  return out;
}

// ============================================================================
// GitHubClient
// ============================================================================

GitHubClient::GitHubClient()
    : token(),
      apiBase("https://api.github.com"),
      limiter(&ownedLimiter),
      ownedLimiter(),
      insecureTls(true) {
#ifdef ARDUINO
  token = GITHUB_STORE.getToken();
#endif
}

GitHubClient::GitHubClient(std::string token_, RateLimiter* limiter_)
    : token(std::move(token_)),
      apiBase("https://api.github.com"),
      limiter(limiter_ ? limiter_ : &ownedLimiter),
      ownedLimiter(),
      insecureTls(true) {}

void GitHubClient::setApiBase(const std::string& base) {
  apiBase = base;
  // Strip a single trailing slash so URL composition stays predictable.
  if (!apiBase.empty() && apiBase.back() == '/') apiBase.pop_back();
}

uint32_t GitHubClient::now() const {
#ifdef ARDUINO
  return static_cast<uint32_t>(time(nullptr));
#else
  return 0;
#endif
}

Result GitHubClient::interpretStatus(int status) {
  if (status == 0) return Result::TlsError;
  if (status == 401) return Result::NoToken;  // expired/invalid token
  if (status == 403 || status == 429) {
    // The caller will have populated lastRateLimit from headers; pass
    // the reset time to the limiter so subsequent calls are blocked.
    if (lastRateLimit.resetEpoch > 0) {
      limiter->suspendUntil(lastRateLimit.resetEpoch);
    }
    return Result::Suspended;
  }
  if (status >= 200 && status < 300) return Result::Ok;
  return Result::HttpError;
}

#ifdef ARDUINO

Result GitHubClient::doRequest(const char* method, const std::string& url, const std::string& jsonBody,
                               void (*feedFn)(void*, const char*, size_t), void* feedCtx, int* outStatus) {
  if (outStatus) *outStatus = 0;

  if (token.empty()) {
    LOG_DBG("GHC", "Skipping %s %s: no token", method, url.c_str());
    return Result::NoToken;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG_DBG("GHC", "Skipping %s %s: wifi down", method, url.c_str());
    return Result::WifiUnavailable;
  }
  const uint32_t nowSec = now();
  if (!limiter->canRequest(nowSec)) {
    LOG_DBG("GHC", "Throttled %s %s", method, url.c_str());
    return limiter->isSuspended(nowSec) ? Result::Suspended : Result::RateLimited;
  }

  // The PAT is sensitive — log only the URL, never the token or
  // Authorization header.
  LOG_DBG("GHC", "%s %s", method, url.c_str());

  auto* secureClient = new NetworkClientSecure();
  if (insecureTls) {
    // Phase 5 will switch this to a pinned CA bundle.
    secureClient->setInsecure();
  }
  std::unique_ptr<NetworkClient> client(secureClient);

  HTTPClient http;
  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");
  // PAT uses the `Bearer` scheme for fine-grained tokens.
  http.addHeader("Authorization", String("Bearer ") + token.c_str());
  if (!jsonBody.empty()) {
    http.addHeader("Content-Type", "application/json");
  }

  // Preserve the rate-limit headers so we can surface them later.
  static const char* kCollectHeaders[] = {"X-RateLimit-Remaining", "X-RateLimit-Limit", "X-RateLimit-Reset"};
  http.collectHeaders(kCollectHeaders, sizeof(kCollectHeaders) / sizeof(kCollectHeaders[0]));

  int status;
  if (strcmp(method, "GET") == 0) {
    status = http.GET();
  } else if (strcmp(method, "POST") == 0) {
    status = http.POST(reinterpret_cast<const uint8_t*>(jsonBody.data()), jsonBody.size());
  } else {
    LOG_ERR("GHC", "Unsupported method: %s", method);
    http.end();
    return Result::InvalidArgs;
  }
  if (outStatus) *outStatus = status;

  // Always update rate-limit cache, even on error.
  String hRemaining = http.header("X-RateLimit-Remaining");
  String hLimit = http.header("X-RateLimit-Limit");
  String hReset = http.header("X-RateLimit-Reset");
  if (hRemaining.length() > 0) lastRateLimit.remaining = hRemaining.toInt();
  if (hLimit.length() > 0) lastRateLimit.limit = hLimit.toInt();
  if (hReset.length() > 0) lastRateLimit.resetEpoch = static_cast<uint32_t>(hReset.toInt());

  if (status < 0) {
    LOG_ERR("GHC", "%s %s: TLS/transport error %d", method, url.c_str(), status);
    http.end();
    return Result::TlsError;
  }

  // Consume the rate-limit token only after we made a real HTTP round-trip.
  limiter->consume(nowSec);

  // Stream the body into the parser even on non-200 — error responses
  // are usually small JSON we don't parse but draining the body is
  // necessary for proper connection cleanup.
  if (feedFn != nullptr) {
    ParserFeedStream stream(feedFn, feedCtx);
    http.writeToStream(&stream);
  }
  http.end();

  return interpretStatus(status);
}

UserResult GitHubClient::getAuthenticatedUser() {
  UserResult out{Result::Ok, {}};
  UserParser parser;
  auto feed = [](void* ctx, const char* data, size_t len) {
    static_cast<UserParser*>(ctx)->feed(data, len);
  };
  int status = 0;
  out.result = doRequest("GET", apiBase + "/user", {}, feed, &parser, &status);
  if (out.result == Result::Ok) {
    if (parser.error()) {
      out.result = Result::ParseError;
    } else if (!parser.found()) {
      out.result = Result::ParseError;
    } else {
      out.user = parser.result();
    }
  }
  return out;
}

Result GitHubClient::searchIssues(const std::string& urlEncodedQuery, IssueListParser& outParser, size_t perPage) {
  outParser.reset();
  auto feed = [](void* ctx, const char* data, size_t len) {
    static_cast<IssueListParser*>(ctx)->feed(data, len);
  };
  int status = 0;
  Result r = doRequest("GET", buildSearchIssuesUrl(apiBase, urlEncodedQuery, perPage), {}, feed, &outParser, &status);
  if (r == Result::Ok && outParser.error()) return Result::ParseError;
  return r;
}

Result GitHubClient::listWorkflowRuns(const std::string& owner, const std::string& repo,
                                      const std::string& statusFilter, WorkflowRunsParser& outParser,
                                      size_t perPage) {
  outParser.reset();
  auto feed = [](void* ctx, const char* data, size_t len) {
    static_cast<WorkflowRunsParser*>(ctx)->feed(data, len);
  };
  int status = 0;
  Result r = doRequest("GET", buildWorkflowRunsUrl(apiBase, owner, repo, statusFilter, perPage), {}, feed,
                       &outParser, &status);
  if (r == Result::Ok && outParser.error()) return Result::ParseError;
  return r;
}

Result GitHubClient::listWorkflowJobs(const std::string& owner, const std::string& repo, uint64_t runId,
                                      WorkflowJobsParser& outParser, size_t perPage) {
  outParser.reset();
  auto feed = [](void* ctx, const char* data, size_t len) {
    static_cast<WorkflowJobsParser*>(ctx)->feed(data, len);
  };
  int status = 0;
  Result r = doRequest("GET", buildWorkflowJobsUrl(apiBase, owner, repo, runId, perPage), {}, feed, &outParser,
                       &status);
  if (r == Result::Ok && outParser.error()) return Result::ParseError;
  return r;
}

Result GitHubClient::assignIssue(const std::string& owner, const std::string& repo, uint32_t issueNumber,
                                 const std::string& assigneeLogin) {
  if (owner.empty() || repo.empty() || assigneeLogin.empty()) return Result::InvalidArgs;
  char tail[64];
  snprintf(tail, sizeof(tail), "/issues/%u/assignees", static_cast<unsigned>(issueNumber));
  std::string url = apiBase + "/repos/" + encodePathSegment(owner) + "/" + encodePathSegment(repo) + tail;
  int status = 0;
  return doRequest("POST", url, buildAssigneesBody(assigneeLogin), nullptr, nullptr, &status);
}

Result GitHubClient::rerunFailedJobs(const std::string& owner, const std::string& repo, uint64_t runId) {
  if (owner.empty() || repo.empty()) return Result::InvalidArgs;
  char tail[96];
  snprintf(tail, sizeof(tail), "/actions/runs/%llu/rerun-failed-jobs", static_cast<unsigned long long>(runId));
  std::string url = apiBase + "/repos/" + encodePathSegment(owner) + "/" + encodePathSegment(repo) + tail;
  int status = 0;
  // POST with no body is allowed; HTTPClient::POST(nullptr, 0) sends Content-Length: 0.
  return doRequest("POST", url, {}, nullptr, nullptr, &status);
}

Result GitHubClient::postComment(const std::string& owner, const std::string& repo, uint32_t issueOrPrNumber,
                                 const std::string& commentBody) {
  if (owner.empty() || repo.empty() || commentBody.empty()) return Result::InvalidArgs;
  char tail[64];
  snprintf(tail, sizeof(tail), "/issues/%u/comments", static_cast<unsigned>(issueOrPrNumber));
  std::string url = apiBase + "/repos/" + encodePathSegment(owner) + "/" + encodePathSegment(repo) + tail;
  int status = 0;
  return doRequest("POST", url, buildCommentBody(commentBody), nullptr, nullptr, &status);
}

#else  // !ARDUINO — stub I/O paths so host build links

Result GitHubClient::doRequest(const char*, const std::string&, const std::string&,
                               void (*)(void*, const char*, size_t), void*, int*) {
  return Result::TlsError;
}
UserResult GitHubClient::getAuthenticatedUser() { return {Result::TlsError, {}}; }
Result GitHubClient::searchIssues(const std::string&, IssueListParser&, size_t) { return Result::TlsError; }
Result GitHubClient::listWorkflowRuns(const std::string&, const std::string&, const std::string&,
                                      WorkflowRunsParser&, size_t) {
  return Result::TlsError;
}
Result GitHubClient::listWorkflowJobs(const std::string&, const std::string&, uint64_t, WorkflowJobsParser&,
                                      size_t) {
  return Result::TlsError;
}
Result GitHubClient::assignIssue(const std::string&, const std::string&, uint32_t, const std::string&) {
  return Result::TlsError;
}
Result GitHubClient::rerunFailedJobs(const std::string&, const std::string&, uint64_t) { return Result::TlsError; }
Result GitHubClient::postComment(const std::string&, const std::string&, uint32_t, const std::string&) {
  return Result::TlsError;
}

#endif  // ARDUINO

}  // namespace github
