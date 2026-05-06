#pragma once
#include <string>

class GitHubCredentialStore;
namespace JsonSettingsIO {
bool saveGitHub(const GitHubCredentialStore& store, const char* path);
bool loadGitHub(GitHubCredentialStore& store, const char* json, bool* needsResave);
}  // namespace JsonSettingsIO

/**
 * Singleton holding a single GitHub fine-grained personal access token (PAT)
 * plus the cached login of the authenticated user.
 *
 * The PAT is XOR-obfuscated with the device's hardware MAC and base64-encoded
 * before being written to disk via `JsonSettingsIO::saveGitHub`. This is the
 * same not-cryptographic-but-not-trivial obfuscation used by
 * `WifiCredentialStore` and `OpdsServerStore` and ties the stored token to
 * the specific device — a copy of the JSON cannot be decoded on another
 * board or PC.
 *
 * Why a single PAT (no list): the plan in `SCOPE.md` constrains the
 * GitHub Companion to a single GitHub identity. Multi-account would
 * require per-call token selection in every endpoint and double the
 * SPIFFS footprint.
 *
 * Persistence policy:
 *   - All mutators call `saveToFile()` only when the stored value would
 *     actually change (the `CLAUDE.md` "SPIFFS write throttling" rule).
 *   - `setLogin()` is purely a cache of the value returned by `GET /user`
 *     and is updated whenever the token is validated — it has no
 *     security significance and is not obfuscated.
 */
class GitHubCredentialStore {
 private:
  static GitHubCredentialStore instance;

  std::string token;       // PAT in plaintext in memory; obfuscated on disk
  std::string login;       // GitHub login of the authenticated user (cache)
  std::string copilotBot;  // Bot handle to assign issues to (default "Copilot")

  GitHubCredentialStore() = default;

  friend bool JsonSettingsIO::saveGitHub(const GitHubCredentialStore&, const char*);
  friend bool JsonSettingsIO::loadGitHub(GitHubCredentialStore&, const char*, bool*);

 public:
  GitHubCredentialStore(const GitHubCredentialStore&) = delete;
  GitHubCredentialStore& operator=(const GitHubCredentialStore&) = delete;

  static GitHubCredentialStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();

  // Token management. setToken() returns false on no-op (write throttling).
  bool setToken(const std::string& newToken);
  void clearToken();
  bool hasToken() const { return !token.empty(); }
  const std::string& getToken() const { return token; }

  // Cached GitHub login (from GET /user). Updated by GitHubClient on
  // successful validation. Returns false when the new value matches the
  // cached one (write throttling).
  bool setLogin(const std::string& newLogin);
  const std::string& getLogin() const { return login; }

  // Configurable Copilot bot handle used by "Assign to Copilot". Default
  // is "Copilot" — the special login used by GitHub's Copilot Coding
  // Agent. Some orgs may prefer "copilot-swe-agent" or another bot.
  bool setCopilotBot(const std::string& handle);
  const std::string& getCopilotBot() const { return copilotBot; }

  // Forget everything. Used by the planned settings UI's "Forget Token"
  // action; also part of factory-reset behavior documented in
  // USER_GUIDE.md (Phase 5).
  void forgetAll();
};

#define GITHUB_STORE GitHubCredentialStore::getInstance()
