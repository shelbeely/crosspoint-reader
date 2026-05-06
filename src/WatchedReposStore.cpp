#include "WatchedReposStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

WatchedReposStore WatchedReposStore::instance;

namespace {
constexpr char WATCHED_FILE_JSON[] = "/.crosspoint/watched_repos.json";

// Validate `owner` / `repo` segments. GitHub's actual rules are stricter
// than this but we only need to reject characters that would corrupt URL
// construction in `lib/GitHubClient` (path separators, query/fragment
// markers, whitespace).
bool isValidSegment(const std::string& s) {
  if (s.empty() || s.size() > 100) return false;
  for (char c : s) {
    if (c == '/' || c == '?' || c == '#' || c == '&' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      return false;
    }
  }
  return true;
}

bool parseSlug(const std::string& slug, WatchedRepo& out) {
  const auto pos = slug.find('/');
  if (pos == std::string::npos || pos == 0 || pos == slug.size() - 1) return false;
  out.owner = slug.substr(0, pos);
  out.repo = slug.substr(pos + 1);
  // Reject extra `/` segments — `owner/repo/extra` is not a valid repo.
  if (out.repo.find('/') != std::string::npos) return false;
  return isValidSegment(out.owner) && isValidSegment(out.repo);
}
}  // namespace

bool WatchedReposStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveWatchedRepos(*this, WATCHED_FILE_JSON);
}

bool WatchedReposStore::loadFromFile() {
  if (!Storage.exists(WATCHED_FILE_JSON)) return false;

  String json = Storage.readFile(WATCHED_FILE_JSON);
  if (json.isEmpty()) return false;

  return JsonSettingsIO::loadWatchedRepos(*this, json.c_str());
}

bool WatchedReposStore::addRepo(const WatchedRepo& repo) {
  if (!isValidSegment(repo.owner) || !isValidSegment(repo.repo)) {
    LOG_ERR("WRS", "Rejected invalid repo: %s/%s", repo.owner.c_str(), repo.repo.c_str());
    return false;
  }
  if (contains(repo.owner, repo.repo)) {
    // Already present — skip the write per the throttling rule.
    return false;
  }
  if (repos.size() >= MAX_REPOS) {
    LOG_DBG("WRS", "Cannot add repo: cap of %zu reached", MAX_REPOS);
    return false;
  }
  repos.push_back(repo);
  LOG_DBG("WRS", "Added watched repo: %s/%s", repo.owner.c_str(), repo.repo.c_str());
  return saveToFile();
}

bool WatchedReposStore::addFromSlug(const std::string& slug) {
  WatchedRepo r;
  if (!parseSlug(slug, r)) {
    LOG_ERR("WRS", "Cannot parse slug: %s", slug.c_str());
    return false;
  }
  return addRepo(r);
}

bool WatchedReposStore::removeRepo(size_t index) {
  if (index >= repos.size()) return false;
  LOG_DBG("WRS", "Removed watched repo: %s/%s", repos[index].owner.c_str(), repos[index].repo.c_str());
  repos.erase(repos.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}

bool WatchedReposStore::removeBySlug(const std::string& slug) {
  WatchedRepo r;
  if (!parseSlug(slug, r)) return false;
  for (size_t i = 0; i < repos.size(); ++i) {
    if (repos[i].owner == r.owner && repos[i].repo == r.repo) {
      return removeRepo(i);
    }
  }
  return false;
}

bool WatchedReposStore::contains(const std::string& owner, const std::string& repo) const {
  for (const auto& r : repos) {
    if (r.owner == owner && r.repo == repo) return true;
  }
  return false;
}

const WatchedRepo* WatchedReposStore::getRepo(size_t index) const {
  if (index >= repos.size()) return nullptr;
  return &repos[index];
}

void WatchedReposStore::clearAll() {
  if (repos.empty()) return;
  repos.clear();
  LOG_DBG("WRS", "Cleared all watched repos");
  saveToFile();
}
