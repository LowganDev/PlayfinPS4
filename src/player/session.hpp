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
    std::string sourceUrl;     // a live channel's own stream (an M3U tuner's), tried before the server's relay
    std::vector<ExternalSubtitle> externalSubtitles;
};

// https sources are played directly on the console too (2.0 sent them through the server after a test made
// without Internet access; retested with it on 2026-11-10, docs/ps4-notes.md).
constexpr bool kDirectHttps = true;

// A live channel's stream address when it can be played without the server: an endless http stream (an M3U
// tuner's), https too when `https`, else empty. It is played with no Jellyfin header (another host).
std::string directSourceUrl(const jellyfin::MediaSource& source, bool https = kDirectHttps);

// The first media source: played directly when the server allows it, else through its transcoding URL (joined to
// the server address, api key removed). An error when the server refused or offers neither. DirectOnly plays the
// file as it is whatever the server says of it, unless the server refused it for another reason than the format.
core::Result<StreamChoice> chooseStream(const jellyfin::Session& session, const std::string& itemId,
                                        const jellyfin::PlaybackInfo& info, MediaKind kind = MediaKind::Video,
                                        jellyfin::PlayMethod method = jellyfin::PlayMethod::Auto);

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
    enum Kind { None, SkipIntro, SkipOutro } kind = None;
    double seekTo = 0;  // seconds
};

// For a video the server has no segments for: its chapters named as an intro (intro, opening, recap, générique…,
// in the first half) or as credits (credits, ending, outro, générique…, in the second half), each up to the next
// chapter (the last one to `runTicks`); next to each other, they make one segment. Empty without a length.
// The player's speeds, slowest first (1: normal).
const std::vector<double>& speedChoices();
// Seconds of real time until a video ends, played on from `position` at `speed` (<= 0 counts as 1).
double remainingSeconds(double duration, double position, double speed);

std::vector<jellyfin::MediaSegment> segmentsFromChapters(const std::vector<jellyfin::Chapter>& chapters,
                                                         std::int64_t runTicks);

// The button for this moment: Passer l'intro inside an Intro, Passer le générique inside an Outro.
SegmentButton buttonAt(double position, const std::vector<jellyfin::MediaSegment>& segments);

// The next-episode card: shown in the last 10 s of a video that has a next episode, with the whole seconds left. Cancelled (◯), it stays hidden until playback goes back before the last 10 s.
class NextCard {
public:
    struct View {
        bool shown = false;
        int seconds = 0;
    };
    View at(double position, double duration, bool hasNext);
    void cancel() { cancelled_ = true; }
    // at the end of the file: the next episode plays unless the card was cancelled
    bool playsNextAtEnd(bool hasNext) const { return hasNext && !cancelled_; }

private:
    bool cancelled_ = false;
};

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
