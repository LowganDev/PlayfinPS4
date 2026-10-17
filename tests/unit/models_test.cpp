#include <doctest/doctest.h>

#include <string>

#include "jellyfin/json.hpp"
#include "jellyfin/models.hpp"
#include "support/files.hpp"

using namespace playfin::jellyfin;
using playfin::core::ErrorKind;
using playfin::test::readFixture;

TEST_CASE("server info") {
    const auto info = parseServerInfo(readFixture("jellyfin/public_info.json"));
    REQUIRE(info.ok());
    CHECK(info.value().id == "f1e2d3c4b5a6");
    CHECK(info.value().name == "Test Server");
    CHECK(info.value().version == "10.10.7");
    CHECK(info.value().localAddress == "http://192.168.1.20:8096");
    CHECK(info.value().startupCompleted);
}

TEST_CASE("server info rejects what is not Jellyfin") {
    for (const char* bad : {"{}", "<html></html>", "[]", R"({"Id":"x"})", ""}) {
        INFO(bad);
        const auto info = parseServerInfo(bad);
        REQUIRE_FALSE(info.ok());
        CHECK(info.error().kind == ErrorKind::BadResponse);
    }
}

TEST_CASE("versions") {
    const auto v = parseVersion("10.10.7");
    REQUIRE(v.has_value());
    CHECK(v->x == 10);
    CHECK(v->y == 10);
    CHECK(v->z == 7);
    CHECK(versionAtLeast(*parseVersion("10.10.0"), 10, 10));
    CHECK(versionAtLeast(*parseVersion("11.0.0"), 10, 10));
    CHECK_FALSE(versionAtLeast(*parseVersion("10.9.11"), 10, 10));
    CHECK(parseVersion("10.10")->z == 0);
    CHECK(parseVersion("10.11.0.0").has_value());
    for (const char* bad : {"", "abc", "10", "10.x.1", "1.2.3.4.5", "999999.1"}) {
        INFO(bad);
        CHECK_FALSE(parseVersion(bad).has_value());
    }
}

TEST_CASE("public users skip entries without id") {
    const auto users = parsePublicUsers(readFixture("jellyfin/users_public.json"));
    REQUIRE(users.ok());
    REQUIRE(users.value().size() == 2);
    CHECK(users.value()[0].name == "demo");
    CHECK(users.value()[0].hasPassword);
    CHECK(users.value()[0].primaryImageTag == "img-demo");
    CHECK_FALSE(users.value()[1].hasPassword);
}

TEST_CASE("authentication result") {
    const auto auth = parseAuth(readFixture("jellyfin/auth.json"));
    REQUIRE(auth.ok());
    CHECK(auth.value().token == "tok-demo");
    CHECK(auth.value().user.id == "u-demo");
    CHECK(auth.value().user.name == "demo");
    CHECK(auth.value().serverId == "f1e2d3c4b5a6");
    CHECK_FALSE(parseAuth(R"({"User":{"Id":"u"}})").ok());
    CHECK_FALSE(parseAuth(R"({"AccessToken":"t"})").ok());
}

TEST_CASE("current user") {
    const auto user = parseUser(readFixture("jellyfin/user_me.json"));
    REQUIRE(user.ok());
    CHECK(user.value().id == "u-demo");
    CHECK_FALSE(parseUser("{}").ok());
}

TEST_CASE("movie item") {
    const auto item = parseItem(readFixture("jellyfin/item_movie.json"));
    REQUIRE(item.ok());
    const Item& m = item.value();
    CHECK(m.type == ItemType::Movie);
    CHECK(m.name == "Big Buck Bunny");
    CHECK(m.productionYear == 2008);
    CHECK(m.runTimeTicks == 5964000000LL);
    CHECK(m.communityRating == doctest::Approx(6.5));
    CHECK(m.officialRating == "PG");
    CHECK(m.genres == std::vector<std::string>{"Animation", "Comedy"});
    CHECK(m.images.primary == "t-m1");
    CHECK(m.images.thumb == "t-m1t");
    CHECK(m.images.logo == "t-m1l");
    CHECK(m.images.backdrops == std::vector<std::string>{"t-m1b"});
    CHECK(m.images.primaryAspectRatio == doctest::Approx(0.6667));
    CHECK(m.userData.favorite);
    CHECK_FALSE(m.userData.played);
    CHECK(m.userData.positionTicks == 1200000000LL);
    CHECK(m.userData.playedPercentage == doctest::Approx(20.1));
}

TEST_CASE("episode item with series images") {
    const auto item = parseItem(readFixture("jellyfin/item_episode.json"));
    REQUIRE(item.ok());
    const Item& e = item.value();
    CHECK(e.type == ItemType::Episode);
    CHECK(e.seriesName == "Test Show");
    CHECK(e.seriesId == "s-1");
    CHECK(e.seasonId == "se-1");
    CHECK(e.indexNumber == 1);
    CHECK(e.parentIndexNumber == 1);
    CHECK(e.images.parentBackdropItemId == "s-1");
    CHECK(e.images.parentBackdrops == std::vector<std::string>{"t-s1b"});
    CHECK(e.images.parentThumbItemId == "s-1");
    CHECK(e.images.parentThumb == "t-s1t");
    CHECK(e.images.seriesPrimary == "t-s1");
    CHECK(e.userData.played);
}

TEST_CASE("item pages: objects with Items, bare arrays, items without id dropped") {
    const auto latest = parseItemPage(readFixture("jellyfin/latest.json"));
    REQUIRE(latest.ok());
    CHECK(latest.value().items.size() == 2);
    CHECK(latest.value().total == 2);
    const auto views = parseItemPage(readFixture("jellyfin/views.json"));
    REQUIRE(views.ok());
    REQUIRE(views.value().items.size() == 2);
    CHECK(views.value().items[0].type == ItemType::CollectionFolder);
    CHECK(views.value().items[0].collectionType == "movies");
    CHECK(views.value().total == 2);
    const auto seasons = parseItemPage(readFixture("jellyfin/seasons.json"));
    REQUIRE(seasons.ok());
    CHECK(seasons.value().items[1].userData.unplayedCount == 2);
    CHECK_FALSE(parseItemPage(R"({"Total":1})").ok());
    CHECK_FALSE(parseItemPage("not json").ok());
}

TEST_CASE("hostile values never crash and fall back to defaults") {
    const auto page = parseItemPage(
        R"({"Items":[{"Id":5,"Name":["x"]},{"Id":"ok","Name":7,"IndexNumber":1e300,"RunTimeTicks":"long",)"
        R"("UserData":7,"ImageTags":[],"Genres":{"a":1},"BackdropImageTags":[1,"b",null],"CommunityRating":"9"},)"
        R"(3,null,"x"],"TotalRecordCount":"n","StartIndex":-5})");
    REQUIRE(page.ok());
    REQUIRE(page.value().items.size() == 1);
    const Item& item = page.value().items[0];
    CHECK(item.id == "ok");
    CHECK(item.name.empty());
    CHECK(item.indexNumber == 0);
    CHECK(item.runTimeTicks == 0);
    CHECK(item.genres.empty());
    CHECK(item.images.backdrops == std::vector<std::string>{"b"});
    CHECK(item.communityRating == 0);
    CHECK(page.value().total == 0);
}

TEST_CASE("huge strings and arrays are capped") {
    std::string overview(100000, 'a');
    const auto item = parseItem(R"({"Id":"x","Overview":")" + overview + R"("})");
    REQUIRE(item.ok());
    CHECK(item.value().overview.size() == json::kMaxString);
    std::string items = R"({"Items":[)";
    for (int i = 0; i < 10050; ++i) {
        items += (i ? "," : "") + std::string(R"({"Id":"i)") + std::to_string(i) + R"("})";
    }
    items += "]}";
    const auto page = parseItemPage(items);
    REQUIRE(page.ok());
    CHECK(page.value().items.size() == json::kMaxArray);
}

TEST_CASE("capping never cuts a UTF-8 character in half") {
    std::string overview(json::kMaxString - 1, 'a');
    overview += "\xC3\xA9";  // é straddles the limit
    const auto item = parseItem(R"({"Id":"x","Overview":")" + overview + R"("})");
    REQUIRE(item.ok());
    CHECK(item.value().overview.size() == json::kMaxString - 1);
}

TEST_CASE("quick connect state and booleans") {
    const auto state = parseQuickConnect(readFixture("jellyfin/quickconnect.json"));
    REQUIRE(state.ok());
    CHECK(state.value().code == "123456");
    CHECK(state.value().secret == "qc-secret");
    CHECK_FALSE(state.value().authenticated);
    CHECK_FALSE(parseQuickConnect(R"({"Code":"1"})").ok());
    CHECK(parseBool("true").value());
    CHECK_FALSE(parseBool("false").value());
    CHECK_FALSE(parseBool("1").ok());
}

TEST_CASE("writeObject escapes strings") {
    CHECK(json::writeObject({{"Username", "a\"b"}, {"Pw", "x\\y\n"}}) == R"({"Username":"a\"b","Pw":"x\\y\n"})");
}

TEST_CASE("UTC dates of the server become seconds since 1970") {
    CHECK(parseUtc("1970-01-01T00:00:00Z") == 0);
    CHECK(parseUtc("2026-10-16T19:30:00.0000000Z") == 1792179000);
    CHECK(parseUtc("2024-02-29T12:00:00Z") == 1709208000);
    CHECK(parseUtc("2026-10-16T19:30:00.5Z") == 1792179000);
    CHECK(parseUtc("") == 0);
    CHECK(parseUtc("not a date") == 0);
    CHECK(utcText(1792179000) == "2026-10-16T19:30:00Z");
}

TEST_CASE("music: tracks know their album, artists and disc") {
    const auto page = parseItemPage(readFixture("jellyfin/ts/album_tracks.json"));
    REQUIRE(page.ok());
    REQUIRE(page.value().items.size() == 6);
    const Item& track = page.value().items[3];
    CHECK(track.type == ItemType::Audio);
    CHECK(track.name == "Rétroviseur");
    CHECK(track.album == "Circuit doré");
    CHECK(track.albumId == "b74a3d6b210da7ad9bfab4365092b320");
    CHECK(track.albumArtist == "Kilohertz");
    CHECK(track.artists == std::vector<std::string>{"Kilohertz"});
    CHECK(track.indexNumber == 1);
    CHECK(track.parentIndexNumber == 2);
    CHECK(track.runTimeTicks == 430000000);
    const auto albums = parseItemPage(readFixture("jellyfin/ts/music_albums.json"));
    REQUIRE(albums.ok());
    CHECK(albums.value().items.front().type == ItemType::MusicAlbum);
    CHECK(albums.value().items.front().albumArtistId == "6233866b316f71faa355d1f0d999f963");
    CHECK(albums.value().items.front().childCount == 7);
    const auto artists = parseItemPage(readFixture("jellyfin/ts/music_artists.json"));
    REQUIRE(artists.ok());
    CHECK(artists.value().items.size() == 3);
    CHECK(artists.value().items[1].type == ItemType::MusicArtist);
}

TEST_CASE("Live TV: channels carry the programme on now, programmes their times") {
    const auto channels = parseItemPage(readFixture("jellyfin/ts/livetv_channels.json"));
    REQUIRE(channels.ok());
    REQUIRE(channels.value().items.size() == 3);
    const Item& channel = channels.value().items.front();
    CHECK(channel.type == ItemType::TvChannel);
    CHECK(channel.name == "Playfin Info");
    CHECK(channel.channelNumber == "1");
    REQUIRE(channel.currentProgram != nullptr);
    CHECK(channel.currentProgram->name == "Le point du jour");
    CHECK(channel.currentProgram->type == ItemType::Program);
    CHECK(channel.currentProgram->startUtc == parseUtc("2026-10-16T19:30:00Z"));
    CHECK(channel.currentProgram->endUtc == parseUtc("2026-10-16T20:30:00Z"));
    CHECK(channel.currentProgram->channelId == channel.id);
    const auto programs = parseItemPage(readFixture("jellyfin/ts/livetv_programs.json"));
    REQUIRE(programs.ok());
    CHECK(programs.value().items.size() == 41);
    CHECK_FALSE(programs.value().items.front().overview.empty());
}

TEST_CASE("Live TV: timers") {
    const auto timers = parseTimers(readFixture("jellyfin/ts/livetv_timers.json"));
    REQUIRE(timers.ok());
    REQUIRE(timers.value().size() == 1);
    const Timer& timer = timers.value().front();
    CHECK(timer.id == "0b4eabd5708f3dc71f2c03c524916899");
    CHECK(timer.programId == "1bbabe69245f97e2485d7053b72ae3a9");
    CHECK(timer.channelId == "79cf8fe44f3f38fbe05265ad38d3f2aa");
    CHECK(timer.name == "Reportage");
    CHECK(timer.startUtc == parseUtc("2026-10-16T22:45:00Z"));
    CHECK(timer.status == "New");
    CHECK_FALSE(parseTimers("{}").ok());
}

TEST_CASE("a live channel's media source has its live stream id") {
    const auto info = parsePlaybackInfo(readFixture("jellyfin/ts/playbackinfo_live.json"));
    REQUIRE(info.ok());
    REQUIRE_FALSE(info.value().sources.empty());
    const MediaSource& source = info.value().sources.front();
    CHECK(source.liveStreamId.rfind("e2329f4997b378e64ccf8fa396deb76e_", 0) == 0);
    CHECK(source.isInfiniteStream);
    CHECK(source.transcodingUrl.find("master.m3u8") != std::string::npos);
}

TEST_CASE("a playlist says whether it holds music or videos") {
    const auto page = parseItemPage(readFixture("jellyfin/ts/music_playlists.json"));
    REQUIRE(page.ok());
    REQUIRE(page.value().items.size() == 1);
    CHECK(page.value().items[0].type == ItemType::Playlist);
    CHECK(page.value().items[0].mediaType == "Audio");
}
