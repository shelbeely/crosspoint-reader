#pragma once

class CrossPointSettings;
class CrossPointState;
class WifiCredentialStore;
class KOReaderCredentialStore;
class RecentBooksStore;
class OpdsServerStore;
class GitHubCredentialStore;
class WatchedReposStore;

namespace JsonSettingsIO {

// CrossPointSettings
bool saveSettings(const CrossPointSettings& s, const char* path);
bool loadSettings(CrossPointSettings& s, const char* json, bool* needsResave = nullptr);

// CrossPointState
bool saveState(const CrossPointState& s, const char* path);
bool loadState(CrossPointState& s, const char* json);

// WifiCredentialStore
bool saveWifi(const WifiCredentialStore& store, const char* path);
bool loadWifi(WifiCredentialStore& store, const char* json, bool* needsResave = nullptr);

// KOReaderCredentialStore
bool saveKOReader(const KOReaderCredentialStore& store, const char* path);
bool loadKOReader(KOReaderCredentialStore& store, const char* json, bool* needsResave = nullptr);

// RecentBooksStore
bool saveRecentBooks(const RecentBooksStore& store, const char* path);
bool loadRecentBooks(RecentBooksStore& store, const char* json);

// OpdsServerStore
bool saveOpds(const OpdsServerStore& store, const char* path);
bool loadOpds(OpdsServerStore& store, const char* json, bool* needsResave = nullptr);

// GitHubCredentialStore (Phase 2 of GitHub Companion mode)
bool saveGitHub(const GitHubCredentialStore& store, const char* path);
bool loadGitHub(GitHubCredentialStore& store, const char* json, bool* needsResave = nullptr);

// WatchedReposStore (Phase 2 of GitHub Companion mode)
bool saveWatchedRepos(const WatchedReposStore& store, const char* path);
bool loadWatchedRepos(WatchedReposStore& store, const char* json);

}  // namespace JsonSettingsIO
