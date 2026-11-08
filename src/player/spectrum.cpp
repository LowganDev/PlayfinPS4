#include "player/spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace playfin::player {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kLowHz = 40;
constexpr double kHighHz = 10000;
constexpr double kFloorDb = -70;

// in place, size a power of two
void fft(std::vector<std::complex<float>>& data) {
    const std::size_t n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const double angle = -2 * kPi / static_cast<double>(length);
        const std::complex<float> step(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
        for (std::size_t i = 0; i < n; i += length) {
            std::complex<float> w(1, 0);
            for (std::size_t k = 0; k < length / 2; ++k) {
                const std::complex<float> u = data[i + k];
                const std::complex<float> v = data[i + k + length / 2] * w;
                data[i + k] = u + v;
                data[i + k + length / 2] = u - v;
                w *= step;
            }
        }
    }
}

}  // namespace

SpectrumBuilder::SpectrumBuilder(int sampleRate)
    : sampleRate_(sampleRate > 0 ? sampleRate : 22050),
      hop_(static_cast<std::size_t>(sampleRate_ / kFramesPerSecond)),
      window_(kWindow, 0.0f),
      hann_(kWindow) {
    for (std::size_t i = 0; i < kWindow; ++i) {
        hann_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / (kWindow - 1)));
    }
    // log-spaced band edges between kLowHz and kHighHz (at most Nyquist), at least one bin each
    const double binHz = static_cast<double>(sampleRate_) / kWindow;
    const double high = std::min(kHighHz, sampleRate_ / 2.0 - binHz);
    std::size_t previous = std::max<std::size_t>(1, static_cast<std::size_t>(kLowHz / binHz));
    for (int band = 0; band < kBands; ++band) {
        const double edge = kLowHz * std::pow(high / kLowHz, static_cast<double>(band + 1) / kBands);
        std::size_t end = static_cast<std::size_t>(edge / binHz) + 1;
        end = std::min(std::max(end, previous + 1), kWindow / 2);
        bins_.emplace_back(previous, end);
        previous = end < kWindow / 2 ? end : kWindow / 2 - 1;
    }
}

void SpectrumBuilder::add(const float* samples, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        window_[next_] = samples[i];
        next_ = (next_ + 1) % kWindow;
        if (++sinceFrame_ >= hop_) {
            sinceFrame_ = 0;
            frames_.push_back(analyse());
        }
    }
}

SpectrumFrame SpectrumBuilder::analyse() const {
    std::vector<std::complex<float>> data(kWindow);
    for (std::size_t i = 0; i < kWindow; ++i) {  // oldest first
        data[i] = std::complex<float>(window_[(next_ + i) % kWindow] * hann_[i], 0);
    }
    fft(data);
    SpectrumFrame frame{};
    // a full-scale sine through the Hann window peaks at kWindow / 4
    const double reference = static_cast<double>(kWindow) / 4;
    for (int band = 0; band < kBands; ++band) {
        float peak = 0;
        for (std::size_t bin = bins_[static_cast<std::size_t>(band)].first;
             bin < bins_[static_cast<std::size_t>(band)].second; ++bin) {
            peak = std::max(peak, std::abs(data[bin]));
        }
        const double db = 20 * std::log10(std::max(1e-9, static_cast<double>(peak) / reference));
        const double level = std::clamp((db - kFloorDb) / -kFloorDb, 0.0, 1.0);
        frame[static_cast<std::size_t>(band)] = static_cast<std::uint8_t>(std::lround(level * 255));
    }
    return frame;
}

std::vector<SpectrumFrame> SpectrumBuilder::take() {
    std::vector<SpectrumFrame> out;
    out.swap(frames_);
    return out;
}

}  // namespace playfin::player
