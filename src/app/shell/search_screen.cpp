#include <string>
#include <vector>

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

// What a search looks for, one row each; L2 / R2 narrow it to one of them.
struct SearchKind {
    const char* title;  // translation key
    std::vector<std::string> types;
    CardShape shape;
};

const std::vector<SearchKind>& searchKinds() {
    static const std::vector<SearchKind> kinds = {
        {"shell.search.movies", {"Movie"}, CardShape::Poster},
        {"shell.search.series", {"Series"}, CardShape::Poster},
        {"shell.search.episodes", {"Episode"}, CardShape::Wide},
        {"shell.search.people", {"Person"}, CardShape::Poster},
        {"shell.search.music", {"MusicAlbum", "MusicArtist", "Audio"}, CardShape::Square},
        {"shell.search.channels", {"TvChannel"}, CardShape::Wide},
    };
    return kinds;
}

using SearchData = std::vector<core::Result<jellyfin::ItemPage>>;  // one per kind asked for

jellyfin::ItemQuery searchQuery(const std::string& term, const std::vector<std::string>& types, int limit) {
    jellyfin::ItemQuery query;
    query.searchTerm = term;
    query.includeTypes = types;
    query.limit = limit;
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
        filter_ = column_->add<ui::Label>("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::SemiBold});
        refreshFilter();
        bind(platform::Action::L2, "", [this] { return stepFilter(-1); });
        bind(platform::Action::R2, ui::tr("shell.search.filter_hint"), [this] { return stepFilter(1); });
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
    // ---- the kinds searched: all of them, or one (L2 / R2)

    void refreshFilter() {
        const std::string name =
            filterIndex_ < 0 ? ui::tr("shell.search.all")
                             : ui::tr(searchKinds()[static_cast<std::size_t>(filterIndex_)].title);
        filter_->setText(ui::tr("shell.search.filter", {name}));
    }

    bool stepFilter(int step) {
        const int count = static_cast<int>(searchKinds().size()) + 1;  // "Tout", then each kind
        filterIndex_ = ((filterIndex_ + 1 + step) % count + count) % count - 1;
        refreshFilter();
        if (!lastTerm_.empty()) {
            search(lastTerm_);
        }
        return true;
    }

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
        lastTerm_ = term;
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
        const int filter = filterIndex_;
        run<SearchData>(
            [client, term, token, filter] {
                SearchData data;
                const auto& kinds = searchKinds();
                for (std::size_t i = 0; i < kinds.size(); ++i) {
                    if (filter >= 0 && static_cast<int>(i) != filter) {
                        data.emplace_back(jellyfin::ItemPage{});
                        continue;
                    }
                    // one kind alone: more of it
                    data.push_back(client->items(searchQuery(term, kinds[i].types, filter >= 0 ? 100 : kRowItems), token));
                }
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
        bool any = false;
        const core::Error* failure = nullptr;
        for (std::size_t i = 0; i < data.size() && i < searchKinds().size(); ++i) {
            const SearchKind& kind = searchKinds()[i];
            if (!data[i].ok()) {
                failure = &data[i].error();
                continue;
            }
            if (!data[i].value().items.empty()) {
                addItemRow(*results_, ui::tr(kind.title), session_, data[i].value().items, kind.shape, open);
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
    ui::Label* filter_;
    int filterIndex_ = -1;  // -1: every kind
    std::string lastTerm_;
    int generation_ = 0;
};

}  // namespace

std::unique_ptr<ui::Screen> makeSearch(AppSession& session) { return std::make_unique<SearchScreen>(session); }

}  // namespace playfin::app
