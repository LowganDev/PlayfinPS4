#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "player/audio_analysis.hpp"
#include "support/fake_jellyfin.hpp"

using namespace playfin;

namespace {

void waitFor(const player::AudioAnalysis& analysis) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!analysis.finished() && !analysis.failed() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

}  // namespace

TEST_CASE("visualizer: a track from the server decoded into spectrum frames, with the Jellyfin header") {
    test::FakeJellyfin server;
    player::AudioAnalysis::Source source;
    source.url = server.baseUrl() + "/Audio/a-1/stream?static=true";
    source.authorization = R"(MediaBrowser Client="Playfin", Device="test", DeviceId="t", Version="2.0.0", Token="tok-demo")";
    player::AudioAnalysis analysis(source);
    waitFor(analysis);
    REQUIRE(analysis.finished());
    CHECK_FALSE(analysis.failed());
    const auto frame = analysis.at(0.5);  // the 1.5 s test tone
    REQUIRE(frame.has_value());
    int lit = 0;
    for (auto level : *frame) {
        lit += level > 100 ? 1 : 0;
    }
    CHECK(lit > 0);
    CHECK_FALSE(analysis.at(60).has_value());  // past the end
}

TEST_CASE("visualizer: a track that cannot be read fails quietly; destroying it stops the decoding") {
    test::FakeJellyfin server;
    player::AudioAnalysis::Source source;
    source.url = server.baseUrl() + "/Audio/a-1/stream?static=true";  // no header: refused
    player::AudioAnalysis refused(source);
    waitFor(refused);
    CHECK(refused.failed());
    CHECK_FALSE(refused.at(0).has_value());
    source.url = "http://127.0.0.1:9/nothing";
    { player::AudioAnalysis gone(source); }  // returns at once
}
