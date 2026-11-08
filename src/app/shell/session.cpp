#include "app/shell/session.hpp"

#include <algorithm>
#include <ctime>

#include "app/shell/downloads.hpp"
#include "app/shell/music_player.hpp"
#include "app/shell/tv_guide.hpp"
#include "core/data_dir.hpp"
#include "core/file_io.hpp"
#include "core/log.hpp"
#include "core/version.hpp"
#include "net/url.hpp"
#include "platform/platform.hpp"
#include "ui/application.hpp"

namespace playfin::app {

AppSession::AppSession(net::HttpConfig http, std::string storePath, std::string settingsPath)
    : http_(std::move(http)), storePath_(storePath), settingsPath_(std::move(settingsPath)), store_(std::move(storePath)) {
    if (!settingsPath_.empty()) {
        settings_ = loadSettings(settingsPath_);
    }
    languageAtStart = settings_.language;
}

AppSession::~AppSession() { stopTransfers(); }

core::WorkerPool& AppSession::transfers() {
    if (!transfers_) {
        transfers_ = std::make_unique<core::WorkerPool>(1);
    }
    return *transfers_;
}

TvGuide& AppSession::tvGuide() {
    if (!tvGuide_) {
        tvGuide_ = std::make_unique<TvGuide>(*this);
    }
    return *tvGuide_;
}

void AppSession::stopTransfers() {
    if (downloads_) {
        downloads_->stop();
    }
    if (tvGuide_) {
        tvGuide_->stop();
    }
    if (!transfers_) {
        return;
    }
    if (!transfers_->finish(std::chrono::seconds(3))) {
        // a transfer that does not hear its cancel: left running rather than waited for (the app is closing)
        PF_LOG_WARN("exit: a transfer still running after 3 s, left behind");
        static_cast<void>(transfers_.release());
        return;
    }
    transfers_.reset();
}

// before the client changes: the stop is reported to the server the music came from
void AppSession::stopMusic() {
    if (music_) {
        music_->stop();
    }
}

void AppSession::submit(std::function<void()> job) {
    ui::Application* app = ui::Application::current();
    if (synchronous || app == nullptr) {
        job();
    } else {
        app->pool().submit(std::move(job));
    }
}

MusicPlayer& AppSession::music() {
    if (!music_) {
        music_ = std::make_unique<MusicPlayer>(*this);
    }
    return *music_;
}

Downloads& AppSession::downloads() {
    if (!downloads_) {
        downloads_ = std::make_unique<Downloads>(*this);
        if (downloadsFreeSpace) {
            downloads_->freeSpace = downloadsFreeSpace;
        }
        downloads_->load();
    }
    return *downloads_;
}

std::int64_t AppSession::now() const {
    return fixedNow ? *fixedNow : static_cast<std::int64_t>(std::time(nullptr));
}

int AppSession::utcOffsetMinutes(std::int64_t utc) const {
    return fixedUtcOffset ? *fixedUtcOffset : platform::utcOffsetMinutes(utc);
}

bool AppSession::updateMusic() {
    if (!music_) {
        return false;
    }
    const unsigned before = music_->changes();
    music_->update();
    return music_->changes() != before;
}

bool AppSession::saveSettings() { return !settingsPath_.empty() && app::saveSettings(settingsPath_, settings_); }

core::Result<core::Unit> AppSession::load() {
    storeUsable_ = false;
    data_ = jellyfin::StoreData{};
    if (storePath_.empty()) {
        data_.deviceId = jellyfin::newDeviceId();  // this run only
        return core::makeError(core::ErrorKind::Storage, "no data folder");
    }
    auto loaded = store_.load();
    if (!loaded.ok()) {
        PF_LOG_ERROR("servers.json: %s", loaded.error().message.c_str());
        data_.deviceId = jellyfin::newDeviceId();  // the file is kept as it is: never overwritten
        return loaded.error();
    }
    data_ = std::move(loaded.value());
    storeUsable_ = true;
    return core::Unit{};
}

bool AppSession::save() {
    if (!storeUsable_) {
        return false;
    }
    const auto saved = store_.save(data_);
    if (!saved.ok()) {
        PF_LOG_ERROR("cannot save servers.json: %s", saved.error().message.c_str());
        return false;
    }
    return true;
}

jellyfin::Session AppSession::sessionFor(const std::string& baseUrl) const {
    jellyfin::Session session;
    session.baseUrl = baseUrl;
    session.deviceId = data_.deviceId;
    session.deviceName = jellyfin::defaultDeviceName();
    for (const auto& server : data_.servers) {
        if (server.id == serverId_) {
            session.pin = server.pin;
        }
    }
    return session;
}

void AppSession::setClient(std::shared_ptr<const jellyfin::Client> client) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_ = std::move(client);
}

std::shared_ptr<const jellyfin::Client> AppSession::client() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return client_;
}

void AppSession::openServer(const jellyfin::Connection& connection, const std::string& pin) {
    stopMusic();
    jellyfin::StoredServer* known = jellyfin::findServer(data_, connection.info.id);
    const bool fresh = known == nullptr;
    jellyfin::StoredServer& server = jellyfin::upsertServer(data_, connection.info.id);
    if (fresh) {
        server.name = connection.info.name;
    }
    // A new address for a known server is only remembered after a sign-in there: anyone can answer with a
    // server's id, and the saved tokens must not follow.
    const bool knownUrl = std::find(server.urls.begin(), server.urls.end(), connection.baseUrl) != server.urls.end();
    if (fresh || knownUrl) {
        jellyfin::rememberUrl(server, connection.baseUrl);
        if (!pin.empty() && connection.baseUrl.rfind("https://", 0) == 0) {
            server.pin = pin;
        }
    }
    pendingPin_ = pin;
    save();
    useServer(connection.info.id, connection.baseUrl);
}

void AppSession::useServer(const std::string& serverId, const std::string& baseUrl) {
    stopMusic();
    serverId_ = serverId;
    baseUrl_ = baseUrl;
    const jellyfin::StoredServer* known = jellyfin::findServer(data_, serverId);
    addressKnown_ = known != nullptr &&
                    std::find(known->urls.begin(), known->urls.end(), baseUrl) != known->urls.end();
    data_.lastServerId = serverId;
    setClient(std::make_shared<const jellyfin::Client>(http_, sessionFor(baseUrl)));
}

jellyfin::StoredServer* AppSession::server() { return jellyfin::findServer(data_, serverId_); }

void AppSession::signedIn(const jellyfin::AuthResult& auth) {
    stopMusic();
    jellyfin::StoredServer& stored = jellyfin::upsertServer(data_, serverId_);
    if (!addressKnown_) {  // signed in here: the address is now trusted for this server
        jellyfin::rememberUrl(stored, baseUrl_);
        if (!pendingPin_.empty() && baseUrl_.rfind("https://", 0) == 0) {
            stored.pin = pendingPin_;
        }
        addressKnown_ = true;
    }
    jellyfin::StoredUser& user = jellyfin::upsertUser(stored, auth.user.id);
    user.name = auth.user.name;
    user.token = auth.token;
    data_.lastServerId = serverId_;
    data_.lastUserId = auth.user.id;
    rememberProfile();
    save();
    const jellyfin::StoredUser copy = user;  // `user` points into data_.servers
    useStoredUser(copy);
}

bool AppSession::rememberProfile() {
    if (consoleUser.empty() || data_.lastServerId.empty()) {
        return false;
    }
    for (jellyfin::ConsoleProfile& profile : data_.profiles) {
        if (profile.consoleUser == consoleUser) {
            const bool changed = profile.serverId != data_.lastServerId || profile.userId != data_.lastUserId;
            profile.serverId = data_.lastServerId;
            profile.userId = data_.lastUserId;
            return changed;
        }
    }
    data_.profiles.push_back({consoleUser, data_.lastServerId, data_.lastUserId});
    return true;
}

void AppSession::shareSignIn(bool share) {
    data_.sharedServerId = share ? data_.lastServerId : std::string();
    data_.sharedUserId = share ? data_.lastUserId : std::string();
    save();
}

bool AppSession::signInShared() const {
    return !data_.sharedServerId.empty() && data_.sharedServerId == data_.lastServerId &&
           data_.sharedUserId == data_.lastUserId;
}

void AppSession::useStoredUser(const jellyfin::StoredUser& user) {
    stopMusic();
    jellyfin::Session session = sessionFor(baseUrl_);
    session.token = user.token;
    session.userId = user.id;
    // a saved user chosen: the next start goes there (user ids are per server), for this PS4 account too (another
    // account may have chosen the same user last, so the profile is checked even when the last user is unchanged)
    const bool userChanged = data_.lastUserId != user.id;
    data_.lastUserId = user.id;
    if (rememberProfile() || userChanged) {
        save();
    }
    setClient(std::make_shared<const jellyfin::Client>(http_, session));
}

void AppSession::forgetToken() {
    stopMusic();
    if (jellyfin::StoredServer* stored = server()) {
        for (auto& user : stored->users) {
            if (user.id == data_.lastUserId) {
                user.token.clear();
            }
        }
    }
    save();
    setClient(std::make_shared<const jellyfin::Client>(http_, sessionFor(baseUrl_)));
}

core::Result<std::string> AppSession::fetchImage(const std::string& url, const core::CancelToken& token) const {
    // a download's picture: read from the downloads folder only
    if (url.rfind("file://", 0) == 0) {
        const auto folder = core::dataPath("downloads");
        const std::string path = url.substr(7);
        if (!folder || path.rfind(*folder + "/", 0) != 0 || path.find("..") != std::string::npos) {
            return core::makeError(core::ErrorKind::BadResponse, "not a download's picture");
        }
        std::string bytes;
        if (core::readSmallFile(path, 8 * 1024 * 1024, bytes) != core::ReadStatus::Ok) {
            return core::makeError(core::ErrorKind::NotFound, "no picture");
        }
        return bytes;
    }
    const auto current = client();
    if (!current) {
        return core::makeError(core::ErrorKind::Unauthorized, "no server");
    }
    return current->fetch(url, token);
}

StartTarget startTarget(AppSession& session) {
    jellyfin::StoreData& data = session.data();
    // the PS4 account's own user; the first account of an installation from before 2.0 keeps its sign-in; an
    // account without one takes the shared sign-in, else chooses
    if (!session.consoleUser.empty()) {
        const jellyfin::ConsoleProfile* mine = nullptr;
        for (const auto& profile : data.profiles) {
            mine = profile.consoleUser == session.consoleUser ? &profile : mine;
        }
        if (mine != nullptr) {
            data.lastServerId = mine->serverId;
            data.lastUserId = mine->userId;
        } else if (data.profiles.empty() && !data.lastUserId.empty()) {
            session.rememberProfile();
            session.save();
        } else if (!data.sharedServerId.empty()) {
            data.lastServerId = data.sharedServerId;
            data.lastUserId = data.sharedUserId;
        } else {
            return StartTarget::Servers;
        }
    }
    jellyfin::StoredServer* server = jellyfin::findServer(data, data.lastServerId);
    if (server == nullptr) {
        return StartTarget::Servers;
    }
    const std::string url = !server->lastUrl.empty() ? server->lastUrl
                                                     : (server->urls.empty() ? std::string() : server->urls.front());
    if (!net::parseUrl(url)) {
        return StartTarget::Servers;
    }
    for (const auto& user : server->users) {
        if (user.id == data.lastUserId && !user.token.empty()) {
            session.useServer(server->id, url);
            session.useStoredUser(user);
            return StartTarget::Home;
        }
    }
    return StartTarget::Servers;
}

}  // namespace playfin::app
