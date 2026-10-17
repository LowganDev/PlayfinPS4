#include "jellyfin/models.hpp"

#include <climits>
#include <cstdio>
#include <utility>

#include "core/strings.hpp"
#include "jellyfin/json.hpp"

namespace playfin::jellyfin {

namespace {

core::Error badResponse(std::string message) {
    return core::makeError(core::ErrorKind::BadResponse, std::move(message));
}

int toInt(std::int64_t value) {
    if (value < INT_MIN || value > INT_MAX) {
        return 0;
    }
    return static_cast<int>(value);
}

// the first entry's Id of an array of {Name, Id}
std::string firstId(yyjson_val* value, const char* key) {
    yyjson_val* list = json::array(value, key);
    return list != nullptr ? json::string(yyjson_arr_get_first(list), "Id") : std::string();
}

Item parseItemValue(yyjson_val* value, int depth = 0) {
    Item item;
    item.id = json::string(value, "Id");
    item.name = json::string(value, "Name");
    item.serverId = json::string(value, "ServerId");
    item.type = itemTypeFromName(json::string(value, "Type"));
    item.seriesId = json::string(value, "SeriesId");
    item.seriesName = json::string(value, "SeriesName");
    item.seasonId = json::string(value, "SeasonId");
    item.seasonName = json::string(value, "SeasonName");
    item.parentId = json::string(value, "ParentId");
    item.collectionType = json::string(value, "CollectionType");
    item.mediaType = json::string(value, "MediaType");
    item.overview = json::string(value, "Overview");
    item.officialRating = json::string(value, "OfficialRating");
    item.premiereDate = json::string(value, "PremiereDate");
    item.indexNumber = toInt(json::integer(value, "IndexNumber"));
    item.parentIndexNumber = toInt(json::integer(value, "ParentIndexNumber"));
    item.productionYear = toInt(json::integer(value, "ProductionYear"));
    item.childCount = toInt(json::integer(value, "ChildCount"));
    item.runTimeTicks = json::integer(value, "RunTimeTicks");
    item.communityRating = json::number(value, "CommunityRating");
    item.genres = json::strings(value, "Genres");

    yyjson_val* tags = json::object(value, "ImageTags");
    item.images.primary = json::string(tags, "Primary");
    item.images.thumb = json::string(tags, "Thumb");
    item.images.logo = json::string(tags, "Logo");
    item.images.backdrops = json::strings(value, "BackdropImageTags");
    item.images.parentThumbItemId = json::string(value, "ParentThumbItemId");
    item.images.parentThumb = json::string(value, "ParentThumbImageTag");
    item.images.parentBackdropItemId = json::string(value, "ParentBackdropItemId");
    item.images.parentBackdrops = json::strings(value, "ParentBackdropImageTags");
    item.images.seriesPrimary = json::string(value, "SeriesPrimaryImageTag");
    item.images.albumPrimary = json::string(value, "AlbumPrimaryImageTag");
    item.images.primaryAspectRatio = json::number(value, "PrimaryImageAspectRatio");

    yyjson_val* data = json::object(value, "UserData");
    item.userData.played = json::boolean(data, "Played");
    item.userData.favorite = json::boolean(data, "IsFavorite");
    item.userData.positionTicks = json::integer(data, "PlaybackPositionTicks");
    item.userData.playedPercentage = json::number(data, "PlayedPercentage");
    item.userData.unplayedCount = toInt(json::integer(data, "UnplayedItemCount"));

    item.album = json::string(value, "Album");
    item.albumId = json::string(value, "AlbumId");
    item.albumArtist = json::string(value, "AlbumArtist");
    item.albumArtistId = firstId(value, "AlbumArtists");
    item.artists = json::strings(value, "Artists");

    item.channelId = json::string(value, "ChannelId");
    item.channelName = json::string(value, "ChannelName");
    item.channelNumber = json::string(value, "ChannelNumber");
    if (item.channelNumber.empty()) {
        item.channelNumber = json::string(value, "Number");
    }
    item.startUtc = parseUtc(json::string(value, "StartDate"));
    item.endUtc = parseUtc(json::string(value, "EndDate"));
    item.timerId = json::string(value, "TimerId");
    yyjson_val* current = json::object(value, "CurrentProgram");
    if (current != nullptr && depth == 0) {
        item.currentProgram = std::make_shared<const Item>(parseItemValue(current, depth + 1));
    }
    return item;
}

UserInfo parseUserValue(yyjson_val* value) {
    UserInfo user;
    user.id = json::string(value, "Id");
    user.name = json::string(value, "Name");
    user.serverId = json::string(value, "ServerId");
    return user;
}

}  // namespace

ItemType itemTypeFromName(std::string_view name) {
    static const std::pair<std::string_view, ItemType> kTypes[] = {
        {"Movie", ItemType::Movie},
        {"Series", ItemType::Series},
        {"Season", ItemType::Season},
        {"Episode", ItemType::Episode},
        {"BoxSet", ItemType::BoxSet},
        {"Folder", ItemType::Folder},
        {"CollectionFolder", ItemType::CollectionFolder},
        {"UserView", ItemType::UserView},
        {"MusicAlbum", ItemType::MusicAlbum},
        {"MusicArtist", ItemType::MusicArtist},
        {"Audio", ItemType::Audio},
        {"Playlist", ItemType::Playlist},
        {"TvChannel", ItemType::TvChannel},
        {"Program", ItemType::Program},
        {"Video", ItemType::Video},
    };
    for (const auto& entry : kTypes) {
        if (entry.first == name) {
            return entry.second;
        }
    }
    return ItemType::Other;
}

std::optional<Version> parseVersion(std::string_view text) {
    int parts[4] = {0, 0, 0, 0};
    int count = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t dot = text.find('.', start);
        if (dot == std::string_view::npos) {
            dot = text.size();
        }
        const std::string_view part = text.substr(start, dot - start);
        if (part.empty() || part.size() > 5 || count == 4) {
            return std::nullopt;
        }
        int value = 0;
        for (char c : part) {
            if (c < '0' || c > '9') {
                return std::nullopt;
            }
            value = value * 10 + (c - '0');
        }
        if (value > 99999) {
            return std::nullopt;
        }
        parts[count++] = value;
        start = dot + 1;
    }
    if (count < 2) {
        return std::nullopt;
    }
    return Version{parts[0], parts[1], parts[2]};
}

bool versionAtLeast(const Version& version, int x, int y) {
    return version.x > x || (version.x == x && version.y >= y);
}

core::Result<ServerInfo> parseServerInfo(std::string_view text) {
    const auto doc = json::Document::parse(text);
    if (!doc || !yyjson_is_obj(doc->root())) {
        return badResponse("not a Jellyfin server");
    }
    yyjson_val* root = doc->root();
    ServerInfo info;
    info.id = json::string(root, "Id");
    info.name = json::string(root, "ServerName");
    info.version = json::string(root, "Version");
    info.localAddress = json::string(root, "LocalAddress");
    info.startupCompleted = json::boolean(root, "StartupWizardCompleted");
    if (info.id.empty() || !parseVersion(info.version)) {
        return badResponse("not a Jellyfin server");
    }
    return info;
}

core::Result<std::vector<PublicUser>> parsePublicUsers(std::string_view text) {
    const auto doc = json::Document::parse(text);
    if (!doc || !yyjson_is_arr(doc->root())) {
        return badResponse("invalid user list");
    }
    std::vector<PublicUser> users;
    yyjson_arr_iter iter = yyjson_arr_iter_with(doc->root());
    yyjson_val* entry = nullptr;
    while ((entry = yyjson_arr_iter_next(&iter)) != nullptr && users.size() < json::kMaxArray) {
        PublicUser user;
        user.id = json::string(entry, "Id");
        if (user.id.empty()) {
            continue;
        }
        user.name = json::string(entry, "Name");
        user.primaryImageTag = json::string(entry, "PrimaryImageTag");
        user.hasPassword = json::boolean(entry, "HasPassword", true);
        users.push_back(std::move(user));
    }
    return users;
}

core::Result<UserInfo> parseUser(std::string_view text) {
    const auto doc = json::Document::parse(text);
    if (!doc) {
        return badResponse("invalid user");
    }
    UserInfo user = parseUserValue(doc->root());
    if (user.id.empty()) {
        return badResponse("invalid user");
    }
    return user;
}

core::Result<AuthResult> parseAuth(std::string_view text) {
    const auto doc = json::Document::parse(text);
    if (!doc) {
        return badResponse("invalid sign-in reply");
    }
    AuthResult auth;
    auth.token = json::string(doc->root(), "AccessToken");
    auth.serverId = json::string(doc->root(), "ServerId");
    auth.user = parseUserValue(json::object(doc->root(), "User"));
    if (auth.token.empty() || auth.user.id.empty()) {
        return badResponse("invalid sign-in reply");
    }
    return auth;
}

core::Result<QuickConnectState> parseQuickConnect(std::string_view text) {
    const auto doc = json::Document::parse(text);
    if (!doc) {
        return badResponse("invalid Quick Connect reply");
    }
    QuickConnectState state;
    state.code = json::string(doc->root(), "Code");
    state.secret = json::string(doc->root(), "Secret");
    state.authenticated = json::boolean(doc->root(), "Authenticated");
    if (state.secret.empty()) {
        return badResponse("invalid Quick Connect reply");
    }
    return state;
}

core::Result<bool> parseBool(std::string_view text) {
    const auto doc = json::Document::parse(text);
    if (!doc || !yyjson_is_bool(doc->root())) {
        return badResponse("expected true or false");
    }
    return static_cast<bool>(yyjson_get_bool(doc->root()));
}

core::Result<ItemPage> parseItemPage(std::string_view text) {
    const auto doc = json::Document::parse(text);
    if (!doc) {
        return badResponse("invalid item list");
    }
    yyjson_val* root = doc->root();
    yyjson_val* list = nullptr;
    ItemPage page;
    const bool bare = yyjson_is_arr(root);
    if (bare) {
        list = root;
    } else {
        list = json::array(root, "Items");
        page.total = toInt(json::integer(root, "TotalRecordCount"));
        page.start = toInt(json::integer(root, "StartIndex"));
        if (page.total < 0) {
            page.total = 0;
        }
        if (page.start < 0) {
            page.start = 0;
        }
    }
    if (list == nullptr) {
        return badResponse("invalid item list");
    }
    yyjson_arr_iter iter = yyjson_arr_iter_with(list);
    yyjson_val* entry = nullptr;
    while ((entry = yyjson_arr_iter_next(&iter)) != nullptr && page.items.size() < json::kMaxArray) {
        if (!yyjson_is_obj(entry)) {
            continue;
        }
        Item item = parseItemValue(entry);
        if (!item.id.empty()) {
            page.items.push_back(std::move(item));
        }
    }
    if (bare) {
        page.total = static_cast<int>(page.items.size());
    }
    return page;
}

core::Result<Item> parseItem(std::string_view text) {
    const auto doc = json::Document::parse(text);
    if (!doc || !yyjson_is_obj(doc->root())) {
        return badResponse("invalid item");
    }
    Item item = parseItemValue(doc->root());
    if (item.id.empty()) {
        return badResponse("invalid item");
    }
    return item;
}

namespace {

StreamType streamTypeFromName(const std::string& name) {
    if (name == "Video") {
        return StreamType::Video;
    }
    if (name == "Audio") {
        return StreamType::Audio;
    }
    if (name == "Subtitle") {
        return StreamType::Subtitle;
    }
    return StreamType::Other;
}

MediaStream parseStream(yyjson_val* value) {
    MediaStream stream;
    stream.index = toInt(json::integer(value, "Index", -1));
    stream.type = streamTypeFromName(json::string(value, "Type"));
    stream.codec = core::asciiLower(json::string(value, "Codec"));
    stream.language = json::string(value, "Language");
    stream.title = json::string(value, "Title");
    stream.displayTitle = json::string(value, "DisplayTitle");
    stream.deliveryMethod = json::string(value, "DeliveryMethod");
    stream.deliveryUrl = json::string(value, "DeliveryUrl");
    stream.isDefault = json::boolean(value, "IsDefault");
    stream.isForced = json::boolean(value, "IsForced");
    stream.isExternal = json::boolean(value, "IsExternal");
    stream.width = toInt(json::integer(value, "Width"));
    stream.height = toInt(json::integer(value, "Height"));
    stream.channels = toInt(json::integer(value, "Channels"));
    return stream;
}

MediaSource parseSource(yyjson_val* value) {
    MediaSource source;
    source.id = json::string(value, "Id");
    source.container = json::string(value, "Container");
    source.transcodingUrl = json::string(value, "TranscodingUrl");
    source.liveStreamId = json::string(value, "LiveStreamId");
    source.isInfiniteStream = json::boolean(value, "IsInfiniteStream");
    source.supportsDirectPlay = json::boolean(value, "SupportsDirectPlay");
    source.supportsDirectStream = json::boolean(value, "SupportsDirectStream");
    source.supportsTranscoding = json::boolean(value, "SupportsTranscoding");
    source.defaultAudioIndex = toInt(json::integer(value, "DefaultAudioStreamIndex", -1));
    source.defaultSubtitleIndex = toInt(json::integer(value, "DefaultSubtitleStreamIndex", -1));
    source.runTimeTicks = json::integer(value, "RunTimeTicks");
    if (yyjson_val* streams = json::array(value, "MediaStreams")) {
        yyjson_arr_iter iter = yyjson_arr_iter_with(streams);
        yyjson_val* entry = nullptr;
        while ((entry = yyjson_arr_iter_next(&iter)) != nullptr && source.streams.size() < kMaxMediaStreams) {
            if (yyjson_is_obj(entry)) {
                source.streams.push_back(parseStream(entry));
            }
        }
    }
    return source;
}

}  // namespace

core::Result<PlaybackInfo> parsePlaybackInfo(std::string_view text) {
    const auto doc = json::Document::parse(text);
    if (!doc || !yyjson_is_obj(doc->root())) {
        return badResponse("invalid playback info");
    }
    yyjson_val* root = doc->root();
    PlaybackInfo info;
    info.playSessionId = json::string(root, "PlaySessionId");
    info.errorCode = json::string(root, "ErrorCode");
    if (yyjson_val* sources = json::array(root, "MediaSources")) {
        yyjson_arr_iter iter = yyjson_arr_iter_with(sources);
        yyjson_val* entry = nullptr;
        while ((entry = yyjson_arr_iter_next(&iter)) != nullptr && info.sources.size() < kMaxMediaSources) {
            if (yyjson_is_obj(entry)) {
                info.sources.push_back(parseSource(entry));
            }
        }
    }
    return info;
}

core::Result<std::vector<MediaSegment>> parseMediaSegments(std::string_view text) {
    const auto doc = json::Document::parse(text);
    yyjson_val* list = doc ? json::array(doc->root(), "Items") : nullptr;
    if (list == nullptr) {
        return badResponse("invalid media segments");
    }
    std::vector<MediaSegment> segments;
    yyjson_arr_iter iter = yyjson_arr_iter_with(list);
    yyjson_val* entry = nullptr;
    while ((entry = yyjson_arr_iter_next(&iter)) != nullptr && segments.size() < kMaxSegments) {
        if (!yyjson_is_obj(entry)) {
            continue;
        }
        MediaSegment segment;
        const std::string type = json::string(entry, "Type");
        segment.type = type == "Intro" ? SegmentType::Intro : type == "Outro" ? SegmentType::Outro : SegmentType::Other;
        segment.startTicks = json::integer(entry, "StartTicks");
        segment.endTicks = json::integer(entry, "EndTicks");
        segments.push_back(segment);
    }
    return segments;
}

namespace {

// days since 1970-01-01 of a civil date (proleptic Gregorian)
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

bool digits(std::string_view text, std::size_t at, std::size_t count, int& out) {
    if (at + count > text.size()) {
        return false;
    }
    out = 0;
    for (std::size_t i = at; i < at + count; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        out = out * 10 + (text[i] - '0');
    }
    return true;
}

}  // namespace

std::int64_t parseUtc(std::string_view text) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (text.size() < 19 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
        text[16] != ':' || !digits(text, 0, 4, year) || !digits(text, 5, 2, month) || !digits(text, 8, 2, day) ||
        !digits(text, 11, 2, hour) || !digits(text, 14, 2, minute) || !digits(text, 17, 2, second) || month < 1 ||
        month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
        return 0;
    }
    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 + hour * 3600 +
           minute * 60 + second;
}

std::string utcText(std::int64_t seconds) {
    std::int64_t days = seconds / 86400;
    std::int64_t rest = seconds % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    // civil date from days (inverse of daysFromCivil)
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
    char text[32];
    std::snprintf(text, sizeof(text), "%04lld-%02u-%02uT%02lld:%02lld:%02lldZ", static_cast<long long>(y), m, d,
                  static_cast<long long>(rest / 3600), static_cast<long long>(rest / 60 % 60),
                  static_cast<long long>(rest % 60));
    return text;
}

core::Result<std::vector<Timer>> parseTimers(std::string_view text) {
    const auto doc = json::Document::parse(text);
    yyjson_val* items = doc ? json::array(doc->root(), "Items") : nullptr;
    if (items == nullptr) {
        return core::makeError(core::ErrorKind::BadResponse, "timers: no Items");
    }
    std::vector<Timer> timers;
    yyjson_arr_iter iter = yyjson_arr_iter_with(items);
    while (yyjson_val* value = yyjson_arr_iter_next(&iter)) {
        if (timers.size() >= kMaxTimers) {
            break;
        }
        Timer timer;
        timer.id = json::string(value, "Id");
        timer.programId = json::string(value, "ProgramId");
        timer.channelId = json::string(value, "ChannelId");
        timer.name = json::string(value, "Name");
        timer.startUtc = parseUtc(json::string(value, "StartDate"));
        timer.endUtc = parseUtc(json::string(value, "EndDate"));
        timer.status = json::string(value, "Status");
        if (!timer.id.empty()) {
            timers.push_back(std::move(timer));
        }
    }
    return timers;
}

}  // namespace playfin::jellyfin
