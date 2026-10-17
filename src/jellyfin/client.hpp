#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "core/task.hpp"
#include "jellyfin/models.hpp"
#include "net/http.hpp"
#include "net/url.hpp"

namespace playfin::jellyfin {

// Largest JSON reply accepted from the API (a parsed document costs several times its size). Files read with
// Client::fetch (images, subtitles) use the HTTP client limit instead.
constexpr std::size_t kMaxApiReplyBytes = 8u * 1024u * 1024u;

struct Session {
    std::string baseUrl;     // working address, without trailing slash
    std::string pin;         // "sha256//…" for a trusted self-signed server, else empty
    std::string deviceId;
    std::string deviceName;
    std::string token;       // empty before sign-in
    std::string userId;
    long connectTimeoutMs = 5000;
};

enum class ImageType { Primary, Thumb, Backdrop, Logo };
const char* imageTypeName(ImageType type);

struct ItemQuery {
    std::string parentId;
    std::vector<std::string> includeTypes;
    std::string sortBy = "SortName";
    bool descending = false;
    std::vector<std::string> filters;
    std::string searchTerm;
    std::string albumArtistId;  // albums of an artist
    int startIndex = 0;
    int limit = 100;
    bool recursive = true;
};

struct PlaybackRequest {
    std::string mediaSourceId;      // empty: the server picks
    std::int64_t startTicks = 0;
    int audioIndex = -1;            // -1: the server's default
    int subtitleIndex = -1;         // -1: none
    std::int64_t maxBitrate = 40000000;
    std::string deviceProfileJson;  // what the console can play (player::deviceProfileJson)
    bool liveTv = false;  // a channel: opens its live stream, relayed by the server (no direct play)
};

// What /Sessions/Playing* reports.
struct PlaybackState {
    std::string itemId;
    std::string mediaSourceId;
    std::string playSessionId;
    std::string liveStreamId;  // a live channel
    std::int64_t positionTicks = 0;
    bool paused = false;
    bool transcoding = false;
    int audioIndex = -1;
    int subtitleIndex = -1;
};

struct Connection {
    std::string baseUrl;
    ServerInfo info;
};

// MediaBrowser Client="Playfin", Device=…, DeviceId=…, Version=…[, Token=…] (quotes, commas, newlines stripped)
std::string authorizationHeader(const Session& session);
std::string defaultDeviceName();

// Tries candidateUrls(address) in order (pin applied to https candidates). Stops at the first Jellyfin reply or at
// an unsupported version. An untrusted certificate is returned (the UI asks) only if no candidate answers.
core::Result<Connection> connect(const net::HttpClient& http, std::string_view address, const std::string& pin,
                                 const core::CancelToken& token = {});

// connect() over an explicit list of base URLs.
core::Result<Connection> connectCandidates(const net::HttpClient& http, const std::vector<std::string>& candidates,
                                           const std::string& pin, const core::CancelToken& token = {});

// Jellyfin 10.10 endpoints. Blocking: call from worker threads (core::runAsync).
class Client {
public:
    Client(const net::HttpClient& http, Session session);

    const Session& session() const { return session_; }
    void setToken(std::string token, std::string userId);

    core::Result<ServerInfo> publicInfo(const core::CancelToken& token = {}) const;
    core::Result<std::vector<PublicUser>> publicUsers(const core::CancelToken& token = {}) const;
    core::Result<AuthResult> signIn(const std::string& user, const std::string& password,
                                    const core::CancelToken& token = {}) const;
    core::Result<bool> quickConnectEnabled(const core::CancelToken& token = {}) const;
    core::Result<QuickConnectState> quickConnectStart(const core::CancelToken& token = {}) const;
    core::Result<QuickConnectState> quickConnectPoll(const std::string& secret,
                                                     const core::CancelToken& token = {}) const;
    core::Result<AuthResult> quickConnectFinish(const std::string& secret, const core::CancelToken& token = {}) const;
    core::Result<UserInfo> currentUser(const core::CancelToken& token = {}) const;
    core::Result<core::Unit> signOut(const core::CancelToken& token = {}) const;

    core::Result<ItemPage> libraries(const core::CancelToken& token = {}) const;
    core::Result<ItemPage> latest(const std::string& libraryId, int limit, const core::CancelToken& token = {}) const;
    core::Result<ItemPage> resume(int limit, const core::CancelToken& token = {}) const;
    core::Result<ItemPage> nextUp(int limit, const core::CancelToken& token = {}) const;
    core::Result<ItemPage> items(const ItemQuery& query, const core::CancelToken& token = {}) const;
    core::Result<Item> item(const std::string& id, const core::CancelToken& token = {}) const;
    core::Result<ItemPage> seasons(const std::string& seriesId, const core::CancelToken& token = {}) const;
    core::Result<ItemPage> episodes(const std::string& seriesId, const std::string& seasonId,
                                    const core::CancelToken& token = {}) const;
    core::Result<core::Unit> setFavorite(const std::string& id, bool favorite, const core::CancelToken& token = {}) const;
    core::Result<core::Unit> setPlayed(const std::string& id, bool played, const core::CancelToken& token = {}) const;

    // music
    core::Result<ItemPage> artists(const std::string& libraryId, int startIndex, int limit,
                                   const core::CancelToken& token = {}) const;
    core::Result<ItemPage> playlistItems(const std::string& playlistId, const core::CancelToken& token = {}) const;

    // Live TV
    core::Result<ItemPage> liveTvChannels(const core::CancelToken& token = {}) const;  // with the programme on now
    // programmes of a channel ending after `fromUtc` and starting before `toUtc`, by start time
    core::Result<ItemPage> programs(const std::string& channelId, std::int64_t fromUtc, std::int64_t toUtc,
                                   const core::CancelToken& token = {}) const;
    core::Result<ItemPage> airingNow(int limit, const core::CancelToken& token = {}) const;
    core::Result<ItemPage> recordings(const core::CancelToken& token = {}) const;
    core::Result<std::vector<Timer>> timers(const core::CancelToken& token = {}) const;
    // schedules a programme with the server's default settings (padding, keep…)
    core::Result<core::Unit> createTimer(const std::string& programId, const core::CancelToken& token = {}) const;
    core::Result<core::Unit> cancelTimer(const std::string& timerId, const core::CancelToken& token = {}) const;
    core::Result<core::Unit> closeLiveStream(const std::string& liveStreamId,
                                             const core::CancelToken& token = {}) const;

    core::Result<PlaybackInfo> playbackInfo(const std::string& itemId, const PlaybackRequest& playback,
                                            const core::CancelToken& token = {}) const;
    // Intro and Outro segments; none (not an error) when the server has no segment data.
    core::Result<std::vector<MediaSegment>> mediaSegments(const std::string& itemId,
                                                          const core::CancelToken& token = {}) const;
    core::Result<core::Unit> reportStart(const PlaybackState& state, const core::CancelToken& token = {}) const;
    core::Result<core::Unit> reportProgress(const PlaybackState& state, const core::CancelToken& token = {}) const;
    core::Result<core::Unit> reportStopped(const PlaybackState& state, const core::CancelToken& token = {}) const;

    // Picture URLs carry no token: fetch() sends it in the header. A user's picture is square.
    std::string userImageUrl(const std::string& userId, const std::string& tag, int size) const;
    std::string imageUrl(const std::string& itemId, ImageType type, const std::string& tag, int maxWidth,
                         int maxHeight) const;
    // Authenticated GET of a URL of this server (images); refuses any other host.
    core::Result<std::string> fetch(const std::string& url, const core::CancelToken& token = {}) const;

private:
    core::Result<net::HttpResponse> request(const char* method, const std::string& pathAndQuery,
                                            const std::string& body, const core::CancelToken& token,
                                            std::size_t maxBodyBytes = kMaxApiReplyBytes) const;
    net::Query userQuery() const;

    const net::HttpClient& http_;
    Session session_;
};

}  // namespace playfin::jellyfin
