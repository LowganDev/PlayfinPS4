#include "app/settings.hpp"
#include "app/shell/cards.hpp"
#include "app/shell/chrome.hpp"
#include "app/shell/format.hpp"
#include "app/shell/nav_bar.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "ui/i18n.hpp"
#include "ui/reveal.hpp"
#include "ui/scroll.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

constexpr int kRowItems = 24;

struct SearchData {
    core::Result<jellyfin::ItemPage> movies = jellyfin::ItemPage{};
    core::Result<jellyfin::ItemPage> series = jellyfin::ItemPage{};
    core::Result<jellyfin::ItemPage> episodes = jellyfin::ItemPage{};
};

jellyfin::ItemQuery searchQuery(const std::string& term, const char* type) {
    jellyfin::ItemQuery query;
    query.searchTerm = term;
    query.includeTypes = {type};
    query.limit = kRowItems;
    query.recursive = true;
    return query;
}

class SearchScreen : public ShellScreen {
public:
    explicit SearchScreen(AppSession& session) : ShellScreen(session) {
        scroll_ = add<ui::VScroll>();
        scroll_->leadingInset = ui::theme().spacingXxl;
        auto content = std::make_unique<ui::Box>(ui::Axis::Column);
        content->padding = pagePadding();
        content->gap = ui::theme().spacingL;
        column_ = content.get();
        scroll_->setHeader(column_->add<NavBar>(session_, Tab::Search));
        platform::TextInputRequest request;
        request.title = ui::tr("shell.search.title");
        request.maxLength = 100;
        field_ = column_->add<ui::TextField>(ui::tr("shell.search.field"), request,
                                             [this](const std::string& term) { search(term); });
        field_->width = ui::SizeSpec::fixed(900);
        remembered = field_;  // the field first, not the bar above it
        status_ = column_->add<ui::Label>("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        status_->color = ui::theme().textSecondary;
        status_->visible = false;
        recent_ = column_->add<ui::Box>(ui::Axis::Column);
        recent_->gap = ui::theme().spacingM;
        results_ = column_->add<ui::Box>(ui::Axis::Column);
        results_->width = ui::SizeSpec::fill();
        results_->gap = ui::theme().rowGap;
        scroll_->setContent(std::move(content));
        showRecent();
    }

private:
    // the recent searches, under the field while nothing is searched
    void showRecent() {
        keepFocusOutOf(recent_);
        recent_->clearChildren();
        const auto& terms = session_.settings().recentSearches;
        recent_->visible = !terms.empty();
        if (terms.empty()) {
            return;
        }
        auto* title = recent_->add<ui::Label>(ui::tr("shell.search.recent"), ui::FontStyle{ui::theme().fontHeading, ui::FontWeight::SemiBold});
        title->color = ui::theme().textSecondary;
        for (const std::string& term : terms) {
            recent_->add<ui::Button>(term, [this, term] {
                // not from inside this button's handler: search() removes the button
                later([this, term] {
                    field_->setValue(term);
                    search(term);
                });
            });
        }
    }

    // views about to be removed must not keep the focus
    void keepFocusOutOf(ui::View* view) {
        ui::Application* app = ui::Application::current();
        if (app != nullptr && view->isAncestorOf(app->focus().focused())) {
            app->focus().focus(field_);
        }
    }

    void search(const std::string& typed) {
        Settings probe;
        addRecentSearch(probe, typed);
        ++generation_;
        keepFocusOutOf(results_);
        results_->clearChildren();
        if (probe.recentSearches.empty()) {  // nothing but spaces
            status_->visible = false;
            showRecent();
            return;
        }
        const std::string term = probe.recentSearches.front();
        addRecentSearch(session_.settings(), term);
        session_.saveSettings();
        keepFocusOutOf(recent_);
        recent_->clearChildren();
        recent_->visible = false;
        status_->setText(ui::tr("shell.loading"));
        status_->color = ui::theme().textSecondary;
        status_->visible = true;
        const auto client = session_.client();
        const core::CancelToken token = token_;
        const int generation = generation_;
        run<SearchData>(
            [client, term, token] {
                SearchData data;
                data.movies = client->items(searchQuery(term, "Movie"), token);
                data.series = client->items(searchQuery(term, "Series"), token);
                data.episodes = client->items(searchQuery(term, "Episode"), token);
                return data;
            },
            [this, term, generation](SearchData data) {
                if (generation == generation_) {
                    show(term, std::move(data));
                }
            });
    }

    void show(const std::string& term, SearchData data) {
        auto open = [this](const jellyfin::Item& item) { openItem(session_, item); };
        const std::pair<const char*, core::Result<jellyfin::ItemPage>*> rows[] = {
            {"shell.search.movies", &data.movies},
            {"shell.search.series", &data.series},
            {"shell.search.episodes", &data.episodes},
        };
        bool any = false;
        const core::Error* failure = nullptr;
        for (const auto& row : rows) {
            if (!row.second->ok()) {
                failure = &row.second->error();
                continue;
            }
            if (!row.second->value().items.empty()) {
                addItemRow(*results_, ui::tr(row.first), session_, row.second->value().items,
                           row.first == std::string("shell.search.episodes"), open);
                any = true;
            }
        }
        if (any) {
            status_->visible = false;
        } else if (failure != nullptr) {
            status_->setText(errorText(*failure));
            status_->color = ui::theme().error;
        } else {
            status_->setText(ui::tr("shell.search.none", {term}));
        }
        ui::revealChildren(*results_, 0);
    }

    ui::VScroll* scroll_;
    ui::Box* column_;
    ui::TextField* field_;
    ui::Label* status_;
    ui::Box* recent_;
    ui::Box* results_;
    int generation_ = 0;
};

}  // namespace

std::unique_ptr<ui::Screen> makeSearch(AppSession& session) { return std::make_unique<SearchScreen>(session); }

}  // namespace playfin::app
