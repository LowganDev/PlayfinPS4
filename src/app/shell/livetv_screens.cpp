#include <nanovg.h>

#include <map>
#include <string>
#include <vector>

#include "app/shell/cards.hpp"
#include "app/shell/chrome.hpp"
#include "app/shell/format.hpp"
#include "app/shell/guide.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "ui/draw.hpp"
#include "ui/i18n.hpp"
#include "ui/image_view.hpp"
#include "ui/reveal.hpp"
#include "ui/scroll.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

constexpr std::int64_t kGuideSeconds = 3 * 86400;  // what the guide asks for: 3 days from now
constexpr float kLogo = 112;

// A focusable row with the accent fill when focused (channels, programmes).
class Row : public ui::Box {
public:
    explicit Row(std::function<void()> onPress) : ui::Box(ui::Axis::Row) {
        focusable = true;
        drawsOwnFocus = true;
        width = ui::SizeSpec::fill();
        cornerRadius = ui::theme().radius;
        crossAlign = ui::CrossAlign::Center;
        gap = ui::theme().spacingL;
        bind(platform::Action::Confirm, ui::tr("ui.select"), [onPress = std::move(onPress)] {
            onPress();
            return true;
        });
    }
    void onFocusChanged(bool focused) override {
        focused_ = focused;
        refresh();
        if (focused && onFocused) {
            onFocused();
        }
    }
    void onThemeChanged() override {
        refresh();
        ui::Box::onThemeChanged();
    }
    // labels that turn onAccent with the fill; secondary ones keep a lighter colour otherwise
    void addText(ui::Label* label, bool secondary) {
        texts_.push_back({label, secondary});
        refresh();
    }
    std::function<void()> onFocused;

protected:
    void refresh() {
        background = focused_ ? ui::theme().controlFocused : resting;
        for (const auto& text : texts_) {
            text.first->color = focused_      ? ui::theme().onAccent
                                : text.second ? ui::theme().textSecondary
                                              : ui::theme().text;
        }
        markDirty();
    }

public:
    ui::Color resting{0, 0, 0, 0};

private:
    bool focused_ = false;
    std::vector<std::pair<ui::Label*, bool>> texts_;
};

// a small pill ("En direct") in the accent colour
ui::View* addBadge(ui::Box& row, const std::string& text) {
    auto* pill = row.add<ui::Box>(ui::Axis::Row);
    pill->padding = {6, 12, 4, 12};
    pill->cornerRadius = 8;
    pill->background = ui::theme().accent;
    auto* label = pill->add<ui::Label>(text, ui::FontStyle{ui::theme().fontSmall, ui::FontWeight::SemiBold});
    label->color = ui::theme().onAccent;
    return pill;
}

// the dot of a programme scheduled to be recorded
class RecordDot : public ui::View {
public:
    RecordDot() {
        width = ui::SizeSpec::fixed(18);
        height = ui::SizeSpec::fixed(18);
    }
    void draw(NVGcontext* vg) const override {
        nvgBeginPath(vg);
        nvgCircle(vg, frame.width / 2, frame.height / 2, frame.width / 2);
        nvgFillColor(vg, ui::nvgColor(ui::theme().error));
        nvgFill(vg);
    }
};

jellyfin::Item channelOf(const jellyfin::Item& program, const std::vector<jellyfin::Item>& channels) {
    for (const auto& channel : channels) {
        if (channel.id == program.channelId) {
            return channel;
        }
    }
    jellyfin::Item channel;
    channel.id = program.channelId;
    channel.type = jellyfin::ItemType::TvChannel;
    channel.name = program.channelName;
    return channel;
}

class LiveTvScreen : public ShellScreen {
public:
    enum class Mode { Now, Guide, Recordings };

    LiveTvScreen(AppSession& session, jellyfin::Item view) : ShellScreen(session), view_(std::move(view)) {
        auto* page = addPage(*this, view_.name);
        auto* bar = page->add<ui::Box>(ui::Axis::Row);
        bar->gap = ui::theme().spacingM;
        nowButton_ = bar->add<ui::Button>(ui::tr("livetv.now"), [this] { show(Mode::Now); });
        guideButton_ = bar->add<ui::Button>(ui::tr("livetv.guide"), [this] { show(Mode::Guide); });
        recordingsButton_ = bar->add<ui::Button>(ui::tr("livetv.recordings"), [this] { show(Mode::Recordings); });
        status_ = page->add<ui::Label>(ui::tr("shell.loading"), bodyText());
        status_->color = ui::theme().textSecondary;
        area_ = page->add<ui::Box>(ui::Axis::Column);
        area_->width = ui::SizeSpec::fill();
        area_->height = ui::SizeSpec::fill();
        refreshBar();
    }
    ~LiveTvScreen() override { selection_.cancel(); }
    void onShow() override {
        if (!loaded_) {
            loaded_ = true;
            later([this] { loadChannels(); });
        }
    }

private:
    void refreshBar() {
        nowButton_->setPrimary(mode_ == Mode::Now);
        guideButton_->setPrimary(mode_ == Mode::Guide);
        recordingsButton_->setPrimary(mode_ == Mode::Recordings);
    }

    void loadChannels() {
        const auto client = session_.client();
        const core::CancelToken token = token_;
        run<core::Result<jellyfin::ItemPage>>([client, token] { return client->liveTvChannels(token); },
                                              [this](core::Result<jellyfin::ItemPage> page) {
                                                  if (!page.ok()) {
                                                      status_->setText(errorText(page.error()));
                                                      status_->color = ui::theme().error;
                                                      return;
                                                  }
                                                  channels_ = page.value().items;
                                                  show(mode_);
                                              });
    }

    // the area below the bar gets the mode's content (the focus goes to the bar's button meanwhile)
    void show(Mode mode) {
        mode_ = mode;
        refreshBar();
        ++generation_;
        ui::Application* app = ui::Application::current();
        if (app != nullptr && area_->isAncestorOf(app->focus().focused())) {
            app->focus().focus(mode == Mode::Now ? nowButton_ : mode == Mode::Guide ? guideButton_ : recordingsButton_);
        }
        area_->clearChildren();
        channelRows_.clear();
        // ↑ out of the content goes back to the tab shown
        area_->setNextFocus(ui::Direction::Up,
                            mode == Mode::Now ? nowButton_ : mode == Mode::Guide ? guideButton_ : recordingsButton_);
        programs_ = nullptr;
        status_->visible = false;
        switch (mode) {
        case Mode::Now:
            showNow();
            break;
        case Mode::Guide:
            showGuide();
            break;
        case Mode::Recordings:
            loadRecordings();
            break;
        }
        ui::revealChildren(*area_, 0);
    }

    void watch(const jellyfin::Item& channel) {
        ui::Application::current()->push(makePlayer(session_, channel, 0));
    }

    // ---- on now: one row per channel with its programme and progress

    void showNow() {
        auto* scroll = area_->add<ui::VScroll>();
        scroll->policy = ui::VScroll::Policy::Nearest;
        auto list = std::make_unique<ui::Box>(ui::Axis::Column);
        list->width = ui::SizeSpec::fill();
        list->gap = ui::theme().spacingS;
        list->padding = {ui::theme().spacingS, 0, 120, 0};
        const std::int64_t now = session_.now();
        ui::View* first = nullptr;
        for (const auto& channel : channels_) {
            auto* row = list->add<Row>([this, channel] { watch(channel); });
            row->name = channel.name;
            row->resting = ui::theme().control;
            row->padding = ui::Insets::all(ui::theme().spacingM);
            auto* logo = row->add<ui::ImageView>();
            logo->width = ui::SizeSpec::fixed(kLogo);
            logo->height = ui::SizeSpec::fixed(kLogo);
            if (const auto client = session_.client(); client && !channel.images.primary.empty()) {
                logo->setUrl(client->imageUrl(channel.id, jellyfin::ImageType::Primary, channel.images.primary,
                                              static_cast<int>(kLogo), static_cast<int>(kLogo)));
            }
            auto* text = row->add<ui::Box>(ui::Axis::Column);
            text->width = ui::SizeSpec::fill();
            text->gap = ui::theme().spacingS;
            row->addText(text->add<ui::Label>(channel.name, titleText(ui::FontWeight::SemiBold)),
                         false);
            if (const jellyfin::Item* program = channel.currentProgram.get()) {
                const int offset = session_.utcOffsetMinutes(program->startUtc);
                row->addText(text->add<ui::Label>(timeRange(*program, offset) + " \xC2\xB7 " + program->name,
                                                  bodyText()),
                             true);
                auto* bar = text->add<ui::ProgressBar>();
                bar->width = ui::SizeSpec::fixed(640);
                bar->height = ui::SizeSpec::fixed(6);
                bar->value = static_cast<float>(progress(*program, now));
            }
            if (first == nullptr) {
                first = row;
            }
        }
        scroll->setContent(std::move(list));
        if (first != nullptr) {
            focusContent(first, nowButton_);
        }
    }

    // ---- the guide: the channels on the left, the focused channel's programmes on the right

    void showGuide() {
        auto* columns = area_->add<ui::Box>(ui::Axis::Row);
        columns->width = ui::SizeSpec::fill();
        columns->height = ui::SizeSpec::fill();
        columns->gap = ui::theme().spacingXl;
        auto* channelScroll = columns->add<ui::VScroll>();
        channelScroll->width = ui::SizeSpec::fixed(400);
        channelScroll->policy = ui::VScroll::Policy::Nearest;
        auto channelList = std::make_unique<ui::Box>(ui::Axis::Column);
        channelList->width = ui::SizeSpec::fill();
        channelList->gap = ui::theme().spacingS;
        channelList->padding = {ui::theme().spacingS, 0, 120, 0};
        ui::Box* left = channelList.get();
        channelScroll->setContent(std::move(channelList));
        for (const auto& channel : channels_) {
            auto* row = left->add<Row>([this, channel] { watch(channel); });
            row->name = "channel " + channel.name;
            row->resting = ui::theme().control;
            row->padding = rowPadding();
            row->addText(row->add<ui::Label>(channel.name, bodyStrong()),
                         false);
            const std::string id = channel.id;
            row->onFocused = [this, id] { selectChannel(id); };
            channelRows_[channel.id] = row;
        }
        auto* scroll = columns->add<ui::VScroll>();
        scroll->policy = ui::VScroll::Policy::Nearest;
        auto list = std::make_unique<ui::Box>(ui::Axis::Column);
        list->width = ui::SizeSpec::fill();
        list->gap = 4;
        list->padding = {ui::theme().spacingS, 0, 120, 0};
        programs_ = list.get();
        scroll->setContent(std::move(list));
        // the two columns' rows do not line up: → and ← go across explicitly
        left->setNextFocus(ui::Direction::Right, programs_);
        if (!channels_.empty()) {
            selectChannel(channels_.front().id);
        }
    }

    void selectChannel(const std::string& channelId) {
        if (channelId == guideChannel_ && programs_ != nullptr && !programs_->children().empty()) {
            return;
        }
        guideChannel_ = channelId;
        selection_.cancel();  // the channel passed over: its programmes are no longer wanted
        selection_ = core::CancelToken();
        if (programs_ != nullptr && channelRows_.count(channelId) != 0) {
            programs_->setNextFocus(ui::Direction::Left, channelRows_[channelId]);
        }
        const auto cached = guide_.find(channelId);
        if (cached != guide_.end()) {
            showPrograms(cached->second);
            return;
        }
        const auto client = session_.client();
        const std::int64_t now = session_.now();
        const core::CancelToken token = selection_;
        const int generation = generation_;
        run<core::Result<jellyfin::ItemPage>>(
            [client, channelId, now, token] { return client->programs(channelId, now, now + kGuideSeconds, token); },
            [this, channelId, generation](core::Result<jellyfin::ItemPage> page) {
                if (generation != generation_ || !page.ok()) {
                    return;
                }
                guide_[channelId] = page.value().items;
                if (channelId == guideChannel_) {
                    showPrograms(guide_[channelId]);
                }
            });
    }

    void showPrograms(const std::vector<jellyfin::Item>& programs) {
        if (programs_ == nullptr) {
            return;
        }
        ui::Application* app = ui::Application::current();
        if (app != nullptr && programs_->isAncestorOf(app->focus().focused())) {
            return;  // the user is in the list: it stays as it is
        }
        programs_->clearChildren();
        for (const auto& channel : channels_) {
            if (channel.id == guideChannel_) {  // whose programmes these are
                auto* name = programs_->add<ui::Label>(channel.name, titleText(ui::FontWeight::Bold));
                name->color = ui::theme().textSecondary;
            }
        }
        const std::int64_t now = session_.now();
        std::int64_t day = -1;
        for (const auto& program : programs) {
            if (program.endUtc <= now) {
                continue;
            }
            const int offset = session_.utcOffsetMinutes(program.startUtc);
            if (localDay(program.startUtc, offset) != day) {
                day = localDay(program.startUtc, offset);
                auto* heading = programs_->add<ui::Label>(dayHeading(program.startUtc, now, offset),
                                                          headingText());
                heading->margin = {ui::theme().spacingM, 0, ui::theme().spacingS, 0};
            }
            auto* row = programs_->add<Row>([this, program] { openProgram(program); });
            row->name = program.name;
            row->padding = rowPadding();
            auto* time = row->add<ui::Label>(clock24(program.startUtc, offset), bodyText());
            time->width = ui::SizeSpec::fixed(90);
            row->addText(time, true);
            auto* title = row->add<ui::Label>(program.name, bodyText());
            title->width = ui::SizeSpec::fill();
            row->addText(title, false);
            if (airing(program, now)) {
                addBadge(*row, ui::tr("livetv.live"));
            }
            if (!program.timerId.empty()) {
                row->add<RecordDot>();
            }
        }
    }

    void openProgram(const jellyfin::Item& program) {
        const jellyfin::Item channel = channelOf(program, channels_);
        const std::int64_t now = session_.now();
        if (airing(program, now)) {
            watch(channel);
            return;
        }
        const int offset = session_.utcOffsetMinutes(program.startUtc);
        std::string message = dayHeading(program.startUtc, now, offset) + " \xC2\xB7 " + timeRange(program, offset) +
                              " \xC2\xB7 " + channel.name;
        if (!program.overview.empty()) {
            message += "\n" + program.overview;
        }
        std::vector<std::pair<std::string, std::function<void()>>> buttons;
        if (program.timerId.empty()) {
            const std::string programId = program.id;
            buttons.emplace_back(ui::tr("livetv.record"), [this, programId] { schedule(programId); });
        } else {
            const std::string timerId = program.timerId;
            buttons.emplace_back(ui::tr("livetv.cancel_recording"), [this, timerId] { cancel(timerId); });
        }
        buttons.emplace_back(ui::tr("livetv.close"), std::function<void()>{});
        ui::Application::current()->showModal(ui::makeDialog(program.name, message, std::move(buttons)));
    }

    void schedule(const std::string& programId) {
        const auto client = session_.client();
        const core::CancelToken token = token_;
        run<core::Result<core::Unit>>([client, programId, token] { return client->createTimer(programId, token); },
                                      [this](core::Result<core::Unit> result) {
                                          ui::Application::current()->toast(
                                              result.ok() ? ui::tr("livetv.scheduled") : errorText(result.error()));
                                          refreshGuide();
                                      });
    }

    void cancel(const std::string& timerId) {
        const auto client = session_.client();
        const core::CancelToken token = token_;
        run<core::Result<core::Unit>>([client, timerId, token] { return client->cancelTimer(timerId, token); },
                                      [this](core::Result<core::Unit> result) {
                                          ui::Application::current()->toast(
                                              result.ok() ? ui::tr("livetv.cancelled") : errorText(result.error()));
                                          if (mode_ == Mode::Recordings) {
                                              show(Mode::Recordings);
                                          } else {
                                              refreshGuide();
                                          }
                                      });
    }

    // the scheduled dots: the programmes of the guide's channel again
    void refreshGuide() {
        guide_.erase(guideChannel_);
        if (mode_ == Mode::Guide && programs_ != nullptr) {
            const std::string channel = guideChannel_;
            guideChannel_.clear();
            ui::Application* app = ui::Application::current();
            if (app != nullptr && programs_->isAncestorOf(app->focus().focused())) {
                app->focus().focus(guideButton_);
            }
            selectChannel(channel);
        }
    }

    // ---- recordings: the recorded programmes and the scheduled ones

    struct RecordingsData {
        core::Result<jellyfin::ItemPage> recordings = jellyfin::ItemPage{};
        core::Result<std::vector<jellyfin::Timer>> timers = std::vector<jellyfin::Timer>{};
    };

    void loadRecordings() {
        const auto client = session_.client();
        const core::CancelToken token = token_;
        const int generation = generation_;
        run<RecordingsData>(
            [client, token] {
                RecordingsData data;
                data.recordings = client->recordings(token);
                data.timers = client->timers(token);
                return data;
            },
            [this, generation](RecordingsData data) {
                if (generation == generation_) {
                    showRecordings(std::move(data));
                }
            });
    }

    void showRecordings(RecordingsData data) {
        auto* scroll = area_->add<ui::VScroll>();
        scroll->policy = ui::VScroll::Policy::Nearest;
        auto list = std::make_unique<ui::Box>(ui::Axis::Column);
        list->width = ui::SizeSpec::fill();
        list->gap = ui::theme().spacingM;
        list->padding = {ui::theme().spacingS, 0, 120, 0};
        list->add<ui::Label>(ui::tr("livetv.recorded_title"), headingText());
        if (data.recordings.ok() && !data.recordings.value().items.empty()) {
            addItemRow(*list, "", session_, data.recordings.value().items, CardShape::Wide,
                       [this](const jellyfin::Item& item) { openItem(session_, item); });
        } else {
            auto* none = list->add<ui::Label>(ui::tr("livetv.nothing"), bodyText());
            none->color = ui::theme().textSecondary;
        }
        auto* heading = list->add<ui::Label>(ui::tr("livetv.scheduled_title"),
                                             headingText());
        heading->margin = {ui::theme().spacingL, 0, 0, 0};
        const std::int64_t now = session_.now();
        if (data.timers.ok()) {
            for (const auto& timer : data.timers.value()) {
                jellyfin::Item program;
                program.startUtc = timer.startUtc;
                program.endUtc = timer.endUtc;
                program.channelId = timer.channelId;
                const int offset = session_.utcOffsetMinutes(timer.startUtc);
                const std::string when = dayHeading(timer.startUtc, now, offset) + " \xC2\xB7 " +
                                         timeRange(program, offset) + " \xC2\xB7 " + channelOf(program, channels_).name;
                const std::string timerId = timer.id;
                const std::string name = timer.name;
                auto* row = list->add<ui::ValueRow>(timer.name, when, [this, timerId, name, when] {
                    ui::Application::current()->showModal(ui::makeDialog(
                        name, when,
                        {{ui::tr("livetv.cancel_recording"), [this, timerId] { cancel(timerId); }},
                         {ui::tr("livetv.close"), {}}}));
                });
                row->name = timer.name;
                row->width = ui::SizeSpec::fill();
            }
        }
        scroll->setContent(std::move(list));
    }

    jellyfin::Item view_;
    std::vector<jellyfin::Item> channels_;
    std::map<std::string, std::vector<jellyfin::Item>> guide_;  // programmes by channel
    std::string guideChannel_;
    std::map<std::string, ui::View*> channelRows_;  // the guide's channel rows by id
    Mode mode_ = Mode::Now;
    ui::Button* nowButton_;
    ui::Button* guideButton_;
    ui::Button* recordingsButton_;
    ui::Label* status_;
    ui::Box* area_;
    ui::Box* programs_ = nullptr;
    int generation_ = 0;
    core::CancelToken selection_;  // the programmes request of the channel in focus
    bool loaded_ = false;
};

}  // namespace

std::unique_ptr<ui::Screen> makeLiveTv(AppSession& session, jellyfin::Item view) {
    return std::make_unique<LiveTvScreen>(session, std::move(view));
}

}  // namespace playfin::app
