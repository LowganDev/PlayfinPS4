#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <memory>
#include <mutex>
#include <string>

#include "app/settings.hpp"
#include "app/shell/library_query.hpp"
#include "jellyfin/client.hpp"
#include "jellyfin/store.hpp"
#include "net/http.hpp"

namespace playfin::app {

class MusicPlayer;

// What the shell screens share: the HTTP client, servers.json, settings.json and the client of the current
// server/user.
// Main thread, except client() and fetchImage() (any thread).
class AppSession {
public:
    // settings.json is read here (defaults without a path; nothing is then written).
    AppSession(net::HttpConfig http, std::string storePath, std::string settingsPath = {});
    ~AppSession();
    AppSession(const AppSession&) = delete;
    AppSession& operator=(const AppSession&) = delete;

    // The music queue (created on first use). Stopped when the server or the user changes.
    MusicPlayer& music();
    // Every frame: updates the music when there is some; true when what it shows changed.
    bool updateMusic();
    // Work for the worker pool that nothing waits for (reports, closing a live stream); inline in test mode.
    void submit(std::function<void()> job);

    // Without a data folder, or when servers.json cannot be read, nothing is ever written (the file is kept).
    core::Result<core::Unit> load();
    bool save();

    const net::HttpClient& http() const { return http_; }
    jellyfin::StoreData& data() { return data_; }

    void useServer(const std::string& serverId, const std::string& baseUrl);
    // A server that answered at `connection.baseUrl`: stored (pin kept for https) and used.
    void openServer(const jellyfin::Connection& connection, const std::string& pin);
    // The current address was already known for this server, so its saved tokens may be sent there.
    bool addressKnown() const { return addressKnown_; }
    jellyfin::StoredServer* server();
    std::shared_ptr<const jellyfin::Client> client() const;

    void signedIn(const jellyfin::AuthResult& auth);
    void useStoredUser(const jellyfin::StoredUser& user);
    void forgetToken();

    Settings& settings() { return settings_; }
    bool saveSettings();

    core::Result<std::string> fetchImage(const std::string& url, const core::CancelToken& token) const;

    // The clock of the Live TV guide (UTC seconds) and the console's distance to UTC; fixed in tests.
    std::int64_t now() const;
    int utcOffsetMinutes(std::int64_t utc) const;
    std::optional<std::int64_t> fixedNow;
    std::optional<int> fixedUtcOffset;

    std::map<std::string, LibraryChoice> libraryChoices;  // by library id, for this session
    std::string languageAtStart;  // settings().language when the app started (the UI's language until a restart)
    std::string noticesFile;      // THIRD_PARTY_LICENSES: copyright lines, credits, where the sources are
    std::string licensesDir;      // resources/licenses: the manifest and the licence texts

    bool synchronous = false;     // test mode: network jobs run inline (deterministic screenshots)
    bool frozenPlayback = false;  // script tests: music pauses at its first moment (deterministic screenshots)
    bool discovery = true;

private:
    jellyfin::Session sessionFor(const std::string& baseUrl) const;
    void stopMusic();
    void setClient(std::shared_ptr<const jellyfin::Client> client);

    net::HttpClient http_;
    std::string storePath_;
    std::string settingsPath_;
    std::unique_ptr<MusicPlayer> music_;
    Settings settings_;
    jellyfin::ServerStore store_;
    bool storeUsable_ = false;  // loaded from a readable file: saving may replace it
    bool addressKnown_ = false;
    std::string pendingPin_;    // pin trusted for an address not stored yet
    jellyfin::StoreData data_;
    mutable std::mutex mutex_;
    std::shared_ptr<const jellyfin::Client> client_;
    std::string serverId_;
    std::string baseUrl_;
};

enum class StartTarget { Servers, Home };
// Home when the last server has an address and the last user a token (the session then uses them).
StartTarget startTarget(AppSession& session);

}  // namespace playfin::app
