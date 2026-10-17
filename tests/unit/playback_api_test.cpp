#include <doctest/doctest.h>

#include <string>

#include "jellyfin/client.hpp"
#include "jellyfin/models.hpp"
#include "support/fake_jellyfin.hpp"
#include "support/files.hpp"

using namespace playfin;
using jellyfin::StreamType;
using test::readFixture;

namespace {

jellyfin::Session signedIn(const std::string& baseUrl) {
    jellyfin::Session s;
    s.baseUrl = baseUrl;
    s.deviceId = "dev-1";
    s.deviceName = "Linux";
    s.token = "tok-demo";
    s.userId = "u-1";
    return s;
}

}  // namespace

TEST_CASE("PlaybackInfo: media source, streams and defaults") {
    const auto info = jellyfin::parsePlaybackInfo(readFixture("jellyfin/playbackinfo_direct.json"));
    REQUIRE(info.ok());
    CHECK(info.value().playSessionId == "ps-direct-1");
    REQUIRE(info.value().sources.size() == 1);
    const jellyfin::MediaSource& source = info.value().sources[0];
    CHECK(source.id == "ms-1");
    CHECK(source.container == "mkv");
    CHECK(source.supportsDirectPlay);
    CHECK(source.runTimeTicks == 26000000000);
    CHECK(source.defaultAudioIndex == 1);
    CHECK(source.defaultSubtitleIndex == 3);
    REQUIRE(source.streams.size() == 6);
    CHECK(source.streams[0].type == StreamType::Video);
    CHECK(source.streams[0].height == 1080);
    CHECK(source.streams[1].type == StreamType::Audio);
    CHECK(source.streams[1].channels == 6);
    CHECK(source.streams[1].title == "VF");
    CHECK(source.streams[3].isForced);
    CHECK(source.streams[4].isExternal);
    CHECK(source.streams[4].deliveryMethod == "External");
    CHECK(source.streams[4].deliveryUrl == "/Videos/e-1/ms-1/Subtitles/4/0/Stream.srt");
    CHECK(source.streams[5].codec == "pgssub");  // lower case
}

TEST_CASE("PlaybackInfo: transcoding URL and missing default subtitle") {
    const auto info = jellyfin::parsePlaybackInfo(readFixture("jellyfin/playbackinfo_transcode.json"));
    REQUIRE(info.ok());
    const jellyfin::MediaSource& source = info.value().sources.at(0);
    CHECK_FALSE(source.supportsDirectPlay);
    CHECK(source.transcodingUrl.rfind("/videos/m-transcode/master.m3u8?", 0) == 0);
    CHECK(source.defaultSubtitleIndex == -1);
}

TEST_CASE("PlaybackInfo: hostile replies give an error or capped lists, never a crash") {
    CHECK_FALSE(jellyfin::parsePlaybackInfo("").ok());
    CHECK_FALSE(jellyfin::parsePlaybackInfo("[]").ok());
    const auto noSources = jellyfin::parsePlaybackInfo(R"({"MediaSources":"x","ErrorCode":"NoCompatibleStream"})");
    REQUIRE(noSources.ok());
    CHECK(noSources.value().sources.empty());
    CHECK(noSources.value().errorCode == "NoCompatibleStream");
    std::string many = R"({"MediaSources":[{"Id":"m","MediaStreams":[)";
    for (int i = 0; i < 10000; ++i) {
        many += std::string(i == 0 ? "" : ",") + R"({"Type":"Audio","Index":)" + std::to_string(i) + "}";
    }
    many += "]}]}";
    const auto capped = jellyfin::parsePlaybackInfo(many);
    REQUIRE(capped.ok());
    CHECK(capped.value().sources.at(0).streams.size() == 512);
}

TEST_CASE("media segments: intro and outro, other kinds kept as Other") {
    const auto segments = jellyfin::parseMediaSegments(readFixture("jellyfin/segments.json"));
    REQUIRE(segments.ok());
    REQUIRE(segments.value().size() == 3);
    CHECK(segments.value()[0].type == jellyfin::SegmentType::Intro);
    CHECK(segments.value()[0].endTicks == 20000000);
    CHECK(segments.value()[1].type == jellyfin::SegmentType::Outro);
    CHECK(segments.value()[2].type == jellyfin::SegmentType::Other);
    CHECK_FALSE(jellyfin::parseMediaSegments("{").ok());
}

TEST_CASE("playback calls against the server: info, segments, reporting") {
    test::FakeJellyfin server;
    const net::HttpClient http{net::HttpConfig{}};
    const jellyfin::Client client(http, signedIn(server.baseUrl()));

    jellyfin::PlaybackRequest request;
    request.startTicks = 1230000000;
    request.deviceProfileJson = R"({"Name":"Playfin"})";
    const auto info = client.playbackInfo("e-1", request);
    REQUIRE(info.ok());
    CHECK(info.value().playSessionId == "ps-direct-1");
    const auto seen = server.last();
    CHECK(seen.method == "POST");
    CHECK(seen.body.find(R"("DeviceProfile":{"Name":"Playfin"})") != std::string::npos);
    CHECK(seen.body.find(R"("StartTimeTicks":1230000000)") != std::string::npos);
    CHECK(seen.body.find(R"("MaxStreamingBitrate":40000000)") != std::string::npos);
    CHECK(seen.target.find("tok-demo") == std::string::npos);

    const auto segments = client.mediaSegments("e-1");
    REQUIRE(segments.ok());
    CHECK(segments.value().size() == 3);
    const auto none = client.mediaSegments("m-none");  // 404: a server without segments
    REQUIRE(none.ok());
    CHECK(none.value().empty());

    jellyfin::PlaybackState state;
    state.itemId = "e-1";
    state.mediaSourceId = "ms-1";
    state.playSessionId = "ps-direct-1";
    state.positionTicks = 1230000000;
    state.audioIndex = 1;
    state.subtitleIndex = -1;
    REQUIRE(client.reportStart(state).ok());
    REQUIRE(client.reportProgress(state).ok());
    state.paused = true;
    REQUIRE(client.reportStopped(state).ok());
    const auto reports = server.reports();
    REQUIRE(reports.size() == 3);
    CHECK(reports[0].path == "/Sessions/Playing");
    CHECK(reports[1].path == "/Sessions/Playing/Progress");
    CHECK(reports[2].path == "/Sessions/Playing/Stopped");
    CHECK(reports[1].body.find(R"("PlayMethod":"DirectPlay")") != std::string::npos);
    CHECK(reports[1].body.find(R"("PositionTicks":1230000000)") != std::string::npos);
    CHECK(reports[2].body.find(R"("IsPaused":true)") != std::string::npos);
}
