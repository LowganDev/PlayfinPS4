#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "player/engine.hpp"

using namespace playfin;

namespace {

const std::string kClip = std::string(PLAYFIN_TEST_FIXTURES) + "/media/clip.mp4";
const std::string kSubtitle = std::string(PLAYFIN_TEST_FIXTURES) + "/media/one.vtt";

// The main loop of a test: waits for the engine's wake-ups, renders, collects events.
class Pump {
public:
    Pump() {
        player::EngineOptions options;
        options.audioOutput = "null";
        engine = player::Engine::create(options, [this] {
            std::lock_guard<std::mutex> lock(mutex_);
            woken_ = true;
            wake_.notify_one();
        });
    }

    // Runs until `done` returns true or `ms` elapse; false on timeout.
    template <typename Done>
    bool until(int ms, Done done) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(20), [this] { return woken_; });
                woken_ = false;
            }
            for (player::EngineEvent event : engine->drainEvents()) {
                events.push_back(event);
            }
            engine->renderIfDue(frame);
            if (done()) {
                return true;
            }
        }
        return false;
    }

    bool saw(player::EngineEvent event) const {
        for (player::EngineEvent e : events) {
            if (e == event) {
                return true;
            }
        }
        return false;
    }

    std::unique_ptr<player::Engine> engine;
    player::VideoFrame frame;
    std::vector<player::EngineEvent> events;

private:
    std::mutex mutex_;
    std::condition_variable wake_;
    bool woken_ = false;
};

}  // namespace

TEST_CASE("the engine renders frames of a local clip") {
    Pump pump;
    REQUIRE(pump.engine != nullptr);
    pump.engine->load(kClip, 0);
    REQUIRE(pump.until(8000, [&] { return pump.saw(player::EngineEvent::FirstFrame) && pump.frame.serial > 0; }));
    CHECK(pump.frame.width == 320);
    CHECK(pump.frame.height == 180);
    REQUIRE(pump.frame.pixels.size() >= 320u * 180u * 4u);
    bool lit = false;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16 * 4; ++x) {
            lit = lit || pump.frame.pixels[static_cast<std::size_t>(y * 320 * 4 + x)] != 0;
        }
    }
    CHECK(lit);
}

TEST_CASE("the engine seeks and pauses") {
    Pump pump;
    REQUIRE(pump.engine != nullptr);
    pump.engine->load(kClip, 0);
    REQUIRE(pump.until(8000, [&] { return pump.saw(player::EngineEvent::FirstFrame); }));
    pump.events.clear();
    pump.engine->seekTo(2.0);
    REQUIRE(pump.until(5000, [&] { return pump.saw(player::EngineEvent::Seeked); }));
    CHECK(pump.engine->state().position >= 1.9);
    pump.engine->setPaused(true);
    pump.until(300, [] { return false; });
    CHECK(pump.engine->state().paused);
}

TEST_CASE("the engine reports the end of the file") {
    Pump pump;
    REQUIRE(pump.engine != nullptr);
    pump.engine->load(kClip, 3.5);
    CHECK(pump.until(8000, [&] { return pump.saw(player::EngineEvent::EndOfFile); }));
}

TEST_CASE("a missing file is an engine error, not a crash") {
    Pump pump;
    REQUIRE(pump.engine != nullptr);
    pump.engine->load("/nonexistent/playfin.mp4", 0);
    CHECK(pump.until(5000, [&] { return pump.saw(player::EngineEvent::Error); }));
}

TEST_CASE("an external subtitle file is added to the engine") {
    Pump pump;
    REQUIRE(pump.engine != nullptr);
    pump.engine->load(kClip, 0);
    REQUIRE(pump.until(8000, [&] { return pump.saw(player::EngineEvent::FileLoaded); }));
    pump.engine->addSubtitle(kSubtitle, true);
    pump.until(500, [] { return false; });
    CHECK(pump.engine->state().subtitleTrack > 0);
    CHECK_FALSE(pump.saw(player::EngineEvent::Error));
}

TEST_CASE("rendering never waits for the picture's display time") {
    Pump pump;
    REQUIRE(pump.engine != nullptr);
    pump.engine->load(kClip, 0);
    REQUIRE(pump.until(8000, [&] { return pump.saw(player::EngineEvent::FirstFrame); }));
    double slowest = 0;
    const std::uint64_t first = pump.frame.serial;
    pump.until(1500, [&] {
        slowest = std::max(slowest, pump.engine->stats().renderMs);
        return false;
    });
    CHECK(pump.frame.serial > first + 10);  // it keeps playing
    CHECK(slowest < 15.0);                  // a 25 fps picture would block up to 40 ms
}

TEST_CASE("the player never runs scripts or configuration, nor follows a file to other files or fonts") {
    const auto options = playfin::player::hardeningOptions();
    const auto has = [&](const char* name, const char* value) {
        for (const auto& option : options) {
            if (std::string(option.first) == name) {
                return std::string(option.second) == value;
            }
        }
        return false;
    };
    CHECK(has("config", "no"));
    CHECK(has("load-scripts", "no"));
    CHECK(has("ytdl", "no"));
    CHECK(has("input-default-bindings", "no"));
    CHECK(has("embeddedfonts", "no"));  // fonts attached to a file never reach FreeType
    CHECK(has("access-references", "no"));
    CHECK(has("load-unsafe-playlists", "no"));
}

TEST_CASE("a hardening option this mpv does not know is a warning, except ytdl (built without Lua)") {
    CHECK(playfin::player::optionMayBeMissing("ytdl"));
    for (const auto& option : playfin::player::hardeningOptions()) {
        if (std::string(option.first) != "ytdl") {
            CHECK_FALSE(playfin::player::optionMayBeMissing(option.first));
        }
    }
}
