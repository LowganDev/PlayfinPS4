#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "player/spectrum.hpp"

namespace playfin::player {

// The music visualizer's sound: a track decoded a second time, on a thread of its own, with FFmpeg (no audio
// filters needed, none are in the PS4 build), mono at kRate into SpectrumFrames; read while it decodes. Stops
// when destroyed. The stream comes from the Jellyfin server: its header and TLS settings are the player's.
class AudioAnalysis {
public:
    static constexpr int kRate = 22050;
    static constexpr std::size_t kMaxFrames = 3 * 3600 * kFramesPerSecond;  // three hours

    struct Source {
        std::string url;
        std::string authorization;  // the Jellyfin "Authorization" header value
        bool verifyTls = true;
        std::string caFile;
    };

    explicit AudioAnalysis(Source source);
    ~AudioAnalysis();
    AudioAnalysis(const AudioAnalysis&) = delete;
    AudioAnalysis& operator=(const AudioAnalysis&) = delete;

    // The frame at `seconds` into the track, nullopt until decoded that far (or past the end).
    std::optional<SpectrumFrame> at(double seconds) const;
    bool finished() const { return finished_; }
    bool failed() const { return failed_; }

private:
    void run(const Source& source);

    std::atomic<bool> stop_{false};
    std::atomic<bool> finished_{false};
    std::atomic<bool> failed_{false};
    mutable std::mutex mutex_;
    std::vector<SpectrumFrame> frames_;
    std::thread thread_;
};

}  // namespace playfin::player
