#include "app/shell/music_player.hpp"

#include <algorithm>
#include <chrono>

#include "app/shell/engine_options.hpp"
#include "app/shell/session.hpp"
#include "core/log.hpp"
#include "player/profile.hpp"
#include "ui/application.hpp"

namespace playfin::app {

namespace {

struct Loaded {
    core::Result<player::StreamChoice> choice = player::StreamChoice{};
};

}  // namespace

MusicPlayer::MusicPlayer(AppSession& session) : session_(session) {}

MusicPlayer::~MusicPlayer() { token_.cancel(); }

std::int64_t MusicPlayer::now() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void MusicPlayer::play(std::vector<jellyfin::Item> tracks, int start, bool shuffle) {
    stop();
    queue_.set(std::move(tracks), start, shuffle);
    failures_ = 0;
    startCurrent();
}

jellyfin::PlaybackState MusicPlayer::playbackState() const {
    jellyfin::PlaybackState state;
    state.itemId = itemId_;
    state.mediaSourceId = stream_.mediaSourceId;
    state.playSessionId = stream_.playSessionId;
    state.transcoding = stream_.transcoding;
    if (engine_) {
        const player::Engine::State engine = engine_->state();
        state.positionTicks = static_cast<std::int64_t>(engine.position * player::kTicksPerSecond);
        state.paused = engine.paused;
    }
    return state;
}

void MusicPlayer::send(player::ReportKind kind, const jellyfin::PlaybackState& state) {
    const auto client = session_.client();
    if (!client) {
        return;
    }
    session_.submit([client, kind, state] { player::sendReport(*client, kind, state); });
}

// the current track: PlaybackInfo (on a worker), then the engine loads its stream
void MusicPlayer::startCurrent() {
    token_.cancel();
    token_ = core::CancelToken();
    if (reporter_) {
        reporter_->stop(playbackState());
        reporter_.reset();
    }
    const jellyfin::Item* track = queue_.current();
    const auto client = session_.client();
    if (track == nullptr || !client) {
        stop();
        return;
    }
    if (!engine_) {
        player::EngineOptions options = engineOptionsFor(*client, session_.synchronous);
        options.audioOnly = true;
        engine_ = player::Engine::create(options, [] {});  // update() polls it every frame
        if (!engine_) {
            PF_LOG_ERROR("music: no engine");
            stop();
            return;
        }
    }
    engine_->setPaused(true);  // the previous track stays silent (and cannot end) while the next one loads
    itemId_ = track->id;
    stream_ = player::StreamChoice{};
    reporter_ = std::make_unique<player::Reporter>(
        [this](player::ReportKind kind, const jellyfin::PlaybackState& state) { send(kind, state); });
    loading_ = true;
    ++changes_;
    jellyfin::PlaybackRequest request;
    request.maxBitrate = session_.settings().maxBitrate;
    player::DirectPlayLimits limits;
    limits.maxBitrate = request.maxBitrate;
    request.deviceProfileJson = player::deviceProfileJson(limits);
    const std::string id = track->id;
    const core::CancelToken token = token_;
    std::function<Loaded()> job = [client, id, request, token] {
        Loaded loaded;
        const auto info = client->playbackInfo(id, request, token);
        if (!info.ok()) {
            loaded.choice = info.error();
        } else {
            loaded.choice = player::chooseStream(client->session(), id, info.value(), player::MediaKind::Audio);
        }
        return loaded;
    };
    std::function<void(Loaded)> done = [this](Loaded loaded) {
        if (!loaded.choice.ok()) {
            failed(loaded.choice.error().message.c_str());
            return;
        }
        stream_ = loaded.choice.value();
        engine_->load(stream_.url, 0);
        engine_->setPaused(false);
    };
    ui::Application* app = ui::Application::current();
    if (session_.synchronous || app == nullptr) {
        done(job());
    } else {
        core::runAsync<Loaded>(app->pool(), app->mainQueue(), std::move(job), std::move(done), token);
    }
}

void MusicPlayer::failed(const char* why) {
    PF_LOG_WARN("music: track %s cannot play: %s", itemId_.c_str(), why);
    loading_ = false;
    ++failures_;
    if (failures_ >= static_cast<int>(queue_.items().size())) {
        stop();  // nothing in the queue plays
        return;
    }
    advance(true);
}

void MusicPlayer::advance(bool userAction) {
    if (queue_.next(userAction)) {
        startCurrent();
    } else {
        stop();
    }
}

void MusicPlayer::togglePause() {
    if (!engine_ || loading_) {
        return;
    }
    const bool paused = !engine_->state().paused;
    engine_->setPaused(paused);
    if (reporter_) {
        jellyfin::PlaybackState state = playbackState();
        state.paused = paused;
        reporter_->onPause(now(), state);
    }
    ++changes_;
}

void MusicPlayer::next() {
    if (active()) {
        advance(true);
    }
}

void MusicPlayer::previous() {
    if (!active()) {
        return;
    }
    const double position = engine_ ? engine_->state().position : 0;
    if (queue_.previous(position) == player::Queue::Back::Restart) {
        seekTo(0);
    } else {
        startCurrent();
    }
}

void MusicPlayer::seekTo(double seconds) {
    if (!engine_ || loading_) {
        return;
    }
    engine_->seekTo(seconds < 0 ? 0 : seconds);
    if (reporter_) {
        reporter_->onSeek(now(), playbackState());
    }
    ++changes_;
}

void MusicPlayer::jump(int offset) {
    const jellyfin::Item* before = queue_.current();
    queue_.jump(offset);
    if (queue_.current() != before) {
        startCurrent();
    }
}

void MusicPlayer::setShuffle(bool on) {
    queue_.setShuffle(on);
    ++changes_;
}

void MusicPlayer::setRepeat(player::Repeat repeat) {
    queue_.setRepeat(repeat);
    ++changes_;
}

void MusicPlayer::stop() {
    token_.cancel();
    if (reporter_) {
        reporter_->stop(playbackState());
        reporter_.reset();
    }
    engine_.reset();
    queue_.set({}, 0, false);
    loading_ = false;
    itemId_.clear();
    ++changes_;
}

void MusicPlayer::listen(const void* owner, std::function<void()> changed) {
    unlisten(owner);
    listeners_.emplace_back(owner, std::move(changed));
}

void MusicPlayer::unlisten(const void* owner) {
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                    [owner](const auto& entry) { return entry.first == owner; }),
                     listeners_.end());
}

void MusicPlayer::update() {
    updateEngine();
    if (notified_ != changes_) {
        notified_ = changes_;
        const auto listeners = listeners_;  // a listener may stop listening (its view goes away)
        for (const auto& entry : listeners) {
            const bool still = std::any_of(listeners_.begin(), listeners_.end(),
                                           [&](const auto& current) { return current.first == entry.first; });
            if (still) {
                entry.second();
            }
        }
    }
}

void MusicPlayer::updateEngine() {
    if (!engine_) {
        return;
    }
    for (const player::EngineEvent event : engine_->drainEvents()) {
        switch (event) {
        case player::EngineEvent::FileLoaded:
            loading_ = false;
            failures_ = 0;
            if (session_.frozenPlayback) {  // at 0:00 exactly: a few milliseconds may have played
                engine_->setPaused(true);
                engine_->seekTo(0);
            }
            if (reporter_) {
                reporter_->onFirstFrame(now(), playbackState());
            }
            ++changes_;
            break;
        case player::EngineEvent::EndOfFile:
            advance(false);
            break;
        case player::EngineEvent::Error:
            failed(engine_ ? engine_->state().error.c_str() : "error");
            break;
        case player::EngineEvent::FirstFrame:
        case player::EngineEvent::Seeked:
            break;
        }
        if (!engine_) {
            return;  // stopped by the event
        }
    }
    const player::Engine::State state = engine_->state();
    if (reporter_ && !loading_) {
        reporter_->tick(now(), playbackState());
    }
    const long second = static_cast<long>(state.position);
    if (second != lastSecond_ || state.paused != lastPaused_) {
        lastSecond_ = second;
        lastPaused_ = state.paused;
        ++changes_;
    }
}

MusicPlayer::Status MusicPlayer::status() const {
    Status status;
    status.track = queue_.current();
    status.loading = loading_;
    if (engine_) {
        const player::Engine::State state = engine_->state();
        status.position = state.position;
        status.duration = state.duration;
        status.paused = state.paused;
    }
    if (status.duration <= 0 && status.track != nullptr) {
        status.duration = static_cast<double>(status.track->runTimeTicks) / player::kTicksPerSecond;
    }
    return status;
}

}  // namespace playfin::app
