// Host-side unit tests for `lib/GitHubClient/GitHubResponseParsers`.
// Built and run by `test/run_github_test.sh` — no Arduino dependency.
//
// These cover the streaming parsers in isolation. The HTTPS client
// itself (URL building, header injection, rate limiting) is tested
// only by the on-device manual matrix described in `SCOPE.md`.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "GitHubResponseParsers.h"

using namespace github;

namespace {

int passed = 0;
int failed = 0;

#define EXPECT(cond, msg)                                     \
  do {                                                        \
    if (!(cond)) {                                            \
      printf("  FAIL: %s\n", msg);                            \
      ++failed;                                               \
    } else {                                                  \
      ++passed;                                               \
    }                                                         \
  } while (0)

#define EXPECT_STR(actual, expected, msg)                                    \
  do {                                                                       \
    if (strcmp((actual), (expected)) != 0) {                                 \
      printf("  FAIL: %s — expected '%s', got '%s'\n", msg, expected, actual); \
      ++failed;                                                              \
    } else {                                                                 \
      ++passed;                                                              \
    }                                                                        \
  } while (0)

// --- UserParser -------------------------------------------------------------

void testUserBasic() {
  printf("testUserBasic:\n");
  UserParser p;
  const char* json = R"({"login":"octocat","id":1,"plan":{"name":"free"}})";
  p.feed(json, strlen(json));
  EXPECT(!p.error(), "no parse error");
  EXPECT(p.found(), "login found");
  EXPECT_STR(p.result().login, "octocat", "login value");
}

void testUserNestedLoginIgnored() {
  printf("testUserNestedLoginIgnored:\n");
  // GitHub's /user response embeds an `owner` object on some endpoints.
  // Make sure nested `login` does not overwrite the top-level one.
  UserParser p;
  const char* json = R"({"login":"alice","plan":{"login":"bob"}})";
  p.feed(json, strlen(json));
  EXPECT(p.found(), "login found");
  EXPECT_STR(p.result().login, "alice", "top-level login wins");
}

void testUserChunked() {
  printf("testUserChunked:\n");
  UserParser p;
  const char* json = R"({"login":"streamuser","id":42})";
  // Feed one byte at a time to exercise the streaming behaviour.
  for (size_t i = 0; i < strlen(json); ++i) {
    p.feed(json + i, 1);
  }
  EXPECT(p.found(), "login found");
  EXPECT_STR(p.result().login, "streamuser", "login matches");
}

// --- IssueListParser --------------------------------------------------------

void testIssueListBasic() {
  printf("testIssueListBasic:\n");
  IssueListParser p;
  // Trimmed shape of a real /search/issues response.
  const char* json = R"({
    "total_count": 2,
    "incomplete_results": false,
    "items": [
      {
        "number": 42,
        "title": "Add markdown reader",
        "state": "open",
        "user": {"login": "alice"},
        "repository_url": "https://api.github.com/repos/octo/widget",
        "labels": [{"name": "enhancement"}]
      },
      {
        "number": 43,
        "title": "Fix CI",
        "state": "open",
        "user": {"login": "bob"},
        "pull_request": {"url": "https://api.github.com/repos/octo/widget/pulls/43"},
        "repository_url": "https://api.github.com/repos/octo/widget"
      }
    ]
  })";
  p.feed(json, strlen(json));
  EXPECT(!p.error(), "no parse error");
  EXPECT(p.count() == 2, "two items parsed");

  EXPECT(p.item(0).number == 42, "item[0].number");
  EXPECT_STR(p.item(0).title, "Add markdown reader", "item[0].title");
  EXPECT_STR(p.item(0).state, "open", "item[0].state");
  EXPECT_STR(p.item(0).authorLogin, "alice", "item[0].author");
  EXPECT_STR(p.item(0).repoFullName, "octo/widget", "item[0].repo");
  EXPECT(!p.item(0).isPullRequest, "item[0] is issue");

  EXPECT(p.item(1).number == 43, "item[1].number");
  EXPECT_STR(p.item(1).authorLogin, "bob", "item[1].author");
  EXPECT(p.item(1).isPullRequest, "item[1] is PR (pull_request key present)");
}

void testIssueListChunked() {
  printf("testIssueListChunked:\n");
  IssueListParser p;
  const char* json = R"({"items":[{"number":1,"title":"hi","user":{"login":"u"},"repository_url":"https://api.github.com/repos/o/r"}]})";
  for (size_t i = 0; i < strlen(json); ++i) p.feed(json + i, 1);
  EXPECT(p.count() == 1, "one item");
  EXPECT_STR(p.item(0).repoFullName, "o/r", "repo via chunked feed");
}

void testIssueListCap() {
  printf("testIssueListCap:\n");
  // Feed MAX_ITEMS_PER_LIST + 5 items; expect only MAX_ITEMS_PER_LIST retained.
  std::string json = "{\"items\":[";
  for (size_t i = 0; i < MAX_ITEMS_PER_LIST + 5; ++i) {
    if (i > 0) json += ',';
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"number\":%zu,\"title\":\"t\",\"user\":{\"login\":\"u\"},\"repository_url\":\"https://api.github.com/repos/o/r\"}",
             i);
    json += buf;
  }
  json += "]}";
  IssueListParser p;
  p.feed(json.data(), json.size());
  EXPECT(p.count() == MAX_ITEMS_PER_LIST, "cap respected");
}

void testIssueListIgnoresNestedUserLogin() {
  printf("testIssueListIgnoresNestedUserLogin:\n");
  // `user.plan.name` and similar nested fields must not overwrite
  // `user.login` even if a deeply nested `login` key exists.
  IssueListParser p;
  const char* json = R"({"items":[{
    "number":7, "title":"t",
    "user":{"login":"top","plan":{"login":"shouldNotWin"}},
    "repository_url":"https://api.github.com/repos/o/r"
  }]})";
  p.feed(json, strlen(json));
  EXPECT(p.count() == 1, "one item");
  EXPECT_STR(p.item(0).authorLogin, "top", "nested login ignored");
}

void testIssueListEnterpriseUrl() {
  printf("testIssueListEnterpriseUrl:\n");
  // GHES uses `https://gh.example.com/api/v3/repos/...` form.
  IssueListParser p;
  const char* json = R"({"items":[{
    "number":1, "title":"t",
    "user":{"login":"u"},
    "repository_url":"https://gh.example.com/api/v3/repos/team/proj"
  }]})";
  p.feed(json, strlen(json));
  EXPECT_STR(p.item(0).repoFullName, "team/proj", "GHES URL parsed");
}

// --- WorkflowRunsParser -----------------------------------------------------

void testWorkflowRunsBasic() {
  printf("testWorkflowRunsBasic:\n");
  WorkflowRunsParser p;
  const char* json = R"({
    "total_count": 2,
    "workflow_runs": [
      {"id": 100, "name": "CI", "status": "completed", "conclusion": "success",
       "head_branch": "main", "run_number": 5},
      {"id": 101, "name": "CI", "status": "in_progress", "conclusion": null,
       "head_branch": "feature/x", "run_number": 6,
       "head_commit": {"message": "wip"}}
    ]
  })";
  p.feed(json, strlen(json));
  EXPECT(!p.error(), "no parse error");
  EXPECT(p.count() == 2, "two runs");
  EXPECT(p.item(0).id == 100, "run[0].id");
  EXPECT_STR(p.item(0).status, "completed", "run[0].status");
  EXPECT_STR(p.item(0).conclusion, "success", "run[0].conclusion");
  EXPECT_STR(p.item(0).headBranch, "main", "run[0].head_branch");
  EXPECT(p.item(0).runNumber == 5, "run[0].run_number");

  EXPECT(p.item(1).id == 101, "run[1].id");
  EXPECT_STR(p.item(1).status, "in_progress", "run[1].status");
  // null conclusion → unset → empty string
  EXPECT_STR(p.item(1).conclusion, "", "run[1].conclusion empty");
  EXPECT_STR(p.item(1).headBranch, "feature/x", "run[1].head_branch");
}

// --- WorkflowJobsParser -----------------------------------------------------

void testWorkflowJobsBasic() {
  printf("testWorkflowJobsBasic:\n");
  WorkflowJobsParser p;
  const char* json = R"({
    "total_count": 1,
    "jobs": [
      {"id": 999, "name": "build", "status": "completed", "conclusion": "failure",
       "steps": [{"name": "checkout","conclusion":"success"},
                 {"name": "test","conclusion":"failure"}]}
    ]
  })";
  p.feed(json, strlen(json));
  EXPECT(p.count() == 1, "one job");
  EXPECT(p.item(0).id == 999, "job.id");
  EXPECT_STR(p.item(0).name, "build", "job.name");
  // Make sure the nested step name didn't overwrite the job name.
  EXPECT_STR(p.item(0).status, "completed", "job.status");
  EXPECT_STR(p.item(0).conclusion, "failure", "job.conclusion");
}

}  // namespace

int main() {
  testUserBasic();
  testUserNestedLoginIgnored();
  testUserChunked();
  testIssueListBasic();
  testIssueListChunked();
  testIssueListCap();
  testIssueListIgnoresNestedUserLogin();
  testIssueListEnterpriseUrl();
  testWorkflowRunsBasic();
  testWorkflowJobsBasic();

  printf("\n%d passed, %d failed\n", passed, failed);
  return failed == 0 ? 0 : 1;
}
