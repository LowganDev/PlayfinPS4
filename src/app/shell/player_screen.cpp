#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "app/shell/downloads.hpp"
#include "app/shell/usb.hpp"
#include "app/shell/tv_guide.hpp"
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
#include "ui/image_view.hpp"
#include "ui/label.hpp"
#include "ui/theme.hpp"
#include "ui/video_source.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

constexpr std::int64_t kBarMs = 4000;          // the bar hides after 4 s without input
constexpr std::int64_t kHoldGapMs = 450;       // presses closer than this are one held button (first repeat: 400)
constexpr std::int64_t kStatsMs = 1000;
constexpr std::int64_t kDirectStartMs = 8000;  // a channel's own stream: no picture by then, the server's relay
constexpr std::int64_t kStallMs = 15000;       // a channel without a new picture this long is opened again

jellyfin::PlayMethod playMethod(const std::string& setting) {
    if (setting == "direct") {
        return jellyfin::PlayMethod::DirectOnly;
    }
    return setting == "transcode" ? jellyfin::PlayMethod::TranscodeOnly : jellyfin::PlayMethod::Auto;
}

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
    std::optional<jellyfin::Item> previous;
    std::optional<jellyfin::Item> next;
    std::vector<jellyfin::Item> channels;  // live: the channels to change to, with their programme on now
};

class PlayerScreen : public ShellScreen, public ui::VideoSource {
public:
    PlayerScreen(AppSession& session, jellyfin::Item item, std::int64_t startTicks, std::string mediaSourceId)
        : ShellScreen(session),
          item_(std::move(item)),
          mediaSourceId_(std::move(mediaSourceId)),
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
        platform::VideoLayer layer{frame_.pixels.empty() ? nullptr : frame_.pixels.data(), frame_.width,
                                   frame_.height, false};
        const std::string& fit = session().settings().pictureFit;
        layer.fit = fit == "fill"      ? platform::VideoFit::Fill
                    : fit == "stretch" ? platform::VideoFit::Stretch
                                       : platform::VideoFit::Fit;
        layer.scale = session().settings().pictureSize / 100.0;
        return layer;
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

        auto* bottom = add<ui::Box>(ui::Axis::Column);
        bottom->width = ui::SizeSpec::fill();
        bottom->height = ui::SizeSpec::fill();
        bottom->align = ui::Align::End;
        bottom->padding = {0, 96, 104, 96};  // the hints bar below the bar, on the video
        bottom->gap = ui::theme().spacingL;

        buildNextCard(*bottom);
        buildHoldPanel(*bottom);

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

    // L2 / R2 held: what comes after 3 s, filling up meanwhile
    void buildHoldPanel(ui::Box& bottom) {
        auto* row = bottom.add<ui::Box>(ui::Axis::Row);
        row->width = ui::SizeSpec::fill();
        row->align = ui::Align::Center;
        holdPanel_ = row->add<ui::Box>(ui::Axis::Column);
        holdPanel_->background = ui::withAlpha(ui::theme().surfaceRaised, 0xF0);
        holdPanel_->cornerRadius = ui::theme().radius;
        holdPanel_->padding = ui::Insets::all(ui::theme().spacingM);
        holdPanel_->gap = ui::theme().spacingS;
        holdPanel_->visible = false;
        holdLabel_ = holdPanel_->add<ui::Label>("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::SemiBold});
        holdProgress_ = holdPanel_->add<ui::ProgressBar>();
        holdProgress_->width = ui::SizeSpec::fixed(420);
    }

    // "Épisode suivant": its picture, its number and title, the seconds left; ✕ plays it now, ◯ cancels
    void buildNextCard(ui::Box& bottom) {
        auto* row = bottom.add<ui::Box>(ui::Axis::Row);
        row->width = ui::SizeSpec::fill();
        row->align = ui::Align::End;
        nextCard_ = row->add<ui::Box>(ui::Axis::Row);
        nextCard_->background = ui::withAlpha(ui::theme().surfaceRaised, 0xF0);
        nextCard_->cornerRadius = ui::theme().radius;
        nextCard_->padding = ui::Insets::all(ui::theme().spacingM);
        nextCard_->gap = ui::theme().spacingL;
        nextCard_->crossAlign = ui::CrossAlign::Center;
        nextCard_->focusable = true;
        nextCard_->drawsOwnFocus = true;
        nextCard_->visible = false;
        nextImage_ = nextCard_->add<ui::ImageView>();
        nextImage_->width = ui::SizeSpec::fixed(320);
        nextImage_->height = ui::SizeSpec::fixed(180);
        nextImage_->cornerRadius = 8;
        nextImage_->clip = true;
        nextImage_->background = ui::theme().surface;
        auto* texts = nextCard_->add<ui::Box>(ui::Axis::Column);
        texts->gap = ui::theme().spacingS;
        auto* heading = texts->add<ui::Label>(ui::tr("player.next_episode"),
                                              ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        heading->color = ui::theme().textSecondary;
        nextTitle_ = texts->add<ui::Label>("", ui::FontStyle{ui::theme().fontTitle, ui::FontWeight::SemiBold});
        nextTitle_->width = ui::SizeSpec::fixed(560);
        nextTitle_->maxLines = 2;
        nextCountdown_ = texts->add<ui::Label>("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::SemiBold});
        nextCountdown_->color = ui::theme().accent;
        nextCard_->bind(platform::Action::Confirm, ui::tr("player.play_now"), [this] {
            playNext();
            return true;
        });
        nextCard_->bind(platform::Action::Back, ui::tr("ui.cancel"), [this] {
            nextCardState_.cancel();
            hideNextCard();
            return true;
        });
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

    // □: the bar (and its hints) away at once, even paused, to see the whole picture; again, it comes back
    void toggleBar() {
        if (!bar_->visible) {
            showBar();
            return;
        }
        bar_->visible = false;
        markDirty();
    }

    void refreshBar(const player::Engine::State& state) {
        if (live_) {
            refreshLiveBar();
            return;
        }
        const double position = seek_.pending() ? seek_.target() : state.position;
        std::string text = clockText(position) + " / " + clockText(state.duration);
        if (state.duration > 0) {  // when it would end, playing on from here
            const std::int64_t at = session().now();
            const auto end = at + static_cast<std::int64_t>(state.duration - position);
            text += " \xC2\xB7 " + ui::tr("player.ends_at", {clock24(end, session().utcOffsetMinutes(end))});
        }
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
        navigationHint = false;  // the hints list what each button does, while the bar shows
        const auto bar = [this] { return bar_ != nullptr && bar_->visible; };
        const auto video = [this, bar] { return bar() && !live_; };
        const auto channel = [this, bar] { return bar() && live_; };
        const auto playing = [this, bar] { return bar() && engine_ && !engine_->state().paused; };
        const auto paused = [this, bar] { return bar() && engine_ && engine_->state().paused; };
        const auto nothing = [] { return false; };  // a second label of a button: its first binding acts
        bind(platform::Action::Confirm, ui::tr("player.hint.pause"), [this] { return press([this] { togglePause(); }); },
             playing);
        bind(platform::Action::Confirm, ui::tr("player.hint.play"), nothing, paused);
        // a live channel cannot be sought: ← / → show the bar, L1 / R1 change channel
        bind(platform::Action::Left, ui::tr("player.hint.seek"),
             [this] { return press([this] { live_ ? showBar() : seekPress(-1, platform::Action::Left); }); }, video);
        bind(platform::Action::Right, ui::tr("player.hint.seek"),
             [this] { return press([this] { live_ ? showBar() : seekPress(1, platform::Action::Right); }); }, video);
        // L1 / R1: as ← / → on a video (held: repeated, longer and longer steps); a channel: the previous or next one
        bind(platform::Action::L1, ui::tr("player.hint.seek"), [this] { return press([this] { shoulder(-1); }); },
             video);
        bind(platform::Action::L1, ui::tr("player.hint.channel"), nothing, channel);
        bind(platform::Action::R1, ui::tr("player.hint.seek"), [this] { return press([this] { shoulder(1); }); },
             video);
        bind(platform::Action::R1, ui::tr("player.hint.channel"), nothing, channel);
        // L2 / R2 held 3 s: the previous or next episode (or channel)
        const auto skippable = [this, bar] { return bar() && (previous_ || next_ || channels_.size() > 1); };
        bind(platform::Action::L2, ui::tr(live_ ? "player.hint.hold_channel" : "player.hint.hold_episode"),
             [this] { return press([this] { startHold(-1); }); }, skippable);
        bind(platform::Action::R2, ui::tr(live_ ? "player.hint.hold_channel" : "player.hint.hold_episode"),
             [this] { return press([this] { startHold(1); }); }, skippable);
        // a channel: ↑ / ↓ set the volume; a video: they show the bar
        bind(platform::Action::Up, ui::tr("player.hint.volume"),
             [this] { return press([this] { live_ ? stepVolume(10) : showBar(); }); }, channel);
        bind(platform::Action::Down, ui::tr("player.hint.volume"),
             [this] { return press([this] { live_ ? stepVolume(-10) : showBar(); }); }, channel);
        bind(platform::Action::Options, ui::tr("player.hint.options"), [this] { return press([this] { openTracks(); }); },
             bar);
        bind(platform::Action::Menu, "", [this] { return press([this] { openTracks(); }); });
        bind(platform::Action::Context, ui::tr("player.hint.hide"), [this] { return press([this] { toggleBar(); }); },
             bar);
        // ◯ leaves (the application's back); its hint while the bar shows
        bind(platform::Action::Back, ui::tr("player.hint.stop"), [] { return false; }, bar);
    }

    template <typename Action>
    bool press(Action action) {
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

    void shoulder(int direction) {
        if (live_) {
            zap(direction);
            return;
        }
        seekPress(direction, direction < 0 ? platform::Action::L1 : platform::Action::R1);
    }

    // ← / → and L1 / R1 alike: one seek, then ours repeats while the button is held (the platform's repeats of a
    // held arrow come on frames: ignored)
    void seekPress(int direction, platform::Action button) {
        if (ui::Application::current() != nullptr && ui::Application::current()->repeating()) {
            return;
        }
        seekBy(direction);
        seekRepeat_.press(direction, now());
        seekButton_ = button;
    }

    // L2 / R2 down: the panel fills for 3 s; released before, nothing happens
    void startHold(int direction) {
        const bool possible = live_ ? channels_.size() > 1 : (direction < 0 ? previous_.has_value() : next_.has_value());
        if (!possible || leaving_) {
            return;
        }
        hold_.press(direction, now());
        const char* key = live_ ? (direction < 0 ? "player.hold.previous_channel" : "player.hold.next_channel")
                                : (direction < 0 ? "player.hold.previous_episode" : "player.hold.next_episode");
        holdLabel_->setText(ui::tr(key));
        holdProgress_->value = 0;
        holdPanel_->visible = true;
        markDirty();
    }

    // every frame: the seek and shoulder buttons held (repeat, hold) or released
    void updateHeld(std::int64_t t) {
        ui::Application* app = ui::Application::current();
        if (app == nullptr) {
            return;
        }
        if (seekRepeat_.direction() != 0) {
            if (!app->held(seekButton_) || app->hasModal()) {
                seekRepeat_.release();
            } else {
                for (int due = seekRepeat_.poll(t); due > 0; --due) {
                    seekBy(seekRepeat_.direction());
                }
            }
        }
        if (hold_.direction() == 0) {
            return;
        }
        const int direction = hold_.direction();
        const auto button = direction < 0 ? platform::Action::L2 : platform::Action::R2;
        if (!app->held(button) || app->hasModal()) {
            hold_.release();
            holdPanel_->visible = false;
            markDirty();
            return;
        }
        holdProgress_->value = static_cast<float>(hold_.progress(t));
        holdProgress_->markDirty();
        if (hold_.due(t)) {
            holdPanel_->visible = false;
            markDirty();
            if (live_) {
                zap(direction);
            } else if (direction < 0) {
                playItem(previous_);
            } else {
                playNext();
            }
        }
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
        if (shownButton_.kind != player::SegmentButton::None && engine_) {
            engine_->seekTo(shownButton_.seekTo);
            reporter_.onSeek(now(), playbackState());
            hideSkip();
        }
    }

    // ---- loading

    void start(std::int64_t startTicks) {
        if (!item_.localUrl.empty()) {  // a file on a USB drive
            startLocal(item_.localUrl, startTicks);
            findUsbSubtitles(item_.localUrl);
            return;
        }
        if (!live_) {  // a download plays from the console, server or not
            if (const auto file = session().downloads().localFile(item_.id)) {
                startLocal(*file, startTicks);
                return;
            }
        }
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
        request.mediaSourceId = mediaSourceId_;
        request.audioIndex = audioIndex_;
        request.subtitleIndex = subtitleIndex_;
        request.maxBitrate = session().settings().maxBitrate;
        request.liveTv = live_;
        if (!live_) {  // a channel is the server's relay or its own stream, whatever the setting
            request.method = playMethod(session().settings().playMethod);
        }
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
                loaded.choice = player::chooseStream(client->session(), item.id, loaded.info.value(),
                                                     player::MediaKind::Video, request.method);
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
                    // without the server's markers: the chapters named as an intro or credits
                    if (extras.segments.empty()) {
                        std::vector<jellyfin::Chapter> chapters = item.chapters;
                        std::int64_t runTicks = item.runTimeTicks;
                        if (chapters.empty()) {
                            if (auto details = client->item(item.id, token); details.ok()) {
                                chapters = std::move(details.value().chapters);
                                runTicks = details.value().runTimeTicks > 0 ? details.value().runTimeTicks : runTicks;
                            }
                        }
                        extras.segments = player::segmentsFromChapters(chapters, runTicks);
                        if (!extras.segments.empty()) {
                            PF_LOG_INFO("player: %zu segments from the chapters", extras.segments.size());
                        }
                    }
                    // the episodes around this one, across seasons (the last of a season goes on to the next)
                    if (item.type == jellyfin::ItemType::Episode && !item.seriesId.empty()) {
                        const auto episodes = client->episodes(item.seriesId, "", token);
                        if (episodes.ok()) {
                            auto around = jellyfin::adjacentEpisodes(episodes.value().items, item.id);
                            extras.previous = std::move(around.previous);
                            extras.next = std::move(around.next);
                        }
                    }
                    return extras;
                },
                [this](Extras extras) {
                    segments_ = std::move(extras.segments);
                    previous_ = std::move(extras.previous);
                    next_ = std::move(extras.next);
                    channels_ = std::move(extras.channels);
                    session().tvGuide().fill(channels_, session().now());
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

    void startLocal(const std::string& file, std::int64_t startTicks) {
        local_ = true;
        if (!engine_) {
            engine_ = player::Engine::create(
                engineOptionsLocal(session().synchronous, usbOpener(session().usbFtp)), [] {});
            if (!engine_) {
                showError();
                return;
            }
            engine_->setVolume(session().settings().volume);
        }
        session().music().stop();
        PF_LOG_INFO("player: %s from %s", item_.id.c_str(),
                    file.compare(0, std::strlen(player::kUsbScheme), player::kUsbScheme) == 0 ? "a USB drive"
                                                                                               : "the console");
        engine_->load(file, static_cast<double>(startTicks) / player::kTicksPerSecond);
        if (session().synchronous) {
            showFirstPictureNow();
        }
    }

    bool createEngine(const jellyfin::Client& client) {
        // the frame loop polls the engine every frame
        player::EngineOptions options = engineOptionsFor(client, session().synchronous);
        options.reconnect = live_;
        engine_ = player::Engine::create(options, [] {});
        if (engine_) {
            engine_->setVolume(session().settings().volume);
        }
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
        if (live_ && !choice_.sourceUrl.empty() && startDirect()) {
            return;
        }
        engine_->load(choice_.url, static_cast<double>(startTicks) / player::kTicksPerSecond);
        if (session().synchronous) {
            showFirstPictureNow();
        }
    }

    // ---- a live channel's own stream (an M3U tuner's): straight from its host, the server's relay as the fallback

    bool startDirect() {
        // its own engine: the Jellyfin header must not go to another host
        auto direct = player::Engine::create(engineOptionsDirect(session().synchronous), [] {});
        if (!direct) {
            return false;
        }
        engine_ = std::move(direct);
        engine_->setVolume(session().settings().volume);
        direct_ = true;
        directSinceMs_ = now();
        PF_LOG_INFO("player: the channel's own stream, without the server");
        engine_->load(choice_.sourceUrl, 0);
        if (session().synchronous) {
            showFirstPictureNow();
        }
        return true;
    }

    // the own stream failed or shows nothing: the server's relay (next frame: called from the engine's events)
    void fallBackToServer() {
        if (!direct_) {
            return;
        }
        direct_ = false;
        PF_LOG_INFO("player: the channel's own stream did not play, through the server");
        later([this] {
            const auto client = session().client();
            if (!client || !createEngine(*client)) {
                showError();
                return;
            }
            frameShown_ = false;
            engine_->load(choice_.url, 0);
            if (session().synchronous) {
                showFirstPictureNow();
            }
        });
    }

    // a channel whose picture stopped (the stream dropped): opened again, every kStallMs at most; its own stream
    // that shows nothing within kDirectStartMs: the server's relay
    void watchLive(const player::Engine::State& state, bool freshPicture, std::int64_t t) {
        if (direct_ && !frameShown_ && !freshPicture && t - directSinceMs_ > kDirectStartMs) {
            fallBackToServer();
            return;
        }
        if (freshPicture || !frameShown_ || state.paused) {
            lastPictureMs_ = t;
            return;
        }
        if (t - lastPictureMs_ > kStallMs) {
            lastPictureMs_ = t;
            PF_LOG_WARN("player: no picture for %lld s, the channel is opened again",
                        static_cast<long long>(kStallMs / 1000));
            engine_->load(direct_ ? choice_.sourceUrl : choice_.url, 0);
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
            if (!local_) {  // a download played offline tells the server nothing
                reporter_.onFirstFrame(now(), playbackState());
            }
            break;
        case player::EngineEvent::Seeked:
            break;
        case player::EngineEvent::EndOfFile:
            ended();
            break;
        case player::EngineEvent::Error:
            if (direct_) {
                fallBackToServer();
                break;
            }
            showError();
            break;
        }
    }

    // External subtitles in order (their mpv ids follow the embedded ones), then the chosen tracks.
    void applyTracks() {
        if (local_) {
            fileLoaded_ = true;
            addUsbSubtitles();
            return;  // the file's own default tracks
        }
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

    // ---- subtitle files beside a video on a USB drive ("Film.fr.srt" for "Film.mkv"), offered in the track menu

    void findUsbSubtitles(const std::string& url) {
        const std::string scheme = player::kUsbScheme;
        const auto ftp = session().usbFtp;
        if (!ftp || url.compare(0, scheme.size(), scheme) != 0) {
            return;
        }
        const std::string path = url.substr(scheme.size());
        const std::size_t slash = path.rfind('/');
        if (slash == std::string::npos) {
            return;
        }
        const std::string dir = path.substr(0, slash);
        const std::string name = path.substr(slash + 1);
        const core::CancelToken token = cancelToken();
        run<std::vector<std::string>>(
            [ftp, dir, name, token] {
                std::vector<std::string> urls;
                const auto entries = ftp->list(dir, token);
                if (entries.ok()) {
                    for (const std::string& subtitle : usbSubtitlesFor(name, entries.value())) {
                        urls.push_back(usbUrl(dir + "/" + subtitle));
                    }
                }
                return urls;
            },
            [this](std::vector<std::string> urls) {
                usbSubtitles_ = std::move(urls);
                if (fileLoaded_) {
                    addUsbSubtitles();
                }
            });
    }

    void addUsbSubtitles() {
        if (!engine_ || usbSubtitles_.empty()) {
            return;
        }
        for (const std::string& url : usbSubtitles_) {
            engine_->addSubtitle(url, false);
        }
        PF_LOG_INFO("player: %zu subtitle files beside the video", usbSubtitles_.size());
        usbSubtitles_.clear();
    }

    // ---- per frame

    void tick(bool freshPicture) {
        const std::int64_t t = now();
        const player::Engine::State state = engine_->state();
        if (state.duration > 0) {
            lastPosition_ = state.position;
            lastDuration_ = state.duration;
        }
        updateHeld(t);
        if (const auto target = seek_.due(t)) {
            engine_->seekTo(*target);
            reporter_.onSeek(t, playbackState());
        }
        reporter_.tick(t, playbackState());
        if (state.buffering != spinner_->visible || (!frameShown_ && !spinner_->visible)) {
            spinner_->visible = state.buffering || !frameShown_;
            markDirty();
        }
        if (live_) {
            watchLive(state, freshPicture, t);
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
        updateNextCard(state);
        logStats(freshPicture, t);
    }

    void updateSkip(const player::Engine::State& state, std::int64_t t) {
        const player::SegmentButton wanted =
            nextCard_->visible ? player::SegmentButton{} : player::buttonAt(state.position, segments_);
        const player::SegmentButton shown = buttonTimer_.visible(wanted, t);
        if (shown.kind == shownButton_.kind && shown.seekTo == shownButton_.seekTo) {
            return;
        }
        shownButton_ = shown;
        if (shown.kind == player::SegmentButton::None) {
            hideSkip();
            return;
        }
        const char* key = shown.kind == player::SegmentButton::SkipIntro ? "player.skip_intro" : "player.skip_credits";
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
        // where the file really ended against the length announced (the next card counts down to that length)
        PF_LOG_INFO("player: end of file at %.1f s of %.1f s", lastPosition_, lastDuration_);
        if (nextCardState_.playsNextAtEnd(next_.has_value())) {
            playNext();
            return;
        }
        leave();
    }

    void updateNextCard(const player::Engine::State& state) {
        if (live_ || !frameShown_) {
            return;
        }
        const double position = seek_.pending() ? seek_.target() : state.position;
        const player::NextCard::View view = nextCardState_.at(position, state.duration, next_.has_value());
        if (!view.shown) {
            hideNextCard();
            return;
        }
        const std::string text = ui::tr("player.next_in", {std::to_string(view.seconds)});
        if (text != nextCountdown_->text()) {
            nextCountdown_->setText(text);
        }
        if (nextCard_->visible) {
            focusNextCard();  // shown under a dialog: focused once it closes
            return;
        }
        if (const auto client = session().client()) {
            nextImage_->setUrl(imageUrlFor(*client, *next_, true, 320, 180));
        }
        nextTitle_->setText(cardSubtitle(*next_));
        PF_LOG_INFO("player: next card at %.1f s of %.1f s", position, state.duration);
        nextCard_->visible = true;
        hideSkip();
        markDirty();
        focusNextCard();
    }

    // the card has the focus while it shows, unless the user went to the bar's buttons: a dialog closing gives the
    // focus back to the screen, and the card shown under it never had it
    void focusNextCard() {
        ui::Application* app = ui::Application::current();
        if (app == nullptr || app->hasModal() || app->top() != this) {
            return;
        }
        const ui::View* focused = app->focus().focused();
        if (focused == nullptr || focused == this || !isAncestorOf(focused)) {
            app->focus().focus(nextCard_);
        }
    }

    void hideNextCard() {
        if (!nextCard_->visible) {
            return;
        }
        nextCard_->visible = false;
        markDirty();
        if (ui::Application* app = ui::Application::current(); app != nullptr && !app->hasModal()) {
            app->focus().focus(this);
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

    void playNext() { playItem(next_); }

    // another episode takes this screen's place
    void playItem(const std::optional<jellyfin::Item>& item) {
        if (!item || leaving_) {
            return;
        }
        leaving_ = true;
        AppSession* appSession = &session();
        const jellyfin::Item next = *item;
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
        if (volumeUnsaved_) {
            volumeUnsaved_ = false;
            session().saveSettings();
        }
        if (engine_) {
            if (local_) {  // where it was left, for the next time it is played offline
                const player::Engine::State state = engine_->state();
                const bool nearEnd = state.duration > 0 && state.position > state.duration - 30;
                session().downloads().setPosition(
                    item_.id, nearEnd ? 0 : static_cast<std::int64_t>(state.position * player::kTicksPerSecond));
            }
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
        if (!engine_) {
            return;
        }
        if (source_.id.empty()) {  // a file without the server (a download, a USB drive's): the tracks mpv found
            std::vector<std::pair<std::string, std::function<void()>>> buttons;
            const auto tracks = engine_->tracks();
            const auto count = [&tracks](bool audio) {
                return std::count_if(tracks.begin(), tracks.end(), [audio](const auto& t) { return t.audio == audio; });
            };
            if (count(true) > 1) {
                buttons.emplace_back(ui::tr("player.audio"), [this] { later([this] { chooseLocalTrack(true); }); });
            }
            if (count(false) > 0) {
                buttons.emplace_back(ui::tr("player.subtitles"), [this] { later([this] { chooseLocalTrack(false); }); });
            }
            buttons.emplace_back(ui::tr("player.volume_value", {std::to_string(session().settings().volume)}),
                                 [this] { later([this] { chooseVolume(); }); });
            buttons.emplace_back(ui::tr("player.picture"), [this] { later([this] { choosePicture(); }); });
            buttons.emplace_back(ui::tr("ui.cancel"), std::function<void()>{});
            ui::Application::current()->showModal(ui::makeDialog(barTitle(), "", std::move(buttons)));
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
             {ui::tr("player.volume_value", {std::to_string(session().settings().volume)}),
              [this] { later([this] { chooseVolume(); }); }},
             {ui::tr("player.picture"), [this] { later([this] { choosePicture(); }); }},
             {ui::tr("ui.cancel"), {}}}));
    }

    // a file's own audio or subtitle tracks (mpv's ids), subtitles with "Off" first
    void chooseLocalTrack(bool audio) {
        if (!engine_) {
            return;
        }
        std::vector<std::string> names;
        std::vector<int> ids;
        int selected = 0;
        if (!audio) {
            names.push_back(ui::tr("player.subtitles_off"));
            ids.push_back(0);
        }
        for (const auto& track : engine_->tracks()) {
            if (track.audio != audio) {
                continue;
            }
            std::string name = track.title;
            if (!track.language.empty()) {
                name = name.empty() ? track.language : name + " (" + track.language + ")";
            }
            if (name.empty()) {
                name = std::to_string(track.id);
            }
            if (track.selected) {
                selected = static_cast<int>(names.size());
            }
            names.push_back(name);
            ids.push_back(track.id);
        }
        ui::Application::current()->showModal(ui::makeChoiceDialog(
            ui::tr(audio ? "player.audio" : "player.subtitles"), names, selected, [this, audio, ids](int index) {
                if (!engine_ || index < 0 || static_cast<std::size_t>(index) >= ids.size()) {
                    return;
                }
                if (audio) {
                    engine_->setAudioTrack(ids[static_cast<std::size_t>(index)]);
                } else {
                    engine_->setSubtitleTrack(ids[static_cast<std::size_t>(index)]);
                }
            }));
    }

    // ---- volume and picture (kept in the settings for the next videos)

    // `save` false: kept when the player closes (the live channels' ↑/↓ step it often)
    void setVolume(int percent, bool save = true) {
        percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
        session().settings().volume = percent;
        volumeUnsaved_ = !save;
        if (save) {
            session().saveSettings();
        }
        if (engine_) {
            engine_->setVolume(percent);
        }
    }

    void stepVolume(int delta) {
        setVolume(session().settings().volume + delta, false);
        if (ui::Application* app = ui::Application::current()) {
            app->toast(ui::tr("player.volume_value", {std::to_string(session().settings().volume)}), 1500);
        }
    }

    void chooseVolume() {
        std::vector<std::string> names;
        int selected = 0;
        for (int i = 0; i <= 10; ++i) {
            const int percent = 100 - i * 10;
            names.push_back(std::to_string(percent) + " %");
            selected = percent == session().settings().volume ? i : selected;
        }
        ui::Application::current()->showModal(ui::makeChoiceDialog(
            ui::tr("player.volume"), names, selected, [this](int index) { setVolume(100 - index * 10); }));
    }

    void choosePicture() {
        static const char* const kFits[] = {"fit", "fill", "stretch"};
        static const int kSizes[] = {100, 95, 90, 85, 80};
        std::vector<std::string> names;
        int selected = 0;
        for (int f = 0; f < 3; ++f) {
            for (int z = 0; z < (f == 0 ? 5 : 1); ++z) {  // a smaller picture only makes sense whole
                std::string name = ui::tr(std::string("player.picture.") + kFits[f]);
                if (f == 0 && z > 0) {
                    name += " \xC2\xB7 " + std::to_string(kSizes[z]) + " %";
                }
                if (session().settings().pictureFit == kFits[f] &&
                    (f != 0 || session().settings().pictureSize == kSizes[z])) {
                    selected = static_cast<int>(names.size());
                }
                names.push_back(name);
            }
        }
        ui::Application::current()->showModal(
            ui::makeChoiceDialog(ui::tr("player.picture"), names, selected, [this](int index) {
                Settings& settings = session().settings();
                settings.pictureFit = index < 5 ? "fit" : index == 5 ? "fill" : "stretch";
                settings.pictureSize = index < 5 ? kSizes[index] : 100;
                session().saveSettings();
                markDirty();
            }));
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
    std::string mediaSourceId_;
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
    std::optional<jellyfin::Item> previous_;
    std::optional<jellyfin::Item> next_;
    player::SeekAccumulator seek_;
    player::ButtonTimer buttonTimer_;
    player::SegmentButton shownButton_;

    ui::Spinner* spinner_ = nullptr;
    player::NextCard nextCardState_;
    ui::Box* nextCard_ = nullptr;
    ui::ImageView* nextImage_ = nullptr;
    ui::Label* nextTitle_ = nullptr;
    ui::Label* nextCountdown_ = nullptr;
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
    bool leaving_ = false;  // the next episode takes this screen's place
    bool local_ = false;    // a download, played from the console
    bool fileLoaded_ = false;                // a local file is loaded (subtitle files can be added)
    std::vector<std::string> usbSubtitles_;  // found beside a USB file, added once it is loaded
    bool volumeUnsaved_ = false;
    bool direct_ = false;               // playing a channel's own stream, not the server's relay
    std::int64_t directSinceMs_ = 0;
    std::int64_t lastPictureMs_ = 0;
    player::ButtonRepeat seekRepeat_;  // ← / → or L1 / R1 held
    platform::Action seekButton_ = platform::Action::Right;
    player::HoldToSkip hold_;              // L2 / R2 held
    ui::Box* holdPanel_ = nullptr;
    ui::Label* holdLabel_ = nullptr;
    ui::ProgressBar* holdProgress_ = nullptr;
    double lastPosition_ = 0;  // the last known position and length, for the end-of-file log line
    double lastDuration_ = 0;  // stepped by ↑/↓: saved when the player closes
    std::int64_t lastSeekPressMs_ = 0;
    std::int64_t holdStartMs_ = 0;
    int lastDirection_ = 0;
    std::int64_t statsStart_ = 0;
    int pictures_ = 0;
};

}  // namespace

std::unique_ptr<ui::Screen> makePlayer(AppSession& session, jellyfin::Item item, std::int64_t startTicks,
                                       std::string mediaSourceId) {
    return std::make_unique<PlayerScreen>(session, std::move(item), startTicks, std::move(mediaSourceId));
}

}  // namespace playfin::app
