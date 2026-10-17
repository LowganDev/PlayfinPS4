#include "jellyfin/client.hpp"

#include <utility>

#include "core/version.hpp"
#include "jellyfin/json.hpp"

namespace playfin::jellyfin {

namespace {

constexpr const char* kFields = "Overview,PrimaryImageAspectRatio,ParentId,Genres,DateCreated,ChildCount";
constexpr const char* kImageTypes = "Primary,Backdrop,Thumb,Logo";

std::string clean(std::string_view value) {
    std::string out;
    for (char c : value) {
        if (c != '"' && c != ',' && c != '\r' && c != '\n') {
            out += c;
        }
    }
    return out;
}

std::string join(const std::vector<std::string>& values) {
    std::string out;
    for (const auto& value : values) {
        if (!out.empty()) {
            out += ',';
        }
        out += value;
    }
    return out;
}

template <typename T, typename Parse>
core::Result<T> parsed(const core::Result<net::HttpResponse>& response, Parse parse) {
    if (!response.ok()) {
        return response.error();
    }
    return parse(response.value().body);
}

core::Result<core::Unit> unit(const core::Result<net::HttpResponse>& response) {
    if (!response.ok()) {
        return response.error();
    }
    return core::Unit{};
}

// Error reported when no candidate answers: the most useful to the user wins. An untrusted certificate comes first
// (the UI can ask to trust it), but only once every other candidate has been tried.
int rank(core::ErrorKind kind) {
    switch (kind) {
    case core::ErrorKind::CertificateUntrusted:
        return 5;
    case core::ErrorKind::BadResponse:
        return 4;
    case core::ErrorKind::Unauthorized:
    case core::ErrorKind::NotFound:
    case core::ErrorKind::Server:
        return 3;
    case core::ErrorKind::Tls:
        return 2;
    case core::ErrorKind::Timeout:
        return 1;
    default:
        return 0;
    }
}

}  // namespace

const char* imageTypeName(ImageType type) {
    switch (type) {
    case ImageType::Primary:
        return "Primary";
    case ImageType::Thumb:
        return "Thumb";
    case ImageType::Backdrop:
        return "Backdrop";
    case ImageType::Logo:
        return "Logo";
    }
    return "Primary";
}

std::string authorizationHeader(const Session& session) {
    std::string header = "MediaBrowser Client=\"Playfin\", Device=\"" + clean(session.deviceName) + "\", DeviceId=\"" +
                         clean(session.deviceId) + "\", Version=\"" + core::version() + "\"";
    if (!session.token.empty()) {
        header += ", Token=\"" + clean(session.token) + "\"";
    }
    return header;
}

std::string defaultDeviceName() {
#ifdef PLAYFIN_PS4
    return "PlayStation 4";
#else
    return "Linux";
#endif
}

core::Result<Connection> connect(const net::HttpClient& http, std::string_view address, const std::string& pin,
                                 const core::CancelToken& token) {
    const auto candidates = net::candidateUrls(address);
    if (candidates.empty()) {
        return core::makeError(core::ErrorKind::InvalidAddress, "this is not a server address");
    }
    return connectCandidates(http, candidates, pin, token);
}

core::Result<Connection> connectCandidates(const net::HttpClient& http, const std::vector<std::string>& candidates,
                                           const std::string& pin, const core::CancelToken& token) {
    core::Error best = core::makeError(core::ErrorKind::Network, "cannot reach the server");
    int bestRank = -1;
    for (const std::string& url : candidates) {
        if (token.cancelled()) {
            return core::makeError(core::ErrorKind::Cancelled, "cancelled");
        }
        Session session;
        session.baseUrl = url;
        session.pin = pin;
        session.connectTimeoutMs = 3000;
        const auto info = Client(http, session).publicInfo(token);
        if (info.ok()) {
            const auto version = parseVersion(info.value().version);
            if (!version || !versionAtLeast(*version, 10, 10)) {
                return core::makeError(core::ErrorKind::UnsupportedServer, "Jellyfin " + info.value().version);
            }
            return Connection{url, info.value()};
        }
        const core::Error& error = info.error();
        if (error.kind == core::ErrorKind::Cancelled) {
            return error;
        }
        if (rank(error.kind) > bestRank) {
            best = error;
            bestRank = rank(error.kind);
        }
    }
    return best;
}

Client::Client(const net::HttpClient& http, Session session) : http_(http), session_(std::move(session)) {}

void Client::setToken(std::string token, std::string userId) {
    session_.token = std::move(token);
    session_.userId = std::move(userId);
}

net::Query Client::userQuery() const {
    net::Query query;
    query.add("userId", session_.userId);
    return query;
}

core::Result<net::HttpResponse> Client::request(const char* method, const std::string& pathAndQuery,
                                                const std::string& body, const core::CancelToken& token,
                                                std::size_t maxBodyBytes) const {
    net::HttpRequest req;
    req.maxBodyBytes = maxBodyBytes;
    req.method = method;
    req.url = session_.baseUrl + pathAndQuery;
    req.connectTimeoutMs = session_.connectTimeoutMs;
    req.headers = {{"Authorization", authorizationHeader(session_)}, {"Accept", "application/json"}};
    if (!body.empty()) {
        req.headers.emplace_back("Content-Type", "application/json");
    }
    req.body = body;
    const bool secure = session_.baseUrl.rfind("https://", 0) == 0;
    auto response = http_.send(req, secure ? session_.pin : std::string(), token);
    if (!response.ok()) {
        return response;
    }
    const long status = response.value().status;
    if (status >= 200 && status < 300) {
        return response;
    }
    const int code = static_cast<int>(status);
    if (status == 401 || status == 403) {
        return core::makeError(core::ErrorKind::Unauthorized, "not authorized", code);
    }
    if (status == 404) {
        return core::makeError(core::ErrorKind::NotFound, "not found", code);
    }
    return core::makeError(core::ErrorKind::Server, "HTTP " + std::to_string(code), code);
}

core::Result<ServerInfo> Client::publicInfo(const core::CancelToken& token) const {
    const auto response = request("GET", "/System/Info/Public", {}, token);
    if (!response.ok() && response.error().kind == core::ErrorKind::NotFound) {
        return core::makeError(core::ErrorKind::BadResponse, "not a Jellyfin server", 404);
    }
    return parsed<ServerInfo>(response, parseServerInfo);
}

core::Result<std::vector<PublicUser>> Client::publicUsers(const core::CancelToken& token) const {
    return parsed<std::vector<PublicUser>>(request("GET", "/Users/Public", {}, token), parsePublicUsers);
}

core::Result<AuthResult> Client::signIn(const std::string& user, const std::string& password,
                                        const core::CancelToken& token) const {
    const std::string body = json::writeObject({{"Username", user}, {"Pw", password}});
    return parsed<AuthResult>(request("POST", "/Users/AuthenticateByName", body, token), parseAuth);
}

core::Result<bool> Client::quickConnectEnabled(const core::CancelToken& token) const {
    return parsed<bool>(request("GET", "/QuickConnect/Enabled", {}, token), parseBool);
}

core::Result<QuickConnectState> Client::quickConnectStart(const core::CancelToken& token) const {
    return parsed<QuickConnectState>(request("POST", "/QuickConnect/Initiate", {}, token), parseQuickConnect);
}

core::Result<QuickConnectState> Client::quickConnectPoll(const std::string& secret,
                                                         const core::CancelToken& token) const {
    const std::string path = "/QuickConnect/Connect" + net::Query().add("secret", secret).str();
    return parsed<QuickConnectState>(request("GET", path, {}, token), parseQuickConnect);
}

core::Result<AuthResult> Client::quickConnectFinish(const std::string& secret, const core::CancelToken& token) const {
    const std::string body = json::writeObject({{"Secret", secret}});
    return parsed<AuthResult>(request("POST", "/Users/AuthenticateWithQuickConnect", body, token), parseAuth);
}

core::Result<UserInfo> Client::currentUser(const core::CancelToken& token) const {
    return parsed<UserInfo>(request("GET", "/Users/Me", {}, token), parseUser);
}

core::Result<core::Unit> Client::signOut(const core::CancelToken& token) const {
    return unit(request("POST", "/Sessions/Logout", {}, token));
}

core::Result<ItemPage> Client::libraries(const core::CancelToken& token) const {
    return parsed<ItemPage>(request("GET", "/UserViews" + userQuery().str(), {}, token), parseItemPage);
}

core::Result<ItemPage> Client::latest(const std::string& libraryId, int limit, const core::CancelToken& token) const {
    net::Query query = userQuery();
    query.add("parentId", libraryId).add("limit", limit).add("fields", kFields).add("enableImageTypes", kImageTypes);
    return parsed<ItemPage>(request("GET", "/Items/Latest" + query.str(), {}, token), parseItemPage);
}

core::Result<ItemPage> Client::resume(int limit, const core::CancelToken& token) const {
    net::Query query = userQuery();
    query.add("limit", limit).add("mediaTypes", "Video").add("fields", kFields).add("enableImageTypes", kImageTypes);
    return parsed<ItemPage>(request("GET", "/UserItems/Resume" + query.str(), {}, token), parseItemPage);
}

core::Result<ItemPage> Client::nextUp(int limit, const core::CancelToken& token) const {
    net::Query query = userQuery();
    query.add("limit", limit).add("fields", kFields).add("enableImageTypes", kImageTypes);
    return parsed<ItemPage>(request("GET", "/Shows/NextUp" + query.str(), {}, token), parseItemPage);
}

core::Result<ItemPage> Client::items(const ItemQuery& itemQuery, const core::CancelToken& token) const {
    net::Query query = userQuery();
    if (!itemQuery.parentId.empty()) {
        query.add("parentId", itemQuery.parentId);
    }
    if (!itemQuery.includeTypes.empty()) {
        query.add("includeItemTypes", join(itemQuery.includeTypes));
    }
    query.add("sortBy", itemQuery.sortBy).add("sortOrder", itemQuery.descending ? "Descending" : "Ascending");
    if (!itemQuery.filters.empty()) {
        query.add("filters", join(itemQuery.filters));
    }
    if (!itemQuery.searchTerm.empty()) {
        query.add("searchTerm", itemQuery.searchTerm);
    }
    if (!itemQuery.albumArtistId.empty()) {
        query.add("albumArtistIds", itemQuery.albumArtistId);
    }
    query.add("startIndex", itemQuery.startIndex)
        .add("limit", itemQuery.limit)
        .add("recursive", itemQuery.recursive ? "true" : "false")
        .add("fields", kFields)
        .add("enableImageTypes", kImageTypes);
    return parsed<ItemPage>(request("GET", "/Items" + query.str(), {}, token), parseItemPage);
}

core::Result<Item> Client::item(const std::string& id, const core::CancelToken& token) const {
    net::Query query = userQuery();
    query.add("fields", kFields);
    return parsed<Item>(request("GET", "/Items/" + net::urlEncode(id) + query.str(), {}, token), parseItem);
}

core::Result<ItemPage> Client::seasons(const std::string& seriesId, const core::CancelToken& token) const {
    net::Query query = userQuery();
    query.add("fields", kFields).add("enableImageTypes", kImageTypes);
    return parsed<ItemPage>(request("GET", "/Shows/" + net::urlEncode(seriesId) + "/Seasons" + query.str(), {}, token),
                            parseItemPage);
}

core::Result<ItemPage> Client::episodes(const std::string& seriesId, const std::string& seasonId,
                                        const core::CancelToken& token) const {
    net::Query query = userQuery();
    query.add("seasonId", seasonId).add("fields", kFields).add("enableImageTypes", kImageTypes);
    return parsed<ItemPage>(
        request("GET", "/Shows/" + net::urlEncode(seriesId) + "/Episodes" + query.str(), {}, token), parseItemPage);
}

core::Result<core::Unit> Client::setFavorite(const std::string& id, bool favorite,
                                             const core::CancelToken& token) const {
    return unit(request(favorite ? "POST" : "DELETE", "/UserFavoriteItems/" + net::urlEncode(id) + userQuery().str(),
                        {}, token));
}

core::Result<core::Unit> Client::setPlayed(const std::string& id, bool played, const core::CancelToken& token) const {
    return unit(
        request(played ? "POST" : "DELETE", "/UserPlayedItems/" + net::urlEncode(id) + userQuery().str(), {}, token));
}

core::Result<ItemPage> Client::artists(const std::string& libraryId, int startIndex, int limit,
                                       const core::CancelToken& token) const {
    net::Query query = userQuery();
    query.add("parentId", libraryId)
        .add("sortBy", "SortName")
        .add("startIndex", startIndex)
        .add("limit", limit)
        .add("fields", kFields)
        .add("enableImageTypes", kImageTypes);
    return parsed<ItemPage>(request("GET", "/Artists/AlbumArtists" + query.str(), {}, token), parseItemPage);
}

core::Result<ItemPage> Client::playlistItems(const std::string& playlistId, const core::CancelToken& token) const {
    net::Query query = userQuery();
    query.add("fields", kFields).add("enableImageTypes", kImageTypes);
    return parsed<ItemPage>(
        request("GET", "/Playlists/" + net::urlEncode(playlistId) + "/Items" + query.str(), {}, token), parseItemPage);
}

core::Result<ItemPage> Client::liveTvChannels(const core::CancelToken& token) const {
    net::Query query = userQuery();
    query.add("addCurrentProgram", "true").add("enableImageTypes", "Primary").add("fields", "Overview");
    return parsed<ItemPage>(request("GET", "/LiveTv/Channels" + query.str(), {}, token), parseItemPage);
}

core::Result<ItemPage> Client::programs(const std::string& channelId, std::int64_t fromUtc, std::int64_t toUtc,
                                        const core::CancelToken& token) const {
    net::Query query = userQuery();
    query.add("channelIds", channelId)
        .add("minEndDate", utcText(fromUtc))
        .add("maxStartDate", utcText(toUtc))
        .add("sortBy", "StartDate")
        .add("fields", "Overview")
        .add("enableImageTypes", "Primary");
    return parsed<ItemPage>(request("GET", "/LiveTv/Programs" + query.str(), {}, token), parseItemPage);
}

core::Result<ItemPage> Client::airingNow(int limit, const core::CancelToken& token) const {
    net::Query query = userQuery();
    query.add("isAiring", "true").add("limit", limit).add("fields", "Overview").add("enableImageTypes", "Primary");
    return parsed<ItemPage>(request("GET", "/LiveTv/Programs/Recommended" + query.str(), {}, token), parseItemPage);
}

core::Result<ItemPage> Client::recordings(const core::CancelToken& token) const {
    net::Query query = userQuery();
    query.add("fields", kFields).add("enableImageTypes", kImageTypes);
    return parsed<ItemPage>(request("GET", "/LiveTv/Recordings" + query.str(), {}, token), parseItemPage);
}

core::Result<std::vector<Timer>> Client::timers(const core::CancelToken& token) const {
    return parsed<std::vector<Timer>>(request("GET", "/LiveTv/Timers", {}, token), parseTimers);
}

core::Result<core::Unit> Client::createTimer(const std::string& programId, const core::CancelToken& token) const {
    net::Query query;
    query.add("programId", programId);
    auto defaults = request("GET", "/LiveTv/Timers/Defaults" + query.str(), {}, token);
    if (!defaults.ok()) {
        return defaults.error();
    }
    return unit(request("POST", "/LiveTv/Timers", defaults.value().body, token));
}

core::Result<core::Unit> Client::cancelTimer(const std::string& timerId, const core::CancelToken& token) const {
    return unit(request("DELETE", "/LiveTv/Timers/" + net::urlEncode(timerId), {}, token));
}

core::Result<core::Unit> Client::closeLiveStream(const std::string& liveStreamId,
                                                 const core::CancelToken& token) const {
    net::Query query;
    query.add("liveStreamId", liveStreamId);
    return unit(request("POST", "/LiveStreams/Close" + query.str(), {}, token));
}

std::string Client::imageUrl(const std::string& itemId, ImageType type, const std::string& tag, int maxWidth,
                             int maxHeight) const {
    net::Query query;
    query.add("tag", tag).add("maxWidth", maxWidth).add("maxHeight", maxHeight).add("format", "Jpg").add("quality", 90);
    return session_.baseUrl + "/Items/" + net::urlEncode(itemId) + "/Images/" + imageTypeName(type) + query.str();
}

std::string Client::userImageUrl(const std::string& userId, const std::string& tag, int size) const {
    net::Query query;
    query.add("tag", tag).add("maxWidth", size).add("maxHeight", size).add("format", "Jpg").add("quality", 90);
    return session_.baseUrl + "/Users/" + net::urlEncode(userId) + "/Images/Primary" + query.str();
}

core::Result<std::string> Client::fetch(const std::string& url, const core::CancelToken& token) const {
    const std::string prefix = session_.baseUrl + "/";
    if (url.compare(0, prefix.size(), prefix) != 0) {
        return core::makeError(core::ErrorKind::BadResponse, "refusing to send credentials to another server");
    }
    auto response = request("GET", url.substr(session_.baseUrl.size()), {}, token, 0);
    if (!response.ok()) {
        return response.error();
    }
    return std::move(response.value().body);
}

core::Result<PlaybackInfo> Client::playbackInfo(const std::string& itemId, const PlaybackRequest& playback,
                                                const core::CancelToken& token) const {
    json::Writer body;
    body.object();
    body.key("UserId").value(session_.userId);
    body.key("MaxStreamingBitrate").value(playback.maxBitrate);
    body.key("StartTimeTicks").value(playback.startTicks);
    if (playback.audioIndex >= 0) {
        body.key("AudioStreamIndex").value(playback.audioIndex);
    }
    body.key("SubtitleStreamIndex").value(playback.subtitleIndex);
    if (!playback.mediaSourceId.empty()) {
        body.key("MediaSourceId").value(playback.mediaSourceId);
    }
    body.key("EnableDirectPlay").value(!playback.liveTv);
    body.key("EnableDirectStream").value(true);
    body.key("EnableTranscoding").value(true);
    body.key("AllowVideoStreamCopy").value(true);
    body.key("AllowAudioStreamCopy").value(true);
    body.key("AutoOpenLiveStream").value(playback.liveTv);
    if (!playback.deviceProfileJson.empty()) {
        body.key("DeviceProfile").raw(playback.deviceProfileJson);
    }
    body.end();
    net::Query query = userQuery();
    if (playback.liveTv) {
        query.add("autoOpenLiveStream", "true");
    }
    const std::string path = "/Items/" + net::urlEncode(itemId) + "/PlaybackInfo" + query.str();
    return parsed<PlaybackInfo>(request("POST", path, body.str(), token), parsePlaybackInfo);
}

core::Result<std::vector<MediaSegment>> Client::mediaSegments(const std::string& itemId,
                                                              const core::CancelToken& token) const {
    net::Query query;
    query.add("includeSegmentTypes", "Intro").add("includeSegmentTypes", "Outro");
    const auto response = request("GET", "/MediaSegments/" + net::urlEncode(itemId) + query.str(), {}, token);
    if (!response.ok() && response.error().kind == core::ErrorKind::NotFound) {
        return std::vector<MediaSegment>{};
    }
    return parsed<std::vector<MediaSegment>>(response, parseMediaSegments);
}

namespace {

std::string reportBody(const PlaybackState& state) {
    json::Writer body;
    body.object();
    body.key("ItemId").value(state.itemId);
    body.key("MediaSourceId").value(state.mediaSourceId);
    body.key("PlaySessionId").value(state.playSessionId);
    if (!state.liveStreamId.empty()) {
        body.key("LiveStreamId").value(state.liveStreamId);
    }
    body.key("PositionTicks").value(state.positionTicks);
    body.key("IsPaused").value(state.paused);
    body.key("PlayMethod").value(state.transcoding ? "Transcode" : "DirectPlay");
    body.key("AudioStreamIndex").value(state.audioIndex);
    body.key("SubtitleStreamIndex").value(state.subtitleIndex);
    body.key("CanSeek").value(state.liveStreamId.empty());
    body.end();
    return body.str();
}

}  // namespace

core::Result<core::Unit> Client::reportStart(const PlaybackState& state, const core::CancelToken& token) const {
    return unit(request("POST", "/Sessions/Playing", reportBody(state), token));
}

core::Result<core::Unit> Client::reportProgress(const PlaybackState& state, const core::CancelToken& token) const {
    return unit(request("POST", "/Sessions/Playing/Progress", reportBody(state), token));
}

core::Result<core::Unit> Client::reportStopped(const PlaybackState& state, const core::CancelToken& token) const {
    return unit(request("POST", "/Sessions/Playing/Stopped", reportBody(state), token));
}

}  // namespace playfin::jellyfin
