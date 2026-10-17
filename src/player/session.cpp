#include "player/session.hpp"

#include "core/log.hpp"
#include "net/url.hpp"
#include "player/stream_url.hpp"

namespace playfin::player {

namespace {

constexpr std::int64_t kProgressMs = 10000;
constexpr double kNextEpisodeSeconds = 30;
constexpr std::int64_t kButtonMs = 8000;

// A path of the server joined to its address; an absolute URL only when it is on the same server (the
// Authorization header goes with every stream request). Empty otherwise.
std::string absolute(const jellyfin::Session& session, const std::string& path) {
    if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0) {
        const auto url = net::parseUrl(path.substr(0, path.find_first_of("?#")));
        const auto base = net::parseUrl(session.baseUrl);
        if (url && base && net::origin(*url) == net::origin(*base)) {
            return path;
        }
        return {};
    }
    return session.baseUrl + (path.empty() || path[0] == '/' ? "" : "/") + path;
}

}  // namespace

core::Result<StreamChoice> chooseStream(const jellyfin::Session& session, const std::string& itemId,
                                        const jellyfin::PlaybackInfo& info, MediaKind kind) {
    if (!info.errorCode.empty() || info.sources.empty()) {
        return core::makeError(core::ErrorKind::BadResponse,
                               info.errorCode.empty() ? "no media source" : info.errorCode);
    }
    const jellyfin::MediaSource& source = info.sources.front();
    StreamChoice choice;
    choice.mediaSourceId = source.id;
    choice.playSessionId = info.playSessionId;
    choice.liveStreamId = source.liveStreamId;
    if (source.supportsDirectPlay) {
        choice.url = directStreamUrl(session, itemId, source.id, info.playSessionId, kind);
    } else if (!source.transcodingUrl.empty() && !absolute(session, source.transcodingUrl).empty()) {
        choice.transcoding = true;
        choice.url = withoutApiKey(absolute(session, source.transcodingUrl));
    } else {
        return core::makeError(core::ErrorKind::BadResponse, "the server cannot stream this file");
    }
    for (const jellyfin::MediaStream& stream : source.streams) {
        if (stream.type == jellyfin::StreamType::Subtitle && stream.deliveryMethod == "External" &&
            !absolute(session, stream.deliveryUrl).empty()) {
            choice.externalSubtitles.push_back({stream.index, withoutApiKey(absolute(session, stream.deliveryUrl))});
        }
    }
    return choice;
}

void Reporter::onFirstFrame(std::int64_t nowMs, const jellyfin::PlaybackState& state) {
    if (started_) {
        return;
    }
    started_ = true;
    lastMs_ = nowMs;
    send_(ReportKind::Start, state);
}

void Reporter::tick(std::int64_t nowMs, const jellyfin::PlaybackState& state) {
    if (started_ && !stopped_ && nowMs - lastMs_ >= kProgressMs) {
        progressNow(nowMs, state);
    }
}

void Reporter::progressNow(std::int64_t nowMs, const jellyfin::PlaybackState& state) {
    if (!started_ || stopped_) {
        return;
    }
    lastMs_ = nowMs;
    send_(ReportKind::Progress, state);
}

void Reporter::stop(const jellyfin::PlaybackState& state) {
    if (!started_ || stopped_) {
        return;
    }
    stopped_ = true;
    send_(ReportKind::Stopped, state);
}

SegmentButton buttonAt(double position, double duration, const std::vector<jellyfin::MediaSegment>& segments,
                       bool hasNext) {
    if (hasNext && duration > 0 && position >= duration - kNextEpisodeSeconds) {
        return {SegmentButton::NextEpisode, 0};
    }
    for (const jellyfin::MediaSegment& segment : segments) {
        const double start = static_cast<double>(segment.startTicks) / kTicksPerSecond;
        const double end = static_cast<double>(segment.endTicks) / kTicksPerSecond;
        if (position < start || position >= end) {
            continue;
        }
        if (segment.type == jellyfin::SegmentType::Intro) {
            return {SegmentButton::SkipIntro, end};
        }
        if (segment.type == jellyfin::SegmentType::Outro) {
            return {SegmentButton::SkipOutro, end};
        }
    }
    return {};
}

SegmentButton ButtonTimer::visible(const SegmentButton& wanted, std::int64_t nowMs) {
    if (wanted.kind != kind_ || wanted.seekTo != seekTo_) {
        kind_ = wanted.kind;
        seekTo_ = wanted.seekTo;
        sinceMs_ = nowMs;
    }
    if (wanted.kind == SegmentButton::None || nowMs - sinceMs_ > kButtonMs) {
        return {};
    }
    return wanted;
}

void sendReport(const jellyfin::Client& client, ReportKind kind, const jellyfin::PlaybackState& state) {
    const core::Result<core::Unit> result = kind == ReportKind::Start      ? client.reportStart(state)
                                            : kind == ReportKind::Progress ? client.reportProgress(state)
                                                                           : client.reportStopped(state);
    if (!result.ok()) {
        PF_LOG_DEBUG("player: report failed: %s", result.error().message.c_str());
    }
}

}  // namespace playfin::player
