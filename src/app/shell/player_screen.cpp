#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "app/shell/engine_options.hpp"
#include "app/shell/format.hpp"
#include "app/shell/guide.hpp"
#include "app/shell/music_player.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "core/log.hpp"
#include "platform/platform.hpp"
#include "player/engine.hpp"
#include "player/profile.hpp"
#include "player/seek.hpp"
#include "player/session.hpp"
#include "player/tracks.hpp"
#include "ui/context.hpp"
#include "ui/i18n.hpp"
#include "ui/label.hpp"
#include "ui/theme.hpp"
#include "ui/video_source.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

constexpr std::int64_t kBarMs = 4000;          // the bar hides after 4 s without input
constexpr std::int64_t kHoldGapMs = 300;       // presses closer than this are one held button
constexpr std::int64_t kCountdownMs = 10000;   // next episode after 10 s
constexpr std::int64_t kStatsMs = 1000;

struct Loaded {
    core::Result<jellyfin::PlaybackInfo> info = core::makeError(core::ErrorKind::Cancelled, "");
    core::Result<player::StreamChoice> choice = core::makeError(core::ErrorKind::Cancelled, "");
};

// A channel's stream stays open on the server (and holds a tuner) until it is closed. The stream a PlaybackInfo
// opened passes from the worker to the screen here: whichever of the two finds the other gone closes it.
struct LiveHandOff {
    std::mutex mutex;
    std::string pending;  // opened, not taken by the screen yet
    bool screenGone = false;
};

void closeOnServer(const std::shared_ptr<const jellyfin::Client>& client, const std::string& id) {
    const auto result = client->closeLiveStream(id);
    if (!result.ok()) {
        PF_LOG_DEBUG("player: live stream not closed: %s", result.error().message.c_str());
    }
}

struct Extras {
    std::vector<jellyfin::MediaSegment> segments;
    std::optional<jellyfin::Item> next;
    std::vector<jellyfin::Item> channels;  // live: the channels to change to, with their programme on now
};

class PlayerScreen : public ShellScreen, public ui::VideoSource {
public:
    PlayerScreen(AppSession& session, jellyfin::Item item, std::int64_t startTicks)
        : ShellScreen(session),
          item_(std::move(item)),
          live_(item_.type == jellyfin::ItemType::TvChannel),
          startTicks_(startTicks),
          reporter_([this](player::ReportKind kind, const jellyfin::PlaybackState& state) { send(kind, state); }) {
        focusable = true;
        buildOverlay();
        bindControls();
    }
    ~PlayerScreen() override { release(); }

    void onShow() override {
        if (ui::Application* app = ui::Application::current()) {
            app->setVideoSource(this);
        }
        if (!started_) {
            started_ = true;
            showBar();
            start(startTicks_);
        }
    }
    void onHide() override { release(); }

    bool renderVideo() override {
        if (!engine_) {
            return false;
        }
        for (player::EngineEvent event : engine_->drainEvents()) {
            onEngineEvent(event);
        }
        const bool fresh = engine_->renderIfDue(frame_);
        tick(fresh);
        return fresh;
    }

    platform::VideoLayer videoLayer() const override {
        return {frame_.pixels.empty() ? nullptr : frame_.pixels.data(), frame_.width, frame_.height, false};
    }

private:
    static std::int64_t now() { return static_cast<std::int64_t>(ui::context().timeMs); }

    // ---- overlay

    void buildOverlay() {
        auto* middle = add<ui::Stack>();
        middle->width = ui::SizeSpec::fill();
        middle->height = ui::SizeSpec::fill();
        middle->horizontal = ui::CrossAlign::Center;
        middle->vertical = ui::CrossAlign::Center;
        spinner_ = middle->add<ui::Spinner>();
        countdown_ = middle->add<ui::Label>("", ui::FontStyle{ui::theme().fontTitle, ui::FontWeight::SemiBold});
        countdown_->background = ui::theme().scrim;
        countdown_->cornerRadius = ui::theme().radius;
        countdown_->padding = ui::Insets::symmetric(ui::theme().spacingM, ui::theme().spacingL);
        countdown_->visible = false;

        auto* bottom = add<ui::Box>(ui::Axis::Column);
        bottom->width = ui::SizeSpec::fill();
        bottom->height = ui::SizeSpec::fill();
        bottom->align = ui::Align::End;
        bottom->padding = {0, 96, 64, 96};
        bottom->gap = ui::theme().spacingL;

        auto* skipRow = bottom->add<ui::Box>(ui::Axis::Row);
        skipRow->width = ui::SizeSpec::fill();
        skipRow->align = ui::Align::End;
        skip_ = skipRow->add<ui::Button>("", [this] { pressSkip(); });
        skip_->setPrimary(true);
        skip_->visible = false;

        bar_ = bottom->add<ui::Box>(ui::Axis::Column);
        bar_->width = ui::SizeSpec::fill();
        bar_->background = ui::withAlpha(ui::theme().surfaceRaised, 0xD9);
        bar_->cornerRadius = ui::theme().radius;
        bar_->padding = ui::Insets::all(ui::theme().spacingL);
        bar_->gap = ui::theme().spacingM;
        title_ = bar_->add<ui::Label>(barTitle(), ui::FontStyle{ui::theme().fontTitle, ui::FontWeight::SemiBold});
        progress_ = bar_->add<ui::ProgressBar>();
        progress_->width = ui::SizeSpec::fill();
        time_ = bar_->add<ui::Label>("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        time_->color = ui::theme().textSecondary;
    }

    std::string barTitle() const {
        if (live_ && item_.currentProgram) {
            return item_.name.empty() ? item_.currentProgram->name
                                      : item_.name + " \xC2\xB7 " + item_.currentProgram->name;
        }
        if (item_.type == jellyfin::ItemType::Episode && !item_.seriesName.empty()) {
            return item_.seriesName + " · " + item_.name;
        }
        return item_.name;
    }

    void showBar() {
        barUntil_ = now() + kBarMs;
        if (!bar_->visible) {
            bar_->visible = true;
            markDirty();
        }
    }

    void refreshBar(const player::Engine::State& state) {
        if (live_) {
            refreshLiveBar();
            return;
        }
        const double position = seek_.pending() ? seek_.target() : state.position;
        const std::string text = clockText(position) + " / " + clockText(state.duration);
        if (text != time_->text()) {
            time_->setText(text);
        }
        const float value = state.duration > 0 ? static_cast<float>(position / state.duration) : 0.0f;
        if (value != progress_->value) {
            progress_->value = value;
            progress_->markDirty();
        }
    }

    // a channel: the programme on now, its times and progress
    void refreshLiveBar() {
        const jellyfin::Item* program = item_.currentProgram.get();
        if (program == nullptr) {
            return;
        }
        const std::int64_t at = session().now();
        const std::string text = timeRange(*program, session().utcOffsetMinutes(program->startUtc));
        if (text != time_->text()) {
            time_->setText(text);
        }
        const auto value = static_cast<float>(progress(*program, at));
        if (value != progress_->value) {
            progress_->value = value;
            progress_->markDirty();
        }
    }

    // ---- controls

    void bindControls() {
        // no hints: the bar and the buttons say enough while a video plays
        bind(platform::Action::Confirm, "", [this] { return press([this] { togglePause(); }); });
        // a live channel cannot be sought: ← / → show the bar, L1 / R1 change channel
        bind(platform::Action::Left, "", [this] { return press([this] { live_ ? showBar() : seekBy(-1); }); });
        bind(platform::Action::Right, "", [this] { return press([this] { live_ ? showBar() : seekBy(1); }); });
        bind(platform::Action::L1, "", [this] { return press([this] { live_ ? zap(-1) : seekSeconds(-60); }); });
        bind(platform::Action::R1, "", [this] { return press([this] { live_ ? zap(1) : seekSeconds(60); }); });
        bind(platform::Action::Up, "", [this] { return press([this] { showBar(); }); });
        bind(platform::Action::Down, "", [this] { return press([this] { showBar(); }); });
        bind(platform::Action::Options, "", [this] { return press([this] { openTracks(); }); });
        bind(platform::Action::Menu, "", [this] { return press([this] { openTracks(); }); });
    }

    // Every key: during the next-episode countdown it cancels it and goes back; otherwise it does its job.
    template <typename Action>
    bool press(Action action) {
        if (countdownUntil_ > 0) {
            leave();
            return true;
        }
        action();
        return true;
    }

    void togglePause() {
        if (!engine_) {
            return;
        }
        const bool paused = !engine_->state().paused;
        engine_->setPaused(paused);
        showBar();
        reporter_.onPause(now(), playbackState(paused));
    }

    void seekBy(int direction) {
        const std::int64_t t = now();
        const bool held = direction == lastDirection_ && t - lastSeekPressMs_ < kHoldGapMs;
        if (!held) {
            holdStartMs_ = t;
        }
        lastDirection_ = direction;
        lastSeekPressMs_ = t;
        seekSeconds(direction * player::seekStep(t - holdStartMs_));
    }

    void seekSeconds(double delta) {
        if (!engine_) {
            return;
        }
        const player::Engine::State state = engine_->state();
        seek_.press(delta, now(), state.position, state.duration);
        showBar();
        refreshBar(state);
    }

    // the previous or next channel (wrapping) takes this screen's place
    void zap(int step) {
        if (channels_.size() < 2) {
            return;
        }
        int index = 0;
        for (std::size_t i = 0; i < channels_.size(); ++i) {
            if (channels_[i].id == item_.id) {
                index = static_cast<int>(i);
            }
        }
        const int count = static_cast<int>(channels_.size());
        const jellyfin::Item next = channels_[static_cast<std::size_t>(((index + step) % count + count) % count)];
        AppSession* appSession = &session();
        later([appSession, next] {
            if (ui::Application* app = ui::Application::current()) {
                app->replaceTop(makePlayer(*appSession, next, 0));
            }
        });
    }

    void pressSkip() {
        if (shownButton_.kind == player::SegmentButton::NextEpisode) {
            playNext();
        } else if (shownButton_.kind != player::SegmentButton::None && engine_) {
            engine_->seekTo(shownButton_.seekTo);
            reporter_.onSeek(now(), playbackState());
            hideSkip();
        }
    }

    // ---- loading

    void start(std::int64_t startTicks) {
        const auto client = session().client();
        if (!client) {
            showError();
            return;
        }
        if (!engine_ && !createEngine(*client)) {
            showError();
            return;
        }
        session().music().stop();  // a video or a channel takes over the sound
        const jellyfin::Item item = item_;
        jellyfin::PlaybackRequest request;
        request.startTicks = startTicks;
        request.audioIndex = audioIndex_;
        request.subtitleIndex = subtitleIndex_;
        request.maxBitrate = session().settings().maxBitrate;
        request.liveTv = live_;
        player::DirectPlayLimits limits;
        limits.maxBitrate = request.maxBitrate;
        request.deviceProfileJson = player::deviceProfileJson(limits);
        const core::CancelToken token = cancelToken();
        const std::shared_ptr<LiveHandOff> handOff = handOff_;
        run<Loaded>(
            [client, item, request, token, handOff] {
                Loaded loaded;
                loaded.info = client->playbackInfo(item.id, request, token);
                if (!loaded.info.ok()) {
                    return loaded;
                }
                loaded.choice = player::chooseStream(client->session(), item.id, loaded.info.value());
                const auto& sources = loaded.info.value().sources;
                const std::string opened = sources.empty() ? std::string() : sources.front().liveStreamId;
                if (!opened.empty()) {
                    std::unique_lock<std::mutex> lock(handOff->mutex);
                    if (handOff->screenGone || !loaded.choice.ok()) {
                        lock.unlock();
                        closeOnServer(client, opened);
                    } else {
                        handOff->pending = opened;
                    }
                }
                return loaded;
            },
            [this, startTicks](Loaded loaded) { onLoaded(std::move(loaded), startTicks); });
        if (!extrasLoaded_) {
            extrasLoaded_ = true;
            const bool live = live_;
            run<Extras>(
                [client, item, live, token] {
                    Extras extras;
                    if (live) {
                        auto channels = client->liveTvChannels(token);
                        if (channels.ok()) {
                            extras.channels = std::move(channels.value().items);
                        }
                        return extras;
                    }
                    auto segments = client->mediaSegments(item.id, token);
                    if (segments.ok()) {
                        extras.segments = std::move(segments.value());
                    }
                    if (item.type == jellyfin::ItemType::Episode && !item.seriesId.empty() && !item.seasonId.empty()) {
                        const auto episodes = client->episodes(item.seriesId, item.seasonId, token);
                        if (episodes.ok()) {
                            const auto& list = episodes.value().items;
                            for (std::size_t i = 0; i + 1 < list.size(); ++i) {
                                if (list[i].id == item.id) {
                                    extras.next = list[i + 1];
                                }
                            }
                        }
                    }
                    return extras;
                },
                [this](Extras extras) {
                    segments_ = std::move(extras.segments);
                    next_ = std::move(extras.next);
                    channels_ = std::move(extras.channels);
                    for (const auto& channel : channels_) {
                        if (channel.id == item_.id) {  // opened from a programme card: the channel as listed
                            item_.name = channel.name;
                            item_.currentProgram = channel.currentProgram;
                            title_->setText(barTitle());
                        }
                    }
                });
        }
    }

    bool createEngine(const jellyfin::Client& client) {
        // the frame loop polls the engine every frame
        engine_ = player::Engine::create(engineOptionsFor(client, session().synchronous), [] {});
        return engine_ != nullptr;
    }

    void onLoaded(Loaded loaded, std::int64_t startTicks) {
        if (!loaded.info.ok() || !loaded.choice.ok()) {
            const core::Error& error = !loaded.info.ok() ? loaded.info.error() : loaded.choice.error();
            PF_LOG_WARN("player: %s cannot be played: %s", item_.id.c_str(), error.message.c_str());
            showError();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(handOff_->mutex);
            handOff_->pending.clear();
        }
        // a restart (other tracks, retry): the previous play session ends, its live stream closes
        if (reporter_.started()) {
            reporter_.stop(playbackState());
            reporter_ = player::Reporter(
                [this](player::ReportKind kind, const jellyfin::PlaybackState& state) { send(kind, state); });
        }
        if (choice_.liveStreamId != loaded.choice.value().liveStreamId) {
            closeLiveStream();
        }
        choice_ = loaded.choice.value();
        liveClosed_ = false;
        source_ = loaded.info.value().sources.front();
        if (audioIndex_ < 0) {
            audioIndex_ = source_.defaultAudioIndex;
        }
        if (!subtitleChosen_) {
            subtitleIndex_ = source_.defaultSubtitleIndex;
        }
        engine_->load(choice_.url, static_cast<double>(startTicks) / player::kTicksPerSecond);
        if (session().synchronous) {
            showFirstPictureNow();
        }
    }

    // Test mode: the first picture before the next frame, then paused on it (deterministic screenshots).
    void showFirstPictureNow() {
        bool shown = false;
        for (int i = 0; i < 2000 && !shown; ++i) {
            for (player::EngineEvent event : engine_->drainEvents()) {
                onEngineEvent(event);
                shown = shown || event == player::EngineEvent::Error;
            }
            if (engine_ && engine_->renderIfDue(frame_)) {
                shown = true;
                onEngineEvent(player::EngineEvent::FirstFrame);
            }
            if (!shown) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        if (engine_) {
            engine_->setPaused(true);
        }
    }

    void onEngineEvent(player::EngineEvent event) {
        switch (event) {
        case player::EngineEvent::FileLoaded:
            applyTracks();
            break;
        case player::EngineEvent::FirstFrame:
            PF_LOG_INFO("player: %s", engine_->describe().c_str());
            reporter_.onFirstFrame(now(), playbackState());
            break;
        case player::EngineEvent::Seeked:
            break;
        case player::EngineEvent::EndOfFile:
            ended();
            break;
        case player::EngineEvent::Error:
            showError();
            break;
        }
    }

    // External subtitles in order (their mpv ids follow the embedded ones), then the chosen tracks.
    void applyTracks() {
        for (const player::ExternalSubtitle& subtitle : choice_.externalSubtitles) {
            engine_->addSubtitle(subtitle.url, false);
        }
        for (const player::TrackItem& track : player::audioTracks(source_, audioIndex_)) {
            if (track.current && !choice_.transcoding) {
                engine_->setAudioTrack(track.mpvId);
            }
        }
        int sid = 0;
        for (const player::TrackItem& track : player::subtitleTracks(source_, subtitleIndex_, "")) {
            if (track.current && track.jellyfinIndex >= 0) {
                sid = track.mpvId;
            }
        }
        engine_->setSubtitleTrack(sid);
    }

    // ---- per frame

    void tick(bool freshPicture) {
        const std::int64_t t = now();
        const player::Engine::State state = engine_->state();
        if (const auto target = seek_.due(t)) {
            engine_->seekTo(*target);
            reporter_.onSeek(t, playbackState());
        }
        reporter_.tick(t, playbackState());
        if (state.buffering != spinner_->visible || (!frameShown_ && !spinner_->visible)) {
            spinner_->visible = state.buffering || !frameShown_;
            markDirty();
        }
        frameShown_ = frameShown_ || freshPicture;
        if (bar_->visible) {
            refreshBar(state);
            if (t > barUntil_ && !state.paused && !seek_.pending()) {
                bar_->visible = false;
                markDirty();
            }
        }
        updateSkip(state, t);
        updateCountdown(t);
        logStats(freshPicture, t);
    }

    void updateSkip(const player::Engine::State& state, std::int64_t t) {
        const player::SegmentButton wanted = player::buttonAt(state.position, state.duration, segments_,
                                                              next_.has_value());
        const player::SegmentButton shown = buttonTimer_.visible(wanted, t);
        if (shown.kind == shownButton_.kind && shown.seekTo == shownButton_.seekTo) {
            return;
        }
        shownButton_ = shown;
        if (shown.kind == player::SegmentButton::None) {
            hideSkip();
            return;
        }
        const char* key = shown.kind == player::SegmentButton::SkipIntro   ? "player.skip_intro"
                          : shown.kind == player::SegmentButton::SkipOutro ? "player.skip_credits"
                                                                           : "player.next_episode";
        skip_->setText(ui::tr(key));
        skip_->visible = true;
        markDirty();
        if (ui::Application* app = ui::Application::current(); app != nullptr && !app->hasModal()) {
            app->focus().focus(skip_);
        }
    }

    void hideSkip() {
        shownButton_ = {};
        if (!skip_->visible) {
            return;
        }
        skip_->visible = false;
        markDirty();
        if (ui::Application* app = ui::Application::current(); app != nullptr && !app->hasModal()) {
            app->focus().focus(this);
        }
    }

    void ended() {
        if (next_) {
            countdownUntil_ = now() + kCountdownMs;
            updateCountdown(now());
            return;
        }
        leave();
    }

    void updateCountdown(std::int64_t t) {
        if (countdownUntil_ <= 0) {
            return;
        }
        if (t >= countdownUntil_) {
            playNext();
            return;
        }
        const auto seconds = static_cast<long>((countdownUntil_ - t + 999) / 1000);
        const std::string text = ui::tr("player.next_in", {std::to_string(seconds)});
        if (text != countdown_->text() || !countdown_->visible) {
            countdown_->setText(text);
            countdown_->visible = true;
            markDirty();
        }
    }

    // ---- leaving

    // Both destroy this screen: they run on the next frame (they are reached from the middle of renderVideo and
    // of key handlers, which still use the screen afterwards).
    void leave() {
        later([] {
            if (ui::Application* app = ui::Application::current()) {
                app->pop();
            }
        });
    }

    void playNext() {
        if (!next_) {
            return;
        }
        countdownUntil_ = 0;
        AppSession* appSession = &session();
        const jellyfin::Item next = *next_;
        later([appSession, next] {
            if (ui::Application* app = ui::Application::current()) {
                app->pop();
                app->push(makePlayer(*appSession, next, 0));
            }
        });
    }

    void release() {
        if (ui::Application* app = ui::Application::current(); app != nullptr && app->videoSource() == this) {
            app->setVideoSource(nullptr);
        }
        if (engine_) {
            reporter_.stop(playbackState());
            engine_.reset();
        }
        closeLiveStream();
        std::string pending;
        {
            std::lock_guard<std::mutex> lock(handOff_->mutex);
            handOff_->screenGone = true;
            pending.swap(handOff_->pending);
        }
        closeLiveStream(pending);
    }

    // the stream being played, once
    void closeLiveStream() {
        if (!liveClosed_) {
            liveClosed_ = true;
            closeLiveStream(choice_.liveStreamId);
        }
    }

    void closeLiveStream(const std::string& id) {
        const auto client = session().client();
        if (id.empty() || !client) {
            return;
        }
        session().submit([client, id] { closeOnServer(client, id); });
    }

    // ---- server side

    jellyfin::PlaybackState playbackState(std::optional<bool> paused = std::nullopt) const {
        jellyfin::PlaybackState state;
        state.itemId = item_.id;
        state.mediaSourceId = choice_.mediaSourceId;
        state.playSessionId = choice_.playSessionId;
        state.liveStreamId = choice_.liveStreamId;
        state.transcoding = choice_.transcoding;
        state.audioIndex = audioIndex_;
        state.subtitleIndex = subtitleIndex_;
        if (engine_) {
            const player::Engine::State engine = engine_->state();
            state.positionTicks = static_cast<std::int64_t>(engine.position * player::kTicksPerSecond);
            state.paused = paused.value_or(engine.paused);
        }
        return state;
    }

    // Reports go out on the worker pool and outlive the screen (the stop report is sent while it closes).
    void send(player::ReportKind kind, const jellyfin::PlaybackState& state) {
        const auto client = session().client();
        if (!client) {
            return;
        }
        session().submit([client, kind, state] { player::sendReport(*client, kind, state); });
    }

    // ---- tracks

    void openTracks() {
        if (!engine_ || source_.id.empty()) {
            return;
        }
        std::string audioName;
        for (const auto& track : player::audioTracks(source_, audioIndex_)) {
            audioName = track.current ? track.label : audioName;
        }
        std::string subtitleName;
        for (const auto& track : player::subtitleTracks(source_, subtitleIndex_, ui::tr("player.subtitles_off"))) {
            subtitleName = track.current ? track.label : subtitleName;
        }
        ui::Application::current()->showModal(ui::makeDialog(
            barTitle(), ui::tr("player.audio") + " : " + audioName + "\n" + ui::tr("player.subtitles") + " : " +
                            subtitleName,
            {{ui::tr("player.audio"), [this] { chooseTrack(jellyfin::StreamType::Audio); }},
             {ui::tr("player.subtitles"), [this] { chooseTrack(jellyfin::StreamType::Subtitle); }},
             {ui::tr("ui.cancel"), {}}}));
    }

    void chooseTrack(jellyfin::StreamType type) {
        const bool audio = type == jellyfin::StreamType::Audio;
        const std::vector<player::TrackItem> tracks =
            audio ? player::audioTracks(source_, audioIndex_)
                  : player::subtitleTracks(source_, subtitleIndex_, ui::tr("player.subtitles_off"));
        std::vector<std::pair<std::string, std::function<void()>>> buttons;
        for (const player::TrackItem& track : tracks) {
            buttons.emplace_back((track.current ? "✓ " : "") + track.label,
                                 [this, track, audio] { pickTrack(track, audio); });
        }
        buttons.emplace_back(ui::tr("ui.cancel"), std::function<void()>{});
        // opened next frame: the first dialog closes after its button's action
        later([audio, buttons = std::move(buttons)]() mutable {
            ui::Application::current()->showModal(ui::makeDialog(
                ui::tr(audio ? "player.audio" : "player.subtitles"), "", std::move(buttons)));
        });
    }

    void pickTrack(const player::TrackItem& track, bool audio) {
        const jellyfin::MediaStream* stream = nullptr;
        for (const jellyfin::MediaStream& candidate : source_.streams) {
            stream = candidate.index == track.jellyfinIndex ? &candidate : stream;
        }
        if (audio) {
            audioIndex_ = track.jellyfinIndex;
        } else {
            subtitleIndex_ = track.jellyfinIndex;
            subtitleChosen_ = true;
        }
        if (stream != nullptr && player::needsRestart(choice_.transcoding, *stream)) {
            restart();
            return;
        }
        if (audio) {
            engine_->setAudioTrack(track.mpvId);
        } else {
            engine_->setSubtitleTrack(track.mpvId);
        }
        reporter_.onTrackChange(now(), playbackState());
    }

    // A new PlaybackInfo for the chosen tracks, from the current position.
    void restart() {
        const auto position = static_cast<std::int64_t>(engine_->state().position * player::kTicksPerSecond);
        frameShown_ = false;
        start(position);
    }

    // ---- errors

    void showError() {
        ui::Application* app = ui::Application::current();
        if (app == nullptr || errorShown_) {
            return;
        }
        errorShown_ = true;
        app->showModal(ui::makeDialog(
            ui::tr("player.error"), "",
            {{ui::tr("player.retry"),
              [this] {
                  errorShown_ = false;
                  const auto position = engine_ ? static_cast<std::int64_t>(engine_->state().position * player::kTicksPerSecond)
                                                : startTicks_;
                  start(position > 0 ? position : startTicks_);
              }},
             {ui::tr("player.back"), [this] { leave(); }}}));
    }

    void logStats(bool fresh, std::int64_t t) {
        if (fresh) {
            ++pictures_;
        }
        if (statsStart_ == 0) {
            statsStart_ = t;
            return;
        }
        if (t - statsStart_ < kStatsMs || session().synchronous) {
            return;
        }
        const player::Engine::Stats stats = engine_->stats();
        PF_LOG_DEBUG("player: fps %.1f decoder-drops %ld output-drops %ld render %.1f ms",
                     pictures_ * 1000.0 / static_cast<double>(t - statsStart_), stats.decoderDrops,
                     stats.outputDrops, stats.renderMs);
        statsStart_ = t;
        pictures_ = 0;
    }

    jellyfin::Item item_;
    bool live_;  // a Live TV channel
    bool liveClosed_ = false;
    std::shared_ptr<LiveHandOff> handOff_ = std::make_shared<LiveHandOff>();
    std::vector<jellyfin::Item> channels_;
    std::int64_t startTicks_;
    player::Reporter reporter_;
    std::unique_ptr<player::Engine> engine_;
    player::VideoFrame frame_;
    player::StreamChoice choice_;
    jellyfin::MediaSource source_;
    std::vector<jellyfin::MediaSegment> segments_;
    std::optional<jellyfin::Item> next_;
    player::SeekAccumulator seek_;
    player::ButtonTimer buttonTimer_;
    player::SegmentButton shownButton_;

    ui::Spinner* spinner_ = nullptr;
    ui::Label* countdown_ = nullptr;
    ui::Button* skip_ = nullptr;
    ui::Box* bar_ = nullptr;
    ui::Label* title_ = nullptr;
    ui::ProgressBar* progress_ = nullptr;
    ui::Label* time_ = nullptr;

    bool started_ = false;
    bool extrasLoaded_ = false;
    bool frameShown_ = false;
    bool errorShown_ = false;
    bool subtitleChosen_ = false;
    int audioIndex_ = -1;
    int subtitleIndex_ = -1;
    std::int64_t barUntil_ = 0;
    std::int64_t countdownUntil_ = 0;
    std::int64_t lastSeekPressMs_ = 0;
    std::int64_t holdStartMs_ = 0;
    int lastDirection_ = 0;
    std::int64_t statsStart_ = 0;
    int pictures_ = 0;
};

}  // namespace

std::unique_ptr<ui::Screen> makePlayer(AppSession& session, jellyfin::Item item, std::int64_t startTicks) {
    return std::make_unique<PlayerScreen>(session, std::move(item), startTicks);
}

}  // namespace playfin::app
