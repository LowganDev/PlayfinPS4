#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/shell/cards.hpp"
#include "app/shell/chrome.hpp"
#include "app/shell/downloads.hpp"
#include "app/shell/format.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "player/session.hpp"
#include "ui/i18n.hpp"
#include "ui/scroll.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

// "1,2 Go", "340 Mo" (decimal units, as the console shows its storage)
std::string sizeText(std::int64_t bytes) {
    char text[32];
    if (bytes >= 1000LL * 1000 * 1000) {
        std::snprintf(text, sizeof(text), "%.1f", static_cast<double>(bytes) / 1e9);
        std::string number = text;
        const std::string separator = ui::tr("settings.decimal_separator");
        if (const auto dot = number.find('.'); dot != std::string::npos) {
            number.replace(dot, 1, separator);
        }
        return ui::tr("downloads.gb", {number});
    }
    return ui::tr("downloads.mb", {std::to_string(bytes / (1000 * 1000))});
}

std::string stateText(const Downloads::Entry& entry) {
    switch (entry.state) {
    case Downloads::State::Done:
        return ui::tr("downloads.done", {sizeText(entry.size)});
    case Downloads::State::Running:
        return entry.size > 0 ? ui::tr("downloads.progress", {std::to_string(entry.received * 100 / entry.size)})
                              : ui::tr("downloads.progress_bytes", {sizeText(entry.received)});
    case Downloads::State::Queued:
        return ui::tr("downloads.queued");
    case Downloads::State::Failed:
        return ui::tr("downloads.failed", {entry.error});
    }
    return {};
}

// "Téléchargements": each film or episode with its state; ✕ plays one (or tries a failed one again), △ removes it.
class DownloadsScreen : public ShellScreen {
public:
    explicit DownloadsScreen(AppSession& session) : ShellScreen(session), downloads_(session.downloads()) {
        column_ = addPage(*this, ui::tr("downloads.title"));
        space_ = column_->add<ui::Label>("", bodyText());
        space_->color = ui::theme().textSecondary;
        auto* scroll = column_->add<ui::VScroll>();
        scroll->policy = ui::VScroll::Policy::Nearest;
        scroll->width = ui::SizeSpec::fill();
        scroll->height = ui::SizeSpec::fill();
        auto list = std::make_unique<ui::Box>(ui::Axis::Column);
        list->width = ui::SizeSpec::fill();
        list->gap = ui::theme().spacingS;
        list->padding = {ui::theme().spacingS, 0, 120, 0};
        list_ = list.get();
        scroll->setContent(std::move(list));
        downloads_.listen(this, [this] { refresh(); });
        rebuild();
    }
    ~DownloadsScreen() override { downloads_.unlisten(this); }

private:
    // rows rebuilt when the list changes; only their texts when a download moves on
    void refresh() {
        std::string signature;
        for (const auto& entry : downloads_.entries()) {
            signature += entry.item.id + ",";
        }
        if (signature != signature_) {
            rebuild();
            return;
        }
        for (std::size_t i = 0; i < rows_.size() && i < downloads_.entries().size(); ++i) {
            rows_[i]->setValue(stateText(downloads_.entries()[i]));
        }
        refreshSpace();
    }

    void refreshSpace() {
        const std::int64_t free = Downloads::freeBytes();
        space_->setText(free > 0 ? ui::tr("downloads.free", {sizeText(free)}) : std::string());
    }

    void rebuild() {
        signature_.clear();
        ui::Application* app = ui::Application::current();
        int focused = -1;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (app != nullptr && app->focus().focused() == rows_[i]) {
                focused = static_cast<int>(i);
            }
        }
        if (app != nullptr && list_->isAncestorOf(app->focus().focused())) {
            app->focus().focus(this);
        }
        list_->clearChildren();
        rows_.clear();
        for (const auto& entry : downloads_.entries()) {
            signature_ += entry.item.id + ",";
            const std::string id = entry.item.id;
            std::string title = cardTitle(entry.item);
            if (entry.item.type == jellyfin::ItemType::Episode) {
                title += " \xC2\xB7 " + cardSubtitle(entry.item);
            }
            auto* row = list_->add<ui::ValueRow>(title, stateText(entry), [this, id] { press(id); });
            row->width = ui::SizeSpec::fill();
            row->bind(platform::Action::Options, ui::tr("downloads.remove"), [this, id] {
                later([this, id] { confirmRemove(id); });
                return true;
            });
            rows_.push_back(row);
        }
        if (rows_.empty()) {
            list_->add<ui::Label>(ui::tr("downloads.empty"), bodyText())->color = ui::theme().textSecondary;
        }
        refreshSpace();
        if (app != nullptr && !rows_.empty() && app->top() == this && !app->hasModal()) {
            const int index = focused < 0 ? 0 : std::min(focused, static_cast<int>(rows_.size()) - 1);
            app->focus().focus(rows_[static_cast<std::size_t>(index)]);
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
    ui::Label* space_;
    ui::Box* list_;
    std::vector<ui::ValueRow*> rows_;
    std::string signature_;
};

}  // namespace

std::unique_ptr<ui::Screen> makeDownloads(AppSession& session) { return std::make_unique<DownloadsScreen>(session); }

}  // namespace playfin::app
