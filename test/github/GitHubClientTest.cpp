// Host-side tests for the URL/body builders in `GitHubClient` and for
// `RateLimiter`. The HTTP-touching methods of `GitHubClient` are
// `#ifdef ARDUINO` and not exercised here.

#include <cstdio>
#include <cstring>

#include "GitHubClient.h"
#include "GitHubRateLimiter.h"

using namespace github;

namespace {

int passed = 0;
int failed = 0;

#define EXPECT(cond, msg)               \
  do {                                  \
    if (!(cond)) {                      \
      printf("  FAIL: %s\n", msg);      \
      ++failed;                         \
    } else {                            \
      ++passed;                         \
    }                                   \
  } while (0)

#define EXPECT_STR(actual, expected, msg)                                    \
  do {                                                                       \
    if ((actual) != (expected)) {                                            \
      printf("  FAIL: %s — expected '%s', got '%s'\n", msg, expected,         \
             std::string(actual).c_str());                                   \
      ++failed;                                                              \
    } else {                                                                 \
      ++passed;                                                              \
    }                                                                        \
  } while (0)

// --- URL builders -----------------------------------------------------------

void testSearchIssuesUrl() {
  printf("testSearchIssuesUrl:\n");
  // Caller pre-encodes the query (the activity layer does this — the
  // builder must NOT double-encode).
  std::string u = GitHubClient::buildSearchIssuesUrl(
      "https://api.github.com", "is%3Aopen+assignee%3A%40me", 20);
  EXPECT_STR(u, "https://api.github.com/search/issues?q=is%3Aopen+assignee%3A%40me&per_page=20",
             "search URL");
}

void testSearchIssuesUrlClampsPerPage() {
  printf("testSearchIssuesUrlClampsPerPage:\n");
  std::string u = GitHubClient::buildSearchIssuesUrl("https://api.github.com", "x", 9999);
  // MAX_ITEMS_PER_LIST is 20.
  EXPECT(u.find("&per_page=20") != std::string::npos, "perPage clamped to 20");
}

void testWorkflowRunsUrl() {
  printf("testWorkflowRunsUrl:\n");
  std::string u = GitHubClient::buildWorkflowRunsUrl(
      "https://api.github.com", "octo-org", "my repo", "completed", 10);
  EXPECT_STR(u,
             "https://api.github.com/repos/octo-org/my%20repo/actions/runs?per_page=10&status=completed",
             "runs URL with space-encoded repo");
}

void testWorkflowRunsUrlNoStatusFilter() {
  printf("testWorkflowRunsUrlNoStatusFilter:\n");
  std::string u = GitHubClient::buildWorkflowRunsUrl(
      "https://api.github.com", "o", "r", "", 5);
  EXPECT(u.find("&status=") == std::string::npos, "no status= when filter empty");
}

void testWorkflowJobsUrl() {
  printf("testWorkflowJobsUrl:\n");
  std::string u = GitHubClient::buildWorkflowJobsUrl(
      "https://api.github.com", "o", "r", 12345678901ULL, 20);
  EXPECT_STR(u, "https://api.github.com/repos/o/r/actions/runs/12345678901/jobs?per_page=20",
             "jobs URL with large run id");
}

// --- Body builders ----------------------------------------------------------

void testCommentBodyEscapesQuotes() {
  printf("testCommentBodyEscapesQuotes:\n");
  std::string body = GitHubClient::buildCommentBody("hello \"world\"\nline2");
  EXPECT_STR(body, R"({"body":"hello \"world\"\nline2"})", "JSON-escaped comment");
}

void testCommentBodyEscapesBackslash() {
  printf("testCommentBodyEscapesBackslash:\n");
  std::string body = GitHubClient::buildCommentBody("path\\file");
  EXPECT_STR(body, R"({"body":"path\\file"})", "backslash escape");
}

void testCommentBodyControlChars() {
  printf("testCommentBodyControlChars:\n");
  // NOTE: write the SOH byte as `\x01""b` to avoid `\x01b` being parsed
  // as a single hex escape (which equals ESC = 0x1B).
  std::string body = GitHubClient::buildCommentBody(std::string("a\x01" "b"));
  EXPECT_STR(body, R"({"body":"a\u0001b"})", "control char as \\u escape");
}

void testAssigneesBody() {
  printf("testAssigneesBody:\n");
  std::string body = GitHubClient::buildAssigneesBody("Copilot");
  EXPECT_STR(body, R"({"assignees":["Copilot"]})", "assignees body");
}

// --- RateLimiter ------------------------------------------------------------

void testRateLimiterBasic() {
  printf("testRateLimiterBasic:\n");
  RateLimiter rl(/*capacity=*/3, /*refillPerSec=*/1);
  uint32_t t = 1000;
  EXPECT(rl.canRequest(t), "starts allowed");
  rl.consume(t);
  rl.consume(t);
  rl.consume(t);
  // Bucket exhausted at the same instant.
  EXPECT(!rl.canRequest(t), "exhausted");
  // One second later one token regenerates.
  EXPECT(rl.canRequest(t + 1), "refilled after 1s");
}

void testRateLimiterRefillCap() {
  printf("testRateLimiterRefillCap:\n");
  RateLimiter rl(2, 1);
  uint32_t t = 100;
  rl.consume(t);
  // Wait long enough to refill far beyond capacity.
  EXPECT(rl.tokensAvailable(t + 1000) == 2, "cap respected after long idle");
}

void testRateLimiterSuspendUntil() {
  printf("testRateLimiterSuspendUntil:\n");
  RateLimiter rl(5, 1);
  uint32_t t = 5000;
  rl.suspendUntil(t + 60);
  EXPECT(!rl.canRequest(t + 30), "suspended in window");
  EXPECT(rl.canRequest(t + 61), "released after window");
}

void testRateLimiterSuspendOnlyExtends() {
  printf("testRateLimiterSuspendOnlyExtends:\n");
  RateLimiter rl(5, 1);
  rl.suspendUntil(2000);
  rl.suspendUntil(1000);  // earlier — must NOT shorten the suspend
  EXPECT(rl.suspendedUntil() == 2000, "suspend never shortens");
}

}  // namespace

int main() {
  testSearchIssuesUrl();
  testSearchIssuesUrlClampsPerPage();
  testWorkflowRunsUrl();
  testWorkflowRunsUrlNoStatusFilter();
  testWorkflowJobsUrl();
  testCommentBodyEscapesQuotes();
  testCommentBodyEscapesBackslash();
  testCommentBodyControlChars();
  testAssigneesBody();
  testRateLimiterBasic();
  testRateLimiterRefillCap();
  testRateLimiterSuspendUntil();
  testRateLimiterSuspendOnlyExtends();

  printf("\n%d passed, %d failed\n", passed, failed);
  return failed == 0 ? 0 : 1;
}
