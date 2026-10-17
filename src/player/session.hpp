#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/result.hpp"
#include "jellyfin/client.hpp"
#include "jellyfin/models.hpp"
#include "player/stream_url.hpp"

namespace playfin::player {

struct ExternalSubtitle {
    int index = -1;   // Jellyfin stream index
    std::string url;  // absolute, on the same server (authenticated by the header)
};

struct StreamChoice {
    std::string url;
    bool transcoding = false;
    std::string mediaSourceId;
    std::string playSessionId;
    std::string liveStreamId;  // a live channel: closed when playback ends
    std::vector<ExternalSubtitle> externalSubtitles;
};

// The first media source: played directly when the server allows it, else through its transcoding URL (joined to
// the server address, api key removed). An error when the server refused or offers neither.
core::Result<StreamChoice> chooseStream(const jellyfin::Session& session, const std::string& itemId,
                                        const jellyfin::PlaybackInfo& info, MediaKind kind = MediaKind::Video);

constexpr double kTicksPerSecond = 10000000.0;  // Jellyfin positions are in ticks of 100 ns

enum class ReportKind { Start, Progress, Stopped };

// One report to the server, blocking (call from a worker); a failure is only logged.
void sendReport(const jellyfin::Client& client, ReportKind kind, const jellyfin::PlaybackState& state);

// When to tell the server: Start at the first picture, Progress every 10 s and right after a pause, a seek or a
// track change, Stopped once (only if Start was sent).
class Reporter {
public:
    using Send = std::function<void(ReportKind, const jellyfin::PlaybackState&)>;
    explicit Reporter(Send send) : send_(std::move(send)) {}

    void onFirstFrame(std::int64_t nowMs, const jellyfin::PlaybackState& state);
    void tick(std::int64_t nowMs, const jellyfin::PlaybackState& state);
    void onPause(std::int64_t nowMs, const jellyfin::PlaybackState& state) { progressNow(nowMs, state); }
    void onSeek(std::int64_t nowMs, const jellyfin::PlaybackState& state) { progressNow(nowMs, state); }
    void onTrackChange(std::int64_t nowMs, const jellyfin::PlaybackState& state) { progressNow(nowMs, state); }
    void stop(const jellyfin::PlaybackState& state);
    bool started() const { return started_; }

private:
    void progressNow(std::int64_t nowMs, const jellyfin::PlaybackState& state);

    Send send_;
    bool started_ = false;
    bool stopped_ = false;
    std::int64_t lastMs_ = 0;
};

struct SegmentButton {
    enum Kind { None, SkipIntro, SkipOutro, NextEpisode } kind = None;
    double seekTo = 0;  // seconds (skip buttons)
};

// The button for this moment: Passer l'intro inside an Intro, Passer le générique inside an Outro, Épisode
// suivant in the last 30 s when there is a next episode (it wins over the Outro button there).
SegmentButton buttonAt(double position, double duration, const std::vector<jellyfin::MediaSegment>& segments,
                       bool hasNext);

// A button stays on screen 8 s at most; a different button starts its own 8 s.
class ButtonTimer {
public:
    SegmentButton visible(const SegmentButton& wanted, std::int64_t nowMs);

private:
    SegmentButton::Kind kind_ = SegmentButton::None;
    double seekTo_ = 0;
    std::int64_t sinceMs_ = 0;
};

}  // namespace playfin::player
