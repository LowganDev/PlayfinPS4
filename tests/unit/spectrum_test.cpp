#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "player/spectrum.hpp"

using namespace playfin::player;

namespace {

std::vector<float> tone(double hz, double seconds, int rate, float amplitude) {
    std::vector<float> samples(static_cast<std::size_t>(seconds * rate));
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = amplitude * static_cast<float>(std::sin(2 * 3.14159265358979 * hz * static_cast<double>(i) / rate));
    }
    return samples;
}

int loudest(const SpectrumFrame& frame) {
    return static_cast<int>(std::max_element(frame.begin(), frame.end()) - frame.begin());
}

}  // namespace

TEST_CASE("spectrum: 30 frames a second; a low tone lights a low band, a high tone a high one") {
    SpectrumBuilder low(22050);
    const auto bass = tone(100, 1.0, 22050, 0.8f);
    low.add(bass.data(), bass.size());
    const auto lowFrames = low.take();
    CHECK(lowFrames.size() == 30);
    CHECK(low.take().empty());
    SpectrumBuilder high(22050);
    const auto treble = tone(5000, 1.0, 22050, 0.8f);
    high.add(treble.data(), treble.size());
    const auto highFrames = high.take();
    const int lowBand = loudest(lowFrames.back());
    const int highBand = loudest(highFrames.back());
    CHECK(lowBand < 8);
    CHECK(highBand > 20);
    CHECK(lowFrames.back()[static_cast<std::size_t>(lowBand)] > 200);  // near full scale
}

TEST_CASE("spectrum: silence is all zero, a quieter tone is lower") {
    SpectrumBuilder silent(22050);
    const std::vector<float> nothing(22050, 0.0f);
    silent.add(nothing.data(), nothing.size());
    const auto frames = silent.take();
    REQUIRE_FALSE(frames.empty());
    for (auto level : frames.back()) {
        CHECK(level == 0);
    }
    SpectrumBuilder loud(22050);
    SpectrumBuilder quiet(22050);
    const auto a = tone(1000, 0.5, 22050, 0.9f);
    const auto b = tone(1000, 0.5, 22050, 0.05f);
    loud.add(a.data(), a.size());
    quiet.add(b.data(), b.size());
    const auto la = loud.take().back();
    const auto lb = quiet.take().back();
    CHECK(la[static_cast<std::size_t>(loudest(la))] > lb[static_cast<std::size_t>(loudest(lb))]);
}
