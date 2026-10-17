#include "app/shell/session.hpp"

#include <algorithm>
#include <ctime>

#include "app/shell/music_player.hpp"
#include "core/log.hpp"
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

AppSession::~AppSession() = default;

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
    save();
    const jellyfin::StoredUser copy = user;  // `user` points into data_.servers
    useStoredUser(copy);
}

void AppSession::useStoredUser(const jellyfin::StoredUser& user) {
    stopMusic();
    jellyfin::Session session = sessionFor(baseUrl_);
    session.token = user.token;
    session.userId = user.id;
    if (data_.lastUserId != user.id) {  // a saved user chosen: the next start goes there (user ids are per server)
        data_.lastUserId = user.id;
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
    const auto current = client();
    if (!current) {
        return core::makeError(core::ErrorKind::Unauthorized, "no server");
    }
    return current->fetch(url, token);
}

StartTarget startTarget(AppSession& session) {
    jellyfin::StoreData& data = session.data();
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

