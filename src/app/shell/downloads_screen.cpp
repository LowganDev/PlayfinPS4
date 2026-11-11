#include <algorithm>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/shell/cards.hpp"
#include "app/shell/chrome.hpp"
#include "app/shell/downloads.hpp"
#include "app/shell/format.hpp"
#include "app/shell/nav_bar.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "player/session.hpp"
#include "ui/application.hpp"
#include "ui/i18n.hpp"
#include "ui/scroll.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

std::string stateText(const Downloads::Entry& entry) {
    switch (entry.state) {
    case Downloads::State::Done:
        return entry.drive.empty() ? ui::tr("downloads.done", {sizeText(entry.size)})
                                   : ui::tr("downloads.done", {sizeText(entry.size)}) + " \xC2\xB7 " +
                                         ui::tr("downloads.target.usb");
    case Downloads::State::Running:
        return Downloads::runningText(entry, true);
    case Downloads::State::Queued:
        return entry.received > 0 ? Downloads::pausedText(entry) + " \xC2\xB7 " + ui::tr("downloads.queued")
                                  : ui::tr("downloads.queued");
    case Downloads::State::Paused:
        return Downloads::pausedText(entry);
    case Downloads::State::Failed:
        return ui::tr("downloads.failed", {entry.error});
    }
    return {};
}

// The "Téléchargements" tab: what is on its way (waiting, downloading, failed), then what is downloaded, each film
// or episode with its state; ✕ plays one (or tries a failed one again), OPTIONS removes it.
class DownloadsScreen : public ShellScreen {
public:
    explicit DownloadsScreen(AppSession& session) : ShellScreen(session), downloads_(session.downloads()) {
        auto* scroll = add<ui::VScroll>();
        scroll->policy = ui::VScroll::Policy::Nearest;
        auto content = std::make_unique<ui::Box>(ui::Axis::Column);
        content->padding = pagePadding();
        content->gap = ui::theme().spacingM;
        column_ = content.get();
        navBar_ = column_->add<NavBar>(session_, Tab::Downloads);
        if (session_.usbFtp) {  // where the next downloads go: the console or a USB drive
            target_ = column_->add<ui::ValueRow>(ui::tr("downloads.target"), targetName(), [this] { chooseTarget(); });
            target_->margin = {ui::theme().spacingL, 0, 0, 0};
        }
        space_ = column_->add<ui::Label>("", bodyText());
        space_->color = ui::theme().textSecondary;
        space_->margin = {ui::theme().spacingL, 0, 0, 0};
        runningHeading_ = addSection(ui::tr("downloads.section_running"));
        running_ = column_->add<ui::Box>(ui::Axis::Column);
        running_->width = ui::SizeSpec::fill();
        running_->gap = ui::theme().spacingS;
        doneHeading_ = addSection(ui::tr("downloads.section_done"));
        done_ = column_->add<ui::Box>(ui::Axis::Column);
        done_->width = ui::SizeSpec::fill();
        done_->gap = ui::theme().spacingS;
        empty_ = column_->add<ui::Label>(ui::tr("downloads.empty"), bodyText());
        empty_->color = ui::theme().textSecondary;
        empty_->margin = {ui::theme().spacingL, 0, 0, 0};
        auto* bottom = column_->add<ui::View>();  // room for the hints bar under the last row
        bottom->height = ui::SizeSpec::fixed(120);
        scroll->setContent(std::move(content));
        downloads_.listen(this, [this] { refresh(); });
        rebuild();
    }
    ~DownloadsScreen() override { downloads_.unlisten(this); }

    void onShow() override {
        ShellScreen::onShow();
        refreshSpace();
    }

private:
    std::string targetName() const {
        return ui::tr(session_.settings().downloadTarget == "usb" ? "downloads.target.usb" : "downloads.target.console");
    }

    void chooseTarget() {
        const bool usb = session_.settings().downloadTarget == "usb";
        ui::Application::current()->showModal(ui::makeChoiceDialog(
            ui::tr("downloads.target"), {ui::tr("downloads.target.console"), ui::tr("downloads.target.usb")}, usb ? 1 : 0,
            [this](int index) {
                session_.settings().downloadTarget = index == 1 ? "usb" : "console";
                session_.saveSettings();
                target_->setValue(targetName());
                refreshSpace();
            }));
    }

    ui::Label* addSection(const std::string& text) {
        auto* heading =
            column_->add<ui::Label>(text, ui::FontStyle{ui::theme().fontHeading, ui::FontWeight::SemiBold});
        heading->margin = {ui::theme().spacingL, 0, 0, 0};
        return heading;
    }

    // rows rebuilt when the list or a state changes; only their texts when a download moves on
    void refresh() {
        if (signature() != signature_) {
            rebuild();
            return;
        }
        for (const auto& entry : downloads_.entries()) {
            for (const auto& row : rows_) {
                if (row.first == entry.item.id) {
                    row.second->setValue(stateText(entry));
                }
            }
        }
        refreshSpace();
    }

    std::string signature() const {
        std::string text;
        for (const auto& entry : downloads_.entries()) {
            text += entry.item.id + (entry.state == Downloads::State::Done ? "+" : "-") + ",";  // order and sections
        }
        return text;
    }

    // the console's free space; none shown for a USB drive (GoldHEN's FTP server cannot tell a drive's)
    void refreshSpace() {
        const std::int64_t free = session_.settings().downloadTarget == "usb" ? 0 : Downloads::freeBytes();
        space_->setText(free > 0 ? ui::tr("downloads.free", {sizeText(free)}) : std::string());
        space_->visible = free > 0;
        markDirty();
    }

    void rebuild() {
        ui::Application* app = ui::Application::current();
        std::string focusedId;
        for (const auto& row : rows_) {
            if (app != nullptr && app->focus().focused() == row.second) {
                focusedId = row.first;
            }
        }
        const bool hadFocus = app != nullptr && app->focus().focused() != nullptr &&
                              (running_->isAncestorOf(app->focus().focused()) ||
                               done_->isAncestorOf(app->focus().focused()));
        if (hadFocus) {
            app->focus().focus(this);
        }
        running_->clearChildren();
        done_->clearChildren();
        rows_.clear();
        signature_ = signature();
        ui::ValueRow* first = nullptr;
        ui::ValueRow* again = nullptr;
        for (const auto& entry : downloads_.entries()) {
            const std::string id = entry.item.id;
            const std::string title = Downloads::title(entry.item);
            ui::Box* list = entry.state == Downloads::State::Done ? done_ : running_;
            auto* row = list->add<ui::ValueRow>(title, stateText(entry), std::function<void()>{});
            row->width = ui::SizeSpec::fill();
            // ✕ does what the row's state calls for, and its hint says so (the state changes without a rebuild)
            const auto in = [this, id](std::initializer_list<Downloads::State> states) {
                return [this, id, states = std::vector<Downloads::State>(states)] {
                    const Downloads::Entry* now = downloads_.find(id);
                    return now != nullptr && std::find(states.begin(), states.end(), now->state) != states.end();
                };
            };
            const std::pair<const char*, std::function<bool()>> pressLabels[] = {
                {"downloads.pause", in({Downloads::State::Running, Downloads::State::Queued})},
                {"downloads.resume", in({Downloads::State::Paused})},
                {"ui.retry", in({Downloads::State::Failed})},
                {"shell.details.play", in({Downloads::State::Done})}};
            for (const auto& [key, shown] : pressLabels) {
                row->bind(
                    platform::Action::Confirm, ui::tr(key),
                    [this, id] {
                        press(id);
                        return true;
                    },
                    shown);
            }
            row->bind(platform::Action::Options, ui::tr("downloads.remove"), [this, id] {
                later([this, id] { confirmRemove(id); });
                return true;
            });
            if (entry.state != Downloads::State::Done) {  // the order of what is on its way (not the running one)
                const auto movable = in({Downloads::State::Queued, Downloads::State::Paused});
                row->bind(
                    platform::Action::L1, ui::tr("downloads.move_up"),
                    [this, id] {
                        downloads_.move(id, -1);
                        return true;
                    },
                    movable);
                row->bind(
                    platform::Action::R1, ui::tr("downloads.move_down"),
                    [this, id] {
                        downloads_.move(id, 1);
                        return true;
                    },
                    movable);
            }
            rows_.emplace_back(id, row);
            if (id == focusedId) {
                again = row;
            }
        }
        // the first row in the order shown: what is on its way, then what is downloaded
        first = !running_->children().empty()  ? static_cast<ui::ValueRow*>(running_->children().front().get())
                : !done_->children().empty() ? static_cast<ui::ValueRow*>(done_->children().front().get())
                                             : nullptr;
        runningHeading_->visible = !running_->children().empty();
        running_->visible = runningHeading_->visible;
        doneHeading_->visible = !done_->children().empty();
        done_->visible = doneHeading_->visible;
        empty_->visible = rows_.empty();
        refreshSpace();
        remembered = first != nullptr ? static_cast<ui::View*>(first) : target_;
        if (app != nullptr && app->top() == this && !app->hasModal()) {
            if (hadFocus && (again != nullptr || first != nullptr)) {
                app->focus().focus(again != nullptr ? static_cast<ui::View*>(again) : first);
            } else if (first != nullptr || target_ != nullptr) {
                focusContent(first != nullptr ? static_cast<ui::View*>(first) : target_, navBar_->currentTab());
            }
        }
        markDirty();
    }

    void press(const std::string& id) {
        const Downloads::Entry* entry = downloads_.find(id);
        if (entry == nullptr) {
            return;
        }
        if (entry->state == Downloads::State::Failed) {
            downloads_.retry(id);
        } else if (entry->state == Downloads::State::Running || entry->state == Downloads::State::Queued) {
            downloads_.pause(id);
        } else if (entry->state == Downloads::State::Paused) {
            downloads_.resume(id);
        } else if (entry->state == Downloads::State::Done) {
            ui::Application::current()->push(makePlayer(session_, entry->item, entry->positionTicks));
        }
    }

    void confirmRemove(const std::string& id) {
        const Downloads::Entry* entry = downloads_.find(id);
        if (entry == nullptr) {
            return;
        }
        Downloads* downloads = &downloads_;
        ui::Application::current()->showModal(ui::makeDialog(
            cardTitle(entry->item), ui::tr("downloads.remove_question"),
            {{ui::tr("downloads.remove"), [downloads, id] { downloads->remove(id); }}, {ui::tr("ui.cancel"), {}}}));
    }

    Downloads& downloads_;
    ui::Box* column_;
    NavBar* navBar_;
    ui::ValueRow* target_ = nullptr;
    ui::Label* space_;
    ui::Label* runningHeading_;
    ui::Box* running_;
    ui::Label* doneHeading_;
    ui::Box* done_;
    ui::Label* empty_;
    std::vector<std::pair<std::string, ui::ValueRow*>> rows_;
    std::string signature_;
};

}  // namespace

std::unique_ptr<ui::Screen> makeDownloads(AppSession& session) { return std::make_unique<DownloadsScreen>(session); }

}  // namespace playfin::app
