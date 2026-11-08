#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/shell/chrome.hpp"
#include "app/shell/format.hpp"
#include "app/shell/library_query.hpp"
#include "app/shell/cards.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "ui/i18n.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

// "Ordre des médiathèques": ✕ takes a library, ↑ / ↓ move it, ✕ puts it down (saved at once). Home lists "Mes
// médias" and the latest rows in this order.
class LibraryOrderScreen : public ShellScreen {
public:
    explicit LibraryOrderScreen(AppSession& session) : ShellScreen(session) {
        column_ = addPage(*this, ui::tr("settings.library_order"));
        hint_ = column_->add<ui::Label>(ui::tr("settings.library_order.hint"),
                                        ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        hint_->color = ui::theme().textSecondary;
        rows_ = column_->add<ui::Box>(ui::Axis::Column);
        rows_->gap = ui::theme().spacingM;
        bind(platform::Action::Up, ui::tr("settings.library_order.move"), [this] { return move(-1); },
             [this] { return moving_ >= 0; });
        bind(platform::Action::Down, ui::tr("settings.library_order.move"), [this] { return move(1); },
             [this] { return moving_ >= 0; });
    }

    void onShow() override {
        if (!loaded_) {
            loaded_ = true;
            later([this] { load(); });
        }
    }

private:
    void load() {
        const auto client = session_.client();
        if (!client) {
            return;
        }
        const core::CancelToken token = token_;
        run<core::Result<jellyfin::ItemPage>>([client, token] { return client->libraries(token); },
                                              [this](core::Result<jellyfin::ItemPage> page) {
                                                  if (!page.ok()) {
                                                      hint_->setText(errorText(page.error()));
                                                      return;
                                                  }
                                                  libraries_ = orderLibraries(page.value().items,
                                                                              session_.settings().libraryOrder);
                                                  rebuild(0);
                                              });
    }

    void rebuild(int focus) {
        rows_->clearChildren();
        buttons_.clear();
        for (std::size_t i = 0; i < libraries_.size(); ++i) {
            const int index = static_cast<int>(i);
            std::string text = libraries_[i].name;
            if (index == moving_) {
                text = "\xE2\x86\x95 " + text;  // ↕
            }
            auto* button = rows_->add<ui::Button>(text, [this, index] { pick(index); });
            button->width = ui::SizeSpec::fixed(900);
            button->setPrimary(index == moving_);
            buttons_.push_back(button);
        }
        if (focus >= 0 && focus < static_cast<int>(buttons_.size())) {
            ui::Application::current()->focus().focus(buttons_[static_cast<std::size_t>(focus)]);
        }
        markDirty();
    }

    void pick(int index) {
        moving_ = moving_ == index ? -1 : index;
        if (moving_ < 0) {
            save();
        }
        later([this, index] { rebuild(index); });
    }

    bool move(int step) {
        if (moving_ < 0) {
            return false;  // plain navigation
        }
        const int target = moving_ + step;
        if (target < 0 || target >= static_cast<int>(libraries_.size())) {
            return true;
        }
        std::swap(libraries_[static_cast<std::size_t>(moving_)], libraries_[static_cast<std::size_t>(target)]);
        moving_ = target;
        save();
        later([this, target] { rebuild(target); });
        return true;
    }

    void save() {
        std::vector<std::string> order;
        for (const jellyfin::Item& library : libraries_) {
            order.push_back(library.id);
        }
        session_.settings().libraryOrder = order;
        session_.saveSettings();
    }

    ui::Box* column_;
    ui::Label* hint_;
    ui::Box* rows_;
    std::vector<ui::Button*> buttons_;
    std::vector<jellyfin::Item> libraries_;
    int moving_ = -1;
    bool loaded_ = false;
};

}  // namespace

std::unique_ptr<ui::Screen> makeLibraryOrder(AppSession& session) {
    return std::make_unique<LibraryOrderScreen>(session);
}

}  // namespace playfin::app
