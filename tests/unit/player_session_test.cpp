#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "jellyfin/models.hpp"
#include "player/seek.hpp"
#include "player/session.hpp"
#include "player/tracks.hpp"
#include "support/files.hpp"

using namespace playfin;
using test::readFixture;

namespace {

jellyfin::Session server() {
    jellyfin::Session s;
    s.baseUrl = "https://h:8920";
    s.token = "tok-demo";
    return s;
}

jellyfin::PlaybackInfo fixture(const char* name) {
    return jellyfin::parsePlaybackInfo(readFixture(std::string("jellyfin/") + name)).value();
}

}  // namespace

TEST_CASE("stream choice: direct play of the first source") {
    const auto choice = player::chooseStream(server(), "e-1", fixture("playbackinfo_direct.json"));
    REQUIRE(choice.ok());
    CHECK_FALSE(choice.value().transcoding);
    CHECK(choice.value().url ==
          "https://h:8920/Videos/e-1/stream?static=true&mediaSourceId=ms-1&playSessionId=ps-direct-1");
    CHECK(choice.value().mediaSourceId == "ms-1");
    REQUIRE(choice.value().externalSubtitles.size() == 1);
    CHECK(choice.value().externalSubtitles[0].index == 4);
    CHECK(choice.value().externalSubtitles[0].url == "https://h:8920/Videos/e-1/ms-1/Subtitles/4/0/Stream.srt");
}

TEST_CASE("stream choice: a transcode keeps the server's URL without its api key") {
    const auto choice = player::chooseStream(server(), "m-transcode", fixture("playbackinfo_transcode.json"));
    REQUIRE(choice.ok());
    CHECK(choice.value().transcoding);
    CHECK(choice.value().url.rfind("https://h:8920/videos/m-transcode/master.m3u8?", 0) == 0);
    CHECK(choice.value().url.find("tok-demo") == std::string::npos);
    CHECK(choice.value().url.find("api_key") == std::string::npos);
    CHECK(choice.value().playSessionId == "ps-tc-1");
}

TEST_CASE("stream choice: nothing playable is an error") {
    jellyfin::PlaybackInfo none;
    CHECK_FALSE(player::chooseStream(server(), "x", none).ok());
    jellyfin::PlaybackInfo refused = fixture("playbackinfo_direct.json");
    refused.errorCode = "NoCompatibleStream";
    CHECK_FALSE(player::chooseStream(server(), "x", refused).ok());
    jellyfin::PlaybackInfo stuck = fixture("playbackinfo_direct.json");
    stuck.sources[0].supportsDirectPlay = false;
    stuck.sources[0].transcodingUrl.clear();
    CHECK_FALSE(player::chooseStream(server(), "x", stuck).ok());
}

TEST_CASE("seeking: a burst of presses is one seek, 300 ms after the last") {
    player::SeekAccumulator seek;
    for (int i = 0; i < 5; ++i) {
        seek.press(10, 1000 + i * 100, 100, 3600);
    }
    CHECK(seek.pending());
    CHECK(seek.target() == doctest::Approx(150));
    CHECK_FALSE(seek.due(1600).has_value());  // 200 ms after the last press
    const auto target = seek.due(1700);
    REQUIRE(target.has_value());
    CHECK(*target == doctest::Approx(150));
    CHECK_FALSE(seek.pending());
    CHECK_FALSE(seek.due(5000).has_value());
}

TEST_CASE("seeking stays inside the video") {
    player::SeekAccumulator seek;
    seek.press(60, 0, 3580, 3600);
    CHECK(seek.target() == doctest::Approx(3599));
    player::SeekAccumulator back;
    back.press(-10, 0, 4, 3600);
    CHECK(back.target() == doctest::Approx(0));
    CHECK(player::seekStep(500) == doctest::Approx(10));
    CHECK(player::seekStep(2500) == doctest::Approx(30));
}

TEST_CASE("reporting: start once, progress every 10 s and on events, stop once") {
    std::vector<player::ReportKind> sent;
    player::Reporter reporter([&sent](player::ReportKind kind, const jellyfin::PlaybackState&) { sent.push_back(kind); });
    jellyfin::PlaybackState state;
    reporter.tick(0, state);  // before the first frame: nothing
    reporter.stop(state);     // left before playing: nothing
    CHECK(sent.empty());
    player::Reporter playing([&sent](player::ReportKind kind, const jellyfin::PlaybackState&) { sent.push_back(kind); });
    playing.onFirstFrame(0, state);
    playing.tick(5000, state);
    playing.tick(10000, state);
    playing.tick(15000, state);
    playing.onPause(16000, state);
    playing.tick(25000, state);
    playing.tick(26000, state);
    playing.stop(state);
    playing.stop(state);
    using K = player::ReportKind;
    CHECK(sent == std::vector<K>{K::Start, K::Progress, K::Progress, K::Progress, K::Stopped});
}

TEST_CASE("segment buttons: intro, credits and next episode") {
    std::vector<jellyfin::MediaSegment> segments = {{jellyfin::SegmentType::Intro, 0, 600000000},
                                                    {jellyfin::SegmentType::Outro, 24000000000, 25500000000}};
    using B = player::SegmentButton;
    player::SegmentButton intro = player::buttonAt(30, 2600, segments, false);
    CHECK(intro.kind == B::SkipIntro);
    CHECK(intro.seekTo == doctest::Approx(60));
    CHECK(player::buttonAt(100, 2600, segments, true).kind == B::None);
    player::SegmentButton outro = player::buttonAt(2450, 2600, segments, true);
    CHECK(outro.kind == B::SkipOutro);
    CHECK(outro.seekTo == doctest::Approx(2550));
    CHECK(player::buttonAt(2580, 2600, segments, true).kind == B::NextEpisode);
    CHECK(player::buttonAt(2580, 2600, segments, false).kind == B::None);
    CHECK(player::buttonAt(2580, 2600, {}, true).kind == B::NextEpisode);
}

TEST_CASE("segment buttons leave after 8 s on screen") {
    player::ButtonTimer timer;
    const player::SegmentButton intro{player::SegmentButton::SkipIntro, 60};
    CHECK(timer.visible(intro, 1000).kind == player::SegmentButton::SkipIntro);
    CHECK(timer.visible(intro, 8999).kind == player::SegmentButton::SkipIntro);
    CHECK(timer.visible(intro, 9001).kind == player::SegmentButton::None);
    const player::SegmentButton next{player::SegmentButton::NextEpisode, 0};
    CHECK(timer.visible(next, 9500).kind == player::SegmentButton::NextEpisode);  // a new button starts again
}

TEST_CASE("tracks: labels, mpv ids and the current one") {
    const jellyfin::MediaSource source = fixture("playbackinfo_direct.json").sources[0];
    const auto audio = player::audioTracks(source, 2);
    REQUIRE(audio.size() == 2);
    CHECK(audio[0].jellyfinIndex == 1);
    CHECK(audio[0].mpvId == 1);
    CHECK(audio[0].label == "Français - Dolby Digital+ - 5.1 - Par défaut");
    CHECK(audio[1].mpvId == 2);
    CHECK(audio[1].current);
    const auto subtitles = player::subtitleTracks(source, -1, "Aucun");
    REQUIRE(subtitles.size() == 4);
    CHECK(subtitles[0].label == "Aucun");
    CHECK(subtitles[0].current);
    CHECK(subtitles[0].mpvId == 0);
    CHECK(subtitles[1].jellyfinIndex == 3);
    CHECK(subtitles[1].mpvId == 1);
    CHECK(subtitles[2].jellyfinIndex == 4);  // external: added after the two embedded ones
    CHECK(subtitles[2].external);
    CHECK(subtitles[2].mpvId == 3);
    CHECK(subtitles[3].jellyfinIndex == 5);
    CHECK(subtitles[3].mpvId == 2);
}

TEST_CASE("a transcode restarts for audio and burned-in subtitles") {
    jellyfin::MediaStream audio;
    audio.type = jellyfin::StreamType::Audio;
    CHECK(player::needsRestart(true, audio));
    CHECK_FALSE(player::needsRestart(false, audio));
    jellyfin::MediaStream burned;
    burned.type = jellyfin::StreamType::Subtitle;
    burned.deliveryMethod = "Encode";
    CHECK(player::needsRestart(false, burned));
    jellyfin::MediaStream text;
    text.type = jellyfin::StreamType::Subtitle;
    text.deliveryMethod = "External";
    CHECK_FALSE(player::needsRestart(true, text));
}

TEST_CASE("streams and subtitles outside the server's own address are refused (the token would follow them)") {
    jellyfin::PlaybackInfo info = fixture("playbackinfo_transcode.json");
    info.sources[0].transcodingUrl = "https://elsewhere.example/videos/x/master.m3u8";
    CHECK_FALSE(player::chooseStream(server(), "m-transcode", info).ok());
    jellyfin::PlaybackInfo direct = fixture("playbackinfo_direct.json");
    direct.sources[0].streams[4].deliveryUrl = "https://elsewhere.example/sub.srt";
    const auto choice = player::chooseStream(server(), "e-1", direct);
    REQUIRE(choice.ok());
    CHECK(choice.value().externalSubtitles.empty());
    jellyfin::PlaybackInfo same = fixture("playbackinfo_transcode.json");
    same.sources[0].transcodingUrl = "https://h:8920/videos/x/master.m3u8?a=1";
    CHECK(player::chooseStream(server(), "m-transcode", same).ok());
}

TEST_CASE("a music track plays directly from the audio endpoint") {
    const auto choice = player::chooseStream(server(), "8ae260f682d60f5d17b5aafe1a7c8dd2",
                                             fixture("ts/playbackinfo_audio.json"), player::MediaKind::Audio);
    REQUIRE(choice.ok());
    CHECK(choice.value().url.find("/Audio/8ae260f682d60f5d17b5aafe1a7c8dd2/stream?static=true") != std::string::npos);
    CHECK_FALSE(choice.value().transcoding);
}

TEST_CASE("a live channel plays through the server's relay and keeps its live stream id") {
    const auto choice = player::chooseStream(server(), "79cf8fe44f3f38fbe05265ad38d3f2aa",
                                             fixture("ts/playbackinfo_live.json"));
    REQUIRE(choice.ok());
    CHECK(choice.value().transcoding);
    CHECK(choice.value().url.find("/videos/79cf8fe4-4f3f-38fb-e052-65ad38d3f2aa/master.m3u8") != std::string::npos);
    CHECK(choice.value().url.find("ApiKey") == std::string::npos);
    CHECK(choice.value().liveStreamId.rfind("e2329f4997b378e64ccf8fa396deb76e_", 0) == 0);
}
