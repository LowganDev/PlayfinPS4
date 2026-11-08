#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace playfin::player {

// The music visualizer's picture of the sound: kBands levels (0 to 255, low to high frequencies on a log scale)
// kFramesPerSecond times a second of the track.
constexpr int kBands = 32;
constexpr int kFramesPerSecond = 30;
using SpectrumFrame = std::array<std::uint8_t, kBands>;

// Mono samples (-1 to 1) in, one SpectrumFrame per 1/kFramesPerSecond s out: a Hann window of kWindow samples, an
// FFT, the energy of each band in dB (-70 dB to 0 dB of a full-scale sine mapped to 0..255).
class SpectrumBuilder {
public:
    static constexpr std::size_t kWindow = 1024;

    explicit SpectrumBuilder(int sampleRate);
    void add(const float* samples, std::size_t count);
    // Frames made so far (taken: the builder keeps none).
    std::vector<SpectrumFrame> take();

private:
    SpectrumFrame analyse() const;

    int sampleRate_;
    std::size_t hop_;
    std::vector<float> window_;    // the last kWindow samples, a ring
    std::size_t next_ = 0;         // where the next sample goes (the oldest one)
    std::size_t sinceFrame_ = 0;
    std::vector<float> hann_;
    std::vector<std::pair<std::size_t, std::size_t>> bins_;  // FFT bins of each band
    std::vector<SpectrumFrame> frames_;
};

}  // namespace playfin::player
