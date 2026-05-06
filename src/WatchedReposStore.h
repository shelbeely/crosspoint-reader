#pragma once
#include <string>
#include <vector>

struct WatchedRepo {
  std::string owner;
  std::string repo;
};

class WatchedReposStore;
namespace JsonSettingsIO {
bool saveWatchedRepos(const WatchedReposStore& store, const char* path);
bool loadWatchedRepos(WatchedReposStore& store, const char* json);
}  // namespace JsonSettingsIO

/**
 * Singleton holding the small list of `owner/repo` pairs the device polls
 * for CI status, failed runs, and per-repo issue queues. Modelled on
 * `OpdsServerStore` — same singleton + JsonSettingsIO + write-throttling
 * patterns.
 *
 * The plan in `SCOPE.md` deliberately rejects an "all repos I have access
 * to" mode: enumerating every repo for an authenticated user would blow
 * both the device memory budget (single 48 KB framebuffer, ~380 KB total
 * RAM) and the GitHub REST rate limit (5000 req/h per token, but we
 * issue many requests per refresh). The watchlist is the only way the
 * companion learns which repos to query.
 *
 * Capacity is capped at MAX_REPOS to bound per-poll request count and
 * SPIFFS footprint. The plan envisages "a small list" — a 16-entry cap
 * is generous for that intent.
 */
class WatchedReposStore {
 private:
  static WatchedReposStore instance;
  std::vector<WatchedRepo> repos;

  WatchedReposStore() = default;

  friend bool JsonSettingsIO::saveWatchedRepos(const WatchedReposStore&, const char*);
  friend bool JsonSettingsIO::loadWatchedRepos(WatchedReposStore&, const char*);

 public:
  static constexpr size_t MAX_REPOS = 16;

  WatchedReposStore(const WatchedReposStore&) = delete;
  WatchedReposStore& operator=(const WatchedReposStore&) = delete;

  static WatchedReposStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();

  // Accepts either a `WatchedRepo{owner, repo}` or the convenience
  // `addFromSlug("owner/repo")` form (parsing fails on malformed input).
  // Returns false if the cap is reached, the entry is malformed, or it
  // is already present (write throttling).
  bool addRepo(const WatchedRepo& repo);
  bool addFromSlug(const std::string& slug);
  bool removeRepo(size_t index);
  bool removeBySlug(const std::string& slug);

  bool contains(const std::string& owner, const std::string& repo) const;

  const std::vector<WatchedRepo>& getRepos() const { return repos; }
  const WatchedRepo* getRepo(size_t index) const;
  size_t getCount() const { return repos.size(); }
  bool empty() const { return repos.empty(); }

  void clearAll();
};

#define WATCHED_REPOS WatchedReposStore::getInstance()
