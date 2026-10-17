#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"

namespace playfin::jellyfin {

enum class ItemType {
    Movie,
    Series,
    Season,
    Episode,
    BoxSet,
    Folder,
    CollectionFolder,
    UserView,
    MusicAlbum,
    MusicArtist,
    Audio,
    Playlist,
    TvChannel,
    Program,
    Video,
    Other,
};
ItemType itemTypeFromName(std::string_view name);

struct UserData {
    bool played = false;
    bool favorite = false;
    std::int64_t positionTicks = 0;
    double playedPercentage = 0;
    int unplayedCount = 0;
};

struct ImageTags {
    std::string primary;
    std::string thumb;
    std::string logo;
    std::vector<std::string> backdrops;
    std::string parentThumbItemId;
    std::string parentThumb;
    std::string parentBackdropItemId;
    std::vector<std::string> parentBackdrops;
    std::string seriesPrimary;
    std::string albumPrimary;  // music tracks: their album's cover
    double primaryAspectRatio = 0;
};

struct Item {
    std::string id;
    std::string name;
    std::string serverId;
    ItemType type = ItemType::Other;
    std::string seriesId;
    std::string seriesName;
    std::string seasonId;
    std::string seasonName;
    std::string parentId;
    std::string collectionType;
    std::string mediaType;  // "Video", "Audio" (playlists)
    std::string overview;
    std::string officialRating;
    std::string premiereDate;
    int indexNumber = 0;
    int parentIndexNumber = 0;
    int productionYear = 0;
    int childCount = 0;
    std::int64_t runTimeTicks = 0;
    double communityRating = 0;
    std::vector<std::string> genres;
    ImageTags images;
    UserData userData;
    // music
    std::string album;
    std::string albumId;
    std::string albumArtist;
    std::string albumArtistId;  // the first of AlbumArtists
    std::vector<std::string> artists;
    // Live TV
    std::string channelId;
    std::string channelName;
    std::string channelNumber;
    std::int64_t startUtc = 0;  // programmes: seconds since 1970 (UTC), 0 when unknown
    std::int64_t endUtc = 0;
    std::string timerId;  // a programme scheduled to be recorded
    std::shared_ptr<const Item> currentProgram;  // channels listed with their programme on now
};

// A scheduled recording.
struct Timer {
    std::string id;
    std::string programId;
    std::string channelId;
    std::string name;
    std::int64_t startUtc = 0;
    std::int64_t endUtc = 0;
    std::string status;  // New, InProgress, Completed, Cancelled…
};

// "2026-10-16T19:30:00.0000000Z" → seconds since 1970 (UTC); 0 when it is not such a date.
std::int64_t parseUtc(std::string_view text);
// seconds since 1970 → "2026-10-16T19:30:00Z"
std::string utcText(std::int64_t seconds);

struct ItemPage {
    std::vector<Item> items;
    int total = 0;
    int start = 0;
};

struct ServerInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string localAddress;
    bool startupCompleted = false;
};

struct PublicUser {
    std::string id;
    std::string name;
    std::string primaryImageTag;
    bool hasPassword = true;
};

struct UserInfo {
    std::string id;
    std::string name;
    std::string serverId;
};

struct AuthResult {
    std::string token;
    std::string serverId;
    UserInfo user;
};

struct QuickConnectState {
    std::string code;
    std::string secret;
    bool authenticated = false;
};

// Playback (PlaybackInfo, media segments)
enum class StreamType { Video, Audio, Subtitle, Other };

struct MediaStream {
    int index = -1;
    StreamType type = StreamType::Other;
    std::string codec;  // lower case
    std::string language;
    std::string title;
    std::string displayTitle;
    std::string deliveryMethod;  // subtitles: Embed, External, Encode, Hls
    std::string deliveryUrl;     // server path of an External subtitle
    bool isDefault = false;
    bool isForced = false;
    bool isExternal = false;
    int width = 0;
    int height = 0;
    int channels = 0;
};

struct MediaSource {
    std::string id;
    std::string container;
    std::string transcodingUrl;  // server path with query, when the server transcodes
    std::string liveStreamId;    // a live channel's stream opened by PlaybackInfo (closed by the client)
    bool isInfiniteStream = false;
    bool supportsDirectPlay = false;
    bool supportsDirectStream = false;
    bool supportsTranscoding = false;
    int defaultAudioIndex = -1;
    int defaultSubtitleIndex = -1;
    std::int64_t runTimeTicks = 0;
    std::vector<MediaStream> streams;
};

struct PlaybackInfo {
    std::string playSessionId;
    std::string errorCode;  // set by the server when nothing can be played
    std::vector<MediaSource> sources;
};

enum class SegmentType { Intro, Outro, Other };

struct MediaSegment {
    SegmentType type = SegmentType::Other;
    std::int64_t startTicks = 0;
    std::int64_t endTicks = 0;
};

constexpr std::size_t kMaxMediaSources = 64;
constexpr std::size_t kMaxMediaStreams = 512;
constexpr std::size_t kMaxSegments = 256;
constexpr std::size_t kMaxTimers = 1000;

// x.y.z (missing parts are 0)
struct Version {
    int x = 0;
    int y = 0;
    int z = 0;
};
std::optional<Version> parseVersion(std::string_view text);
bool versionAtLeast(const Version& version, int x, int y);

// Parsers never throw or crash; anything unusable is BadResponse.
core::Result<ServerInfo> parseServerInfo(std::string_view text);
core::Result<std::vector<PublicUser>> parsePublicUsers(std::string_view text);
core::Result<UserInfo> parseUser(std::string_view text);
core::Result<AuthResult> parseAuth(std::string_view text);
core::Result<QuickConnectState> parseQuickConnect(std::string_view text);
core::Result<bool> parseBool(std::string_view text);
// {"Items":[...],"TotalRecordCount":n,"StartIndex":n} or a bare array; items without Id are dropped
core::Result<ItemPage> parseItemPage(std::string_view text);
core::Result<Item> parseItem(std::string_view text);
core::Result<PlaybackInfo> parsePlaybackInfo(std::string_view text);
core::Result<std::vector<Timer>> parseTimers(std::string_view text);
// {"Items":[...]} of /MediaSegments
core::Result<std::vector<MediaSegment>> parseMediaSegments(std::string_view text);

}  // namespace playfin::jellyfin
