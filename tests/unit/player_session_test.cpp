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
}

TEST_CASE("seek steps grow while the button is held: 10 s, 30 s after 2 s, 1 min after 4 s, 2 min after 6 s") {
    CHECK(player::seekStep(0) == doctest::Approx(10));
    CHECK(player::seekStep(1999) == doctest::Approx(10));
    CHECK(player::seekStep(2000) == doctest::Approx(30));
    CHECK(player::seekStep(4000) == doctest::Approx(60));
    CHECK(player::seekStep(6000) == doctest::Approx(120));
    CHECK(player::seekStep(60000) == doctest::Approx(120));
}

TEST_CASE("a held seek button repeats on time: after 400 ms, then every 200 ms, a late poll catching up") {
    player::ButtonRepeat repeat;
    CHECK(repeat.poll(1000) == 0);
    repeat.press(-1, 1000);
    CHECK(repeat.direction() == -1);
    CHECK(repeat.poll(1399) == 0);
    CHECK(repeat.poll(1400) == 1);
    CHECK(repeat.poll(1599) == 0);
    CHECK(repeat.poll(1600) == 1);
    CHECK(repeat.poll(2050) == 2);  // 1800 and 2000: a slow frame loses nothing
    CHECK(repeat.poll(2199) == 0);
    CHECK(repeat.poll(9000) == player::ButtonRepeat::kMaxCatchUp);  // a stall gives no burst
    repeat.release();
    CHECK(repeat.poll(9999) == 0);
}

TEST_CASE("hold to skip: due once after 3 s held, its progress meanwhile; a release before cancels it") {
    player::HoldToSkip hold;
    CHECK(hold.direction() == 0);
    hold.press(1, 1000);
    CHECK(hold.direction() == 1);
    CHECK(hold.progress(2500) == doctest::Approx(0.5));
    CHECK_FALSE(hold.due(3999));
    hold.release();
    CHECK(hold.direction() == 0);
    CHECK_FALSE(hold.due(5000));
    hold.press(-1, 6000);
    CHECK(hold.due(9000));
    CHECK(hold.direction() == 0);  // done: the button must be pressed again
    CHECK_FALSE(hold.due(20000));
    CHECK(hold.progress(20000) == doctest::Approx(0));
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

TEST_CASE("segment buttons: intro and credits (the next episode has its own card)") {
    std::vector<jellyfin::MediaSegment> segments = {{jellyfin::SegmentType::Intro, 0, 600000000},
                                                    {jellyfin::SegmentType::Outro, 24000000000, 25500000000}};
    using B = player::SegmentButton;
    player::SegmentButton intro = player::buttonAt(30, segments);
    CHECK(intro.kind == B::SkipIntro);
    CHECK(intro.seekTo == doctest::Approx(60));
    CHECK(player::buttonAt(100, segments).kind == B::None);
    player::SegmentButton outro = player::buttonAt(2450, segments);
    CHECK(outro.kind == B::SkipOutro);
    CHECK(outro.seekTo == doctest::Approx(2550));
    CHECK(player::buttonAt(2580, segments).kind == B::None);
}

TEST_CASE("segment buttons leave after 8 s on screen") {
    player::ButtonTimer timer;
    const player::SegmentButton intro{player::SegmentButton::SkipIntro, 60};
    CHECK(timer.visible(intro, 1000).kind == player::SegmentButton::SkipIntro);
    CHECK(timer.visible(intro, 8999).kind == player::SegmentButton::SkipIntro);
    CHECK(timer.visible(intro, 9001).kind == player::SegmentButton::None);
    const player::SegmentButton outro{player::SegmentButton::SkipOutro, 2550};
    CHECK(timer.visible(outro, 9500).kind == player::SegmentButton::SkipOutro);  // a new button starts again
}

TEST_CASE("next-episode card: the last 10 s, the seconds left, cancelled until sought back") {
    player::NextCard card;
    CHECK_FALSE(card.at(2589, 2600, true).shown);
    const auto shown = card.at(2590.2, 2600, true);
    CHECK(shown.shown);
    CHECK(shown.seconds == 10);
    CHECK(card.at(2599.5, 2600, true).seconds == 1);
    CHECK_FALSE(card.at(2595, 2600, false).shown);  // no next episode
    CHECK_FALSE(card.at(0, 0, true).shown);         // no duration yet
    CHECK(card.playsNextAtEnd(true));
    card.cancel();
    CHECK_FALSE(card.at(2595, 2600, true).shown);
    CHECK_FALSE(card.playsNextAtEnd(true));
    CHECK_FALSE(card.at(2500, 2600, true).shown);  // sought back out of the last 10 s: the card may come again
    CHECK(card.at(2595, 2600, true).shown);
    CHECK(card.playsNextAtEnd(true));
    CHECK_FALSE(card.playsNextAtEnd(false));
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
    CHECK(choice.value().sourceUrl == "http://streams/ch1/index.m3u8");  // tried first, straight from the source
}

TEST_CASE("a live channel's own source is played directly only over http or https") {
    jellyfin::MediaSource source;
    source.isInfiniteStream = true;
    source.protocol = "Http";
    source.path = "https://iptv.example/live/1.m3u8?user=a";
    CHECK(player::directSourceUrl(source) == "https://iptv.example/live/1.m3u8?user=a");
    source.path = "http://192.168.1.20:8000/ch1";
    CHECK(player::directSourceUrl(source) == "http://192.168.1.20:8000/ch1");
    for (const char* path : {"udp://239.0.0.1:1234", "rtsp://cam/1", "file:///etc/passwd", "/media/live.ts", ""}) {
        source.path = path;
        CHECK(player::directSourceUrl(source).empty());
    }
    source.path = "http://192.168.1.20:8000/ch1";
    source.protocol = "File";
    CHECK(player::directSourceUrl(source).empty());
    source.protocol = "Http";
    source.isInfiniteStream = false;  // a recording: through the server
    CHECK(player::directSourceUrl(source).empty());
}
