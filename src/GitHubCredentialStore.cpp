#include "GitHubCredentialStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

GitHubCredentialStore GitHubCredentialStore::instance;

namespace {
constexpr char GITHUB_FILE_JSON[] = "/.crosspoint/github.json";
constexpr char DEFAULT_COPILOT_BOT[] = "Copilot";
}  // namespace

bool GitHubCredentialStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveGitHub(*this, GITHUB_FILE_JSON);
}

bool GitHubCredentialStore::loadFromFile() {
  if (!Storage.exists(GITHUB_FILE_JSON)) {
    // No file yet — leave defaults in place. Not an error.
    if (copilotBot.empty()) copilotBot = DEFAULT_COPILOT_BOT;
    return false;
  }

  String json = Storage.readFile(GITHUB_FILE_JSON);
  if (json.isEmpty()) {
    return false;
  }

  bool resave = false;
  bool ok = JsonSettingsIO::loadGitHub(*this, json.c_str(), &resave);
  if (ok && copilotBot.empty()) copilotBot = DEFAULT_COPILOT_BOT;
  if (ok && resave) {
    LOG_DBG("GHS", "Resaving GitHub credentials with obfuscated token");
    saveToFile();
  }
  return ok;
}

bool GitHubCredentialStore::setToken(const std::string& newToken) {
  if (newToken == token) {
    // No change — skip the SPIFFS write per the throttling rule.
    return false;
  }
  token = newToken;
  // Clearing the token also clears the cached login, since it no longer
  // refers to any validated identity.
  if (token.empty()) login.clear();
  // Note: deliberately do NOT log token contents.
  LOG_DBG("GHS", "Token %s", token.empty() ? "cleared" : "updated");
  saveToFile();
  return true;
}

void GitHubCredentialStore::clearToken() { setToken(""); }

bool GitHubCredentialStore::setLogin(const std::string& newLogin) {
  if (newLogin == login) return false;
  login = newLogin;
  LOG_DBG("GHS", "Cached login: %s", login.c_str());
  saveToFile();
  return true;
}

bool GitHubCredentialStore::setCopilotBot(const std::string& handle) {
  std::string normalized = handle.empty() ? std::string(DEFAULT_COPILOT_BOT) : handle;
  if (normalized == copilotBot) return false;
  copilotBot = std::move(normalized);
  LOG_DBG("GHS", "Copilot bot handle: %s", copilotBot.c_str());
  saveToFile();
  return true;
}

void GitHubCredentialStore::forgetAll() {
  token.clear();
  login.clear();
  copilotBot = DEFAULT_COPILOT_BOT;
  LOG_DBG("GHS", "Forgot all GitHub credentials");
  saveToFile();
}
