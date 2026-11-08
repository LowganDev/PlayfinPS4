#include "app/shell/cards.hpp"
#include "app/shell/chrome.hpp"
#include "app/shell/downloads.hpp"
#include "app/shell/format.hpp"
#include "app/shell/library_query.hpp"
#include "app/shell/music_player.hpp"
#include "app/shell/nav_bar.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "player/session.hpp"
#include "ui/i18n.hpp"
#include "ui/reveal.hpp"
#include "ui/scroll.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

constexpr int kRowItems = 16;

bool playable(const jellyfin::Item& item) {
    return item.type == jellyfin::ItemType::Movie || item.type == jellyfin::ItemType::Episode ||
           item.type == jellyfin::ItemType::Video;
}


struct HomeData {
    core::Result<jellyfin::ItemPage> resume = jellyfin::ItemPage{};
    core::Result<jellyfin::ItemPage> nextUp = jellyfin::ItemPage{};
    core::Result<jellyfin::ItemPage> libraries = jellyfin::ItemPage{};
    std::vector<std::pair<jellyfin::Item, jellyfin::ItemPage>> latest;
    core::Result<jellyfin::ItemPage> airing = jellyfin::ItemPage{};  // when the server has Live TV
};

class HomeScreen : public ShellScreen {
public:
    explicit HomeScreen(AppSession& session) : ShellScreen(session) {
        scroll_ = add<ui::VScroll>();
        scroll_->leadingInset = ui::theme().spacingXxl;
        auto content = std::make_unique<ui::Box>(ui::Axis::Column);
        content->padding = pagePadding();
        content->gap = ui::theme().rowGap;
        column_ = content.get();
        navBar_ = column_->add<NavBar>(session_, Tab::Home);
        scroll_->setHeader(navBar_);
        spinner_ = column_->add<ui::Spinner>();
        scroll_->setContent(std::move(content));
        // the touchpad: everything again from the server (what was added, watched, what is on)
        bind(platform::Action::Touchpad, ui::tr("shell.home.refresh"), [this] {
            AppSession* session = &session_;
            later([session] { ui::Application::current()->replace(makeHome(*session)); });
            return true;
        });
        bind(platform::Action::Menu, "", [this] {  // OPTIONS button: same menu, one hint (triangle) is enough
            openMenu();
            return true;
        });
        bind(platform::Action::Options, ui::tr("shell.menu.hint"), [this] {
            openMenu();
            return true;
        });
    }
    void onShow() override {
        if (!loaded_) {
            loaded_ = true;
            later([this] { load(); });
        } else {
            refresh();  // back from details or the player: Resume and Next up changed
        }
    }

private:
    // the rows of Resume and Next up get their new items in place (the focus stays where it was)
    void refresh() {
        if (resumeRow_ == nullptr && nextUpRow_ == nullptr) {
            return;
        }
        const auto client = session_.client();
        const core::CancelToken token = token_;
        using Pages = std::pair<core::Result<jellyfin::ItemPage>, core::Result<jellyfin::ItemPage>>;
        run<Pages>(
            [client, token] { return Pages{client->resume(kRowItems, token), client->nextUp(kRowItems, token)}; },
            [this](Pages pages) {
                update(resumeRow_, pages.first);
                update(nextUpRow_, pages.second);
            });
    }

    static void update(ui::HList* row, const core::Result<jellyfin::ItemPage>& page) {
        if (row == nullptr || !page.ok() || page.value().items.empty()) {
            return;  // an emptied row stays until the next visit: it may hold the focus
        }
        static_cast<ItemsSource*>(row->source())->items() = page.value().items;
        row->reload();
    }

    void load() {
        const auto client = session_.client();
        const core::CancelToken token = token_;
        const std::vector<std::string> order = session_.settings().libraryOrder;
        run<HomeData>(
            [client, token, order] {
                HomeData data;
                // an unreachable server fails every call: stop at the first one (2 workers on PS4)
                auto unreachable = [](const auto& result) {
                    return !result.ok() && (result.error().kind == core::ErrorKind::Network ||
                                            result.error().kind == core::ErrorKind::Timeout ||
                                            result.error().kind == core::ErrorKind::Cancelled);
                };
                data.resume = client->resume(kRowItems, token);
                if (unreachable(data.resume)) {
                    data.libraries = data.resume.error();
                    return data;
                }
                data.nextUp = client->nextUp(kRowItems, token);
                data.libraries = client->libraries(token);
                if (data.libraries.ok()) {
                    data.libraries.value().items = orderLibraries(std::move(data.libraries.value().items), order);
                    for (const auto& library : data.libraries.value().items) {
                        if (library.collectionType == "livetv") {
                            data.airing = client->airingNow(kRowItems, token);
                            break;
                        }
                    }
                    for (const auto& library : data.libraries.value().items) {
                        if (data.latest.size() >= 8 || token.cancelled()) {
                            break;
                        }
                        if (library.collectionType == "livetv" || library.collectionType == "playlists") {
                            continue;  // channels and playlists are not "added": Live TV has its own row
                        }
                        auto latest = client->latest(library.id, kRowItems, token);
                        if (unreachable(latest)) {
                            break;
                        }
                        if (latest.ok() && !latest.value().items.empty()) {
                            data.latest.emplace_back(library, std::move(latest.value()));
                        }
                    }
                }
                return data;
            },
            [this](HomeData data) { show(std::move(data)); });
    }

    void show(HomeData data) {
        spinner_->visible = false;
        if (!data.libraries.ok()) {
            if (data.libraries.error().kind == core::ErrorKind::Unauthorized) {
                session_.forgetToken();
                showUsers(session_);
                return;
            }
            auto* error = column_->add<ui::Label>(errorText(data.libraries.error()),
                                                  ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
            error->color = ui::theme().error;
            auto* retry = column_->add<ui::Button>(ui::tr("shell.retry"), [this] {
                ui::Application::current()->replace(makeHome(session_));
            });
            if (!session_.downloads().entries().empty()) {  // offline: what is on the console still plays
                column_->add<ui::Button>(ui::tr("downloads.title"), [this] {
                    ui::Application::current()->push(makeDownloads(session_));
                });
            }
            focusContent(retry, navBar_->currentTab());
            return;
        }
        auto open = [this](const jellyfin::Item& item) { openItem(session_, item); };
        const std::size_t firstRow = column_->children().size();
        bool any = false;
        // in Jellyfin's order: the libraries, Resume, Next up, what is on TV, the downloads, the latest additions
        if (!data.libraries.value().items.empty()) {
            addItemRow(*column_, ui::tr("shell.home.libraries"), session_, data.libraries.value().items, true, open);
            any = true;
        }
        if (data.resume.ok() && !data.resume.value().items.empty()) {
            resumeRow_ = addItemRow(*column_, ui::tr("shell.home.resume"), session_, data.resume.value().items, true, open);
            any = true;
        }
        if (data.nextUp.ok() && !data.nextUp.value().items.empty()) {
            nextUpRow_ = addItemRow(*column_, ui::tr("shell.home.next_up"), session_, data.nextUp.value().items, true, open);
            any = true;
        }
        if (data.airing.ok() && !data.airing.value().items.empty()) {
            addItemRow(*column_, ui::tr("livetv.home_row"), session_, data.airing.value().items, true, open);
            any = true;
        }
        // what is on the console, ready to play without the server
        std::vector<jellyfin::Item> downloaded;
        for (const auto& entry : session_.downloads().entries()) {
            if (entry.state == Downloads::State::Done) {
                downloaded.push_back(entry.item);
            }
        }
        if (!downloaded.empty()) {
            AppSession* session = &session_;
            addItemRow(*column_, ui::tr("downloads.title"), session_, std::move(downloaded), true,
                       [session](const jellyfin::Item& item) {
                           const Downloads::Entry* entry = session->downloads().find(item.id);
                           ui::Application::current()->push(
                               makePlayer(*session, item, entry != nullptr ? entry->positionTicks : 0));
                       });
            any = true;
        }
        for (auto& entry : data.latest) {
            const CardShape shape = entry.first.collectionType == "music" ? CardShape::Square : CardShape::Poster;
            addItemRow(*column_, ui::tr("shell.home.latest", {entry.first.name}), session_, entry.second.items, shape,
                       open);
            any = true;
        }
        if (!any) {
            column_->add<ui::Label>(ui::tr("shell.home.empty"), ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        }
        ui::revealChildren(*column_, firstRow);
        if (firstRow < column_->children().size()) {
            focusContent(column_->children()[firstRow].get(), navBar_->currentTab());
        }
    }

    void openMenu() {
        AppSession* session = &session_;
        std::string subtitle;
        if (const jellyfin::StoredServer* server = session_.server()) {
            subtitle = server->name;
        }
        ui::Application::current()->showModal(ui::makeDialog(
            ui::tr("shell.menu.title"), subtitle,
            {{ui::tr("shell.menu.switch_user"),
              [session] { showUsers(*session); }},
             {ui::tr("shell.menu.switch_server"),
              [session] { ui::Application::current()->replace(makeServers(*session)); }},
             {ui::tr("shell.menu.sign_out"), [session] { signOut(*session); }},
             {ui::tr("ui.cancel"), {}}}));
    }

    ui::VScroll* scroll_;
    ui::Box* column_;
    NavBar* navBar_;
    ui::Spinner* spinner_;
    ui::HList* resumeRow_ = nullptr;
    ui::HList* nextUpRow_ = nullptr;
    bool loaded_ = false;
};

class LibraryScreen : public ShellScreen {
public:
    LibraryScreen(AppSession& session, jellyfin::Item library)
        : ShellScreen(session), library_(std::move(library)), choice_(session.libraryChoices[library_.id]) {
        auto* column = addPage(*this, library_.name);
        count_ = column->add<ui::Label>(ui::tr("shell.loading"), ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        count_->color = ui::theme().textSecondary;
        auto* bar = column->add<ui::Box>(ui::Axis::Row);
        bar->gap = ui::theme().spacingM;
        sort_ = bar->add<ui::Button>("", [this] { change(nextSort(choice_)); });
        order_ = bar->add<ui::Button>("", [this] {
            LibraryChoice next = choice_;
            next.descending = !next.descending;
            change(next);
        });
        filter_ = bar->add<ui::Button>("", [this] {
            LibraryChoice next = choice_;
            next.filter = nextFilter(next.filter);
            change(next);
        });
        // six posters per row; room around them for the grown focused card, taken from the margins
        grid_ = column->add<ui::Grid>(cardSize(false), ui::theme().cardGap, kGridColumns);
        grid_->padding = ui::Insets::all(40);
        grid_->margin = {0, -40, 0, -40};
        auto source = std::make_unique<ItemsSource>(session_, std::vector<jellyfin::Item>{}, false,
                                                    [this](const jellyfin::Item& item) { openItem(session_, item); });
        source_ = source.get();
        grid_->setSource(std::move(source));
        grid_->onNearEnd(14, [this] { loadMore(); });
        // ← in the first column: the A–Z jump (names in order); △: a quick look at the item
        grid_->bind(
            platform::Action::Left, ui::tr("shell.library.jump_hint"),
            [this] {
                if (grid_->focusedIndex() % grid_->columns != 0 || !jumpable()) {
                    return false;
                }
                openJump();
                return true;
            },
            [this] { return grid_->focusedIndex() % grid_->columns == 0 && jumpable(); });
        grid_->bind(platform::Action::Options, ui::tr("shell.peek.hint"), [this] {
            const int index = grid_->focusedIndex();
            if (index >= 0 && index < static_cast<int>(source_->items().size())) {
                openPeek(*this, source_->items()[static_cast<std::size_t>(index)]);
            }
            return true;
        });
        refreshBar();
    }
    void onShow() override {
        if (!started_) {
            started_ = true;
            later([this] { loadMore(); });
        }
    }

private:
    void refreshBar() {
        sort_->setText(ui::tr("shell.library.sort", {sortName(choice_.sort)}));
        order_->setText(ui::tr(choice_.descending ? "shell.library.descending" : "shell.library.ascending"));
        filter_->setText(ui::tr("shell.library.filter", {filterName(choice_.filter)}));
    }

    static std::string sortName(LibrarySort sort) {
        switch (sort) {
        case LibrarySort::Name:
            return ui::tr("shell.library.sort.name");
        case LibrarySort::DateAdded:
            return ui::tr("shell.library.sort.date_added");
        case LibrarySort::Year:
            return ui::tr("shell.library.sort.year");
        case LibrarySort::Rating:
            return ui::tr("shell.library.sort.rating");
        }
        return {};
    }

    static std::string filterName(LibraryFilter filter) {
        switch (filter) {
        case LibraryFilter::All:
            return ui::tr("shell.library.filter.all");
        case LibraryFilter::Unplayed:
            return ui::tr("shell.library.filter.unplayed");
        case LibraryFilter::Favorites:
            return ui::tr("shell.library.filter.favorites");
        }
        return {};
    }

    bool jumpable() const { return choice_.sort == LibrarySort::Name && !choice_.descending && total_ > 0; }

    // "#" and A to Z, nine to a row
    void openJump() {
        auto overlay = std::make_unique<ui::Stack>();
        overlay->background = ui::theme().scrim;
        overlay->horizontal = ui::CrossAlign::Center;
        overlay->vertical = ui::CrossAlign::Center;
        auto* card = overlay->add<ui::Box>(ui::Axis::Column);
        card->padding = ui::Insets::all(ui::theme().spacingXl);
        card->gap = ui::theme().spacingM;
        card->background = ui::theme().surfaceRaised;
        card->cornerRadius = ui::theme().radius;
        card->add<ui::Label>(ui::tr("shell.library.jump"), ui::FontStyle{ui::theme().fontHeading, ui::FontWeight::SemiBold});
        const std::string letters = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        ui::Box* row = nullptr;
        for (std::size_t i = 0; i < letters.size(); ++i) {
            if (i % 9 == 0) {
                row = card->add<ui::Box>(ui::Axis::Row);
                row->gap = ui::theme().spacingS;
            }
            const char letter = letters[i];
            auto* button = row->add<ui::Button>(std::string(1, letter), [this, letter] {
                ui::Application::current()->closeModal();
                jumpTo(letter);
            });
            button->width = ui::SizeSpec::fixed(84);
            if (i == 0) {
                overlay->remembered = button;
            }
        }
        overlay->bind(platform::Action::Back, ui::tr("ui.cancel"), [] {
            ui::Application::current()->closeModal();
            return true;
        });
        ui::Application::current()->showModal(std::move(overlay));
    }

    // how many names come before the letter tells its first item; the pages up to it are loaded, then focused
    void jumpTo(char letter) {
        if (letter == '#') {
            focusIndex(0);
            return;
        }
        jellyfin::ItemQuery query = libraryQuery(library_, choice_, 0);
        query.nameLessThan = std::string(1, letter);
        query.limit = 0;
        const auto client = session_.client();
        const core::CancelToken token = token_;
        const int generation = generation_;
        run<core::Result<jellyfin::ItemPage>>([client, query, token] { return client->items(query, token); },
                                              [this, generation](core::Result<jellyfin::ItemPage> before) {
                                                  if (generation != generation_ || !before.ok()) {
                                                      return;
                                                  }
                                                  jumpTarget_ = std::min(before.value().total, total_ - 1);
                                                  continueJump();
                                              });
    }

    void continueJump() {
        if (jumpTarget_ < 0) {
            return;
        }
        if (jumpTarget_ < static_cast<int>(source_->items().size())) {
            const int target = jumpTarget_;
            jumpTarget_ = -1;
            focusIndex(target);
            return;
        }
        loadMore();  // its page calls continueJump again
    }

    void focusIndex(int index) {
        if (ui::View* view = grid_->itemView(index)) {
            ui::Application::current()->focus().focus(view);
        }
    }

    // a new listing: the pages of the previous one still on their way are dropped (generation)
    void change(const LibraryChoice& choice) {
        choice_ = choice;
        session_.libraryChoices[library_.id] = choice;
        refreshBar();
        ++generation_;
        loading_ = false;
        jumpTarget_ = -1;
        total_ = -1;
        source_->items().clear();
        grid_->reload();
        count_->setText(ui::tr("shell.loading"));
        count_->color = ui::theme().textSecondary;
        loadMore();
    }

    void loadMore() {
        if (loading_ || (total_ >= 0 && static_cast<int>(source_->items().size()) >= total_)) {
            return;
        }
        loading_ = true;
        const jellyfin::ItemQuery query =
            libraryQuery(library_, choice_, static_cast<int>(source_->items().size()));
        const auto client = session_.client();
        const core::CancelToken token = token_;
        const int generation = generation_;
        run<core::Result<jellyfin::ItemPage>>([client, query, token] { return client->items(query, token); },
                                              [this, generation](core::Result<jellyfin::ItemPage> page) {
                                                  if (generation != generation_) {
                                                      return;
                                                  }
                                                  loading_ = false;
                                                  if (!page.ok()) {
                                                      count_->setText(errorText(page.error()));
                                                      count_->color = ui::theme().error;
                                                      return;
                                                  }
                                                  total_ = page.value().total;
                                                  auto& items = source_->items();
                                                  items.insert(items.end(), page.value().items.begin(),
                                                               page.value().items.end());
                                                  count_->setText(ui::translator().plural("shell.library.count", total_));
                                                  grid_->reload();
                                                  if (firstPage_) {
                                                      firstPage_ = false;
                                                      focusContent(grid_, sort_);
                                                  } else {
                                                      focusIfEmpty(grid_);
                                                  }
                                                  continueJump();
                                              });
    }

    jellyfin::Item library_;
    LibraryChoice choice_;
    ui::Label* count_;
    ui::Button* sort_;
    ui::Button* order_;
    ui::Button* filter_;
    ui::Grid* grid_;
    ItemsSource* source_;
    int total_ = -1;
    int jumpTarget_ = -1;  // an A–Z jump waiting for its page
    int generation_ = 0;
    bool loading_ = false;
    bool started_ = false;
    bool firstPage_ = true;
};

struct DetailsData {
    core::Result<jellyfin::Item> item = jellyfin::Item{};
    core::Result<jellyfin::ItemPage> seasons = jellyfin::ItemPage{};
    std::string seasonId;  // the season shown first: the first one with unwatched episodes
    core::Result<jellyfin::ItemPage> episodes = jellyfin::ItemPage{};
    core::Result<jellyfin::ItemPage> siblings = jellyfin::ItemPage{};  // an episode's season, for previous/next
};

const char* const kHeart = "\xE2\x99\xA5 ";
const char* const kCheck = "\xE2\x9C\x93 ";

// an empty line takes no room
void setLine(ui::Label* label, const std::string& text) {
    label->setText(text);
    label->visible = !text.empty();
}

std::string joined(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& part : parts) {
        out += (out.empty() ? "" : " \xC2\xB7 ") + part;
    }
    return out;
}

class DetailsScreen : public ShellScreen {
public:
    DetailsScreen(AppSession& session, jellyfin::Item item) : ShellScreen(session), item_(std::move(item)) {
        enterRise = 40;
        // the shade under the picture: the picture has it baked in and covers it once there
        auto* shade = add<BackdropShade>();
        backdrop_ = add<ui::ImageView>();
        backdrop_->width = ui::SizeSpec::fill();
        backdrop_->height = ui::SizeSpec::fill();
        backdrop_->cornerRadius = 0;
        backdrop_->placeholder = ui::Color{0, 0, 0, 0};
        backdrop_->backdrop = true;
        shade->setPicture(backdrop_);
        scroll_ = add<ui::VScroll>();
        auto content = std::make_unique<ui::Box>(ui::Axis::Column);
        content->padding = pagePadding();
        content->gap = ui::theme().spacingL;
        column_ = content.get();
        if (item_.type == jellyfin::ItemType::Episode && !item_.seriesName.empty()) {
            auto* series = column_->add<ui::Label>(item_.seriesName, ui::FontStyle{ui::theme().fontTitle, ui::FontWeight::Regular});
            series->color = ui::theme().textSecondary;
        }
        title_ = column_->add<ui::Label>(item_.name, ui::FontStyle{ui::theme().fontDisplay, ui::FontWeight::Bold});
        title_->width = ui::SizeSpec::fill();
        title_->maxLines = 2;
        meta_ = column_->add<ui::Label>(metaLine(item_), ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        meta_->color = ui::theme().textSecondary;
        genres_ = column_->add<ui::Label>(joined(item_.genres), ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        genres_->color = ui::theme().textSecondary;
        // the version's quality, when it ends, who directed it
        chips_ = column_->add<ui::Box>(ui::Axis::Row);
        chips_->gap = ui::theme().spacingS;
        chips_->crossAlign = ui::CrossAlign::Center;
        directors_ = column_->add<ui::Label>("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        directors_->color = ui::theme().textSecondary;
        directors_->visible = false;
        overview_ = column_->add<ui::Label>(item_.overview, ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        overview_->width = ui::SizeSpec::fixed(1100);
        overview_->maxLines = 6;
        setLine(meta_, metaLine(item_));
        setLine(genres_, joined(item_.genres));
        setLine(overview_, item_.overview);
        actions_ = column_->add<ui::Box>(ui::Axis::Row);
        actions_->gap = ui::theme().spacingM;
        play_ = actions_->add<ui::Button>(ui::tr("shell.details.play"), [this] { playFrom(0); });
        resume_ = actions_->add<ui::Button>("", [this] { playFrom(item_.userData.positionTicks); });
        fromStart_ = actions_->add<ui::Button>(ui::tr("player.from_start"), [this] { playFrom(0); });
        version_ = actions_->add<ui::Button>("", [this] { later([this] { chooseVersion(); }); });
        version_->visible = false;
        download_ = actions_->add<ui::Button>("", [this] { pressDownload(); });
        download_->visible = false;
        favorite_ = actions_->add<ui::Button>("", [this] { toggleFavorite(); });
        watched_ = actions_->add<ui::Button>("", [this] { toggleWatched(); });
        previous_ = actions_->add<ui::Button>(ui::tr("shell.details.previous_episode"), [this] { openSibling(-1); });
        next_ = actions_->add<ui::Button>(ui::tr("shell.details.next_episode"), [this] { openSibling(1); });
        previous_->visible = false;
        next_->visible = false;
        refreshActions();
        scroll_->setContent(std::move(content));
        session_.downloads().listen(this, [this] { refreshDownload(); });
    }
    ~DetailsScreen() override { session_.downloads().unlisten(this); }
    void onShow() override {
        if (!loaded_) {
            loaded_ = true;
            later([this] { load(); });
        } else {
            reloadItem();  // back from the player: resume position and watched state changed
        }
    }

private:
    void load() {
        const auto client = session_.client();
        const jellyfin::Item item = item_;
        const core::CancelToken token = token_;
        run<DetailsData>(
            [client, item, token] {
                DetailsData data;
                data.item = client->item(item.id, token);
                if (item.type == jellyfin::ItemType::Series && !token.cancelled()) {
                    data.seasons = client->seasons(item.id, token);
                    if (data.seasons.ok() && !data.seasons.value().items.empty()) {
                        const auto& seasons = data.seasons.value().items;
                        data.seasonId = seasons.front().id;
                        for (const auto& season : seasons) {
                            if (season.userData.unplayedCount > 0) {
                                data.seasonId = season.id;
                                break;
                            }
                        }
                        data.episodes = client->episodes(item.id, data.seasonId, token);
                    }
                }
                if (item.type == jellyfin::ItemType::Episode && !item.seriesId.empty() && !token.cancelled()) {
                    data.siblings = client->episodes(item.seriesId, "", token);  // every season: next crosses them
                }
                return data;
            },
            [this](DetailsData data) { show(std::move(data)); });
    }

    void reloadItem() {
        const auto client = session_.client();
        const std::string id = item_.id;
        const core::CancelToken token = token_;
        run<core::Result<jellyfin::Item>>([client, id, token] { return client->item(id, token); },
                                          [this](core::Result<jellyfin::Item> item) {
                                              if (item.ok()) {
                                                  showItem(item.value());
                                              }
                                          });
    }

    // the buttons are kept (one of them may have the focus); a focused button that disappears hands the focus to
    // the first visible one
    void refreshActions() {
        const bool playableItem = playable(item_);
        const std::int64_t position = item_.userData.positionTicks;
        play_->visible = playableItem && position <= 0;
        resume_->visible = playableItem && position > 0;
        fromStart_->visible = resume_->visible;
        resume_->setText(ui::tr("player.resume", {clockText(static_cast<double>(position) / player::kTicksPerSecond)}));
        const std::string favorite = ui::tr("shell.details.favorite");
        favorite_->setText(item_.userData.favorite ? kHeart + favorite : favorite);
        const std::string watched = ui::tr("shell.details.watched");
        watched_->setText(item_.userData.played ? kCheck + watched : watched);
        const auto index = siblingIndex();
        previous_->visible = index > 0;
        next_->visible = index >= 0 && index + 1 < static_cast<int>(siblings_.size());
        actions_->markDirty();
        ui::Application* app = ui::Application::current();
        if (app == nullptr) {
            return;
        }
        ui::View* focused = app->focus().focused();
        if (focused != nullptr && actions_->isAncestorOf(focused) && !focused->visible) {
            app->focus().focus(resume_->visible ? resume_ : play_->visible ? play_ : favorite_);
        }
    }

    int siblingIndex() const {
        for (std::size_t i = 0; i < siblings_.size(); ++i) {
            if (siblings_[i].id == item_.id) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void playFrom(std::int64_t ticks) {
        ui::Application::current()->push(makePlayer(session_, item_, ticks, sourceId_));
    }

    // ---- downloads: a film or an episode, or the season shown of a series

    void refreshDownload() {
        const bool single = item_.type == jellyfin::ItemType::Movie || item_.type == jellyfin::ItemType::Episode;
        const bool season = item_.type == jellyfin::ItemType::Series && episodesRow_ != nullptr;
        download_->visible = single || season;
        if (season) {
            download_->setText(ui::tr("downloads.season"));
            return;
        }
        const Downloads::Entry* entry = session_.downloads().find(item_.id);
        if (entry == nullptr) {
            download_->setText(ui::tr("downloads.download"));
        } else if (entry->state == Downloads::State::Done) {
            download_->setText(ui::tr("downloads.downloaded"));
        } else if (entry->state == Downloads::State::Running && entry->size > 0) {
            download_->setText(ui::tr("downloads.progress", {std::to_string(entry->received * 100 / entry->size)}));
        } else if (entry->state == Downloads::State::Failed) {
            download_->setText(ui::tr("downloads.retry"));
        } else {
            download_->setText(ui::tr("downloads.queued"));
        }
    }

    void pressDownload() {
        Downloads& downloads = session_.downloads();
        if (item_.type == jellyfin::ItemType::Series) {
            if (episodesRow_ == nullptr) {
                return;
            }
            for (const jellyfin::Item& episode : static_cast<ItemsSource*>(episodesRow_->source())->items()) {
                downloads.add(episode);
            }
            ui::Application::current()->toast(ui::tr("downloads.season_added"));
            return;
        }
        const Downloads::Entry* entry = downloads.find(item_.id);
        if (entry == nullptr) {
            downloads.add(item_);
        } else if (entry->state == Downloads::State::Failed) {
            downloads.retry(item_.id);
        } else {
            ui::Application::current()->push(makeDownloads(session_));
        }
        refreshDownload();
    }

    const jellyfin::MediaSource* chosenSource() const {
        for (const jellyfin::MediaSource& source : item_.mediaSources) {
            if (source.id == sourceId_) {
                return &source;
            }
        }
        return item_.mediaSources.empty() ? nullptr : &item_.mediaSources.front();
    }

    // the chips of the version chosen and "Se termine à …"
    void refreshQuality() {
        chips_->clearChildren();
        std::vector<std::string> chips;
        if (const jellyfin::MediaSource* source = chosenSource()) {
            chips = qualityChips(*source);
        }
        if (playable(item_) && item_.type != jellyfin::ItemType::Series) {
            const std::int64_t at = session_.now();
            const std::string ends = endsAtText(item_, at, session_.utcOffsetMinutes(at));
            if (!ends.empty()) {
                chips.push_back(ends);
            }
        }
        for (std::size_t i = 0; i < chips.size(); ++i) {
            const bool last = i + 1 == chips.size() && chips[i].find(' ') != std::string::npos;
            auto* chip = chips_->add<ui::Label>(chips[i], ui::FontStyle{ui::theme().fontSmall, ui::FontWeight::SemiBold});
            chip->padding = ui::Insets::symmetric(4, 12);
            chip->cornerRadius = 6;
            chip->background = last ? ui::Color{0, 0, 0, 0} : ui::withAlpha(ui::theme().text, 0x24);
            chip->color = last ? ui::theme().textSecondary : ui::theme().text;
        }
        chips_->visible = !chips.empty();
        setLine(directors_, directorsLine(item_));
        if (item_.mediaSources.size() > 1) {
            const jellyfin::MediaSource* source = chosenSource();
            version_->setText(ui::tr("shell.details.version", {source != nullptr ? source->name : std::string()}));
            version_->visible = true;
        }
        column_->markDirty();
    }

    void chooseVersion() {
        std::vector<std::string> names;
        int selected = 0;
        for (std::size_t i = 0; i < item_.mediaSources.size(); ++i) {
            const jellyfin::MediaSource& source = item_.mediaSources[i];
            names.push_back(source.name.empty() ? std::to_string(i + 1) : source.name);
            selected = &source == chosenSource() ? static_cast<int>(i) : selected;
        }
        ui::Application::current()->showModal(
            ui::makeChoiceDialog(ui::tr("shell.details.version", {""}), names, selected, [this](int index) {
                sourceId_ = item_.mediaSources[static_cast<std::size_t>(index)].id;
                refreshQuality();
            }));
    }

    // the cast and crew: a portrait each (actors first, as the server lists them); one opens the person
    void showPeople() {
        if (item_.people.empty() || castRow_ != nullptr) {
            return;
        }
        std::vector<jellyfin::Item> people;
        for (const jellyfin::Person& person : item_.people) {
            if (person.id.empty()) {
                continue;
            }
            jellyfin::Item card;
            card.id = person.id;
            card.name = person.name;
            card.type = jellyfin::ItemType::Person;
            card.role = !person.role.empty() ? person.role : person.type == "Director" ? directorWord() : "";
            card.images.primary = person.imageTag;
            people.push_back(std::move(card));
        }
        if (people.empty()) {
            return;
        }
        AppSession* session = &session_;
        castRow_ = addItemRow(*column_, ui::tr("shell.details.cast"), session_, std::move(people), CardShape::Poster,
                              [session](const jellyfin::Item& person) {
                                  ui::Application::current()->push(makePerson(*session, person));
                              });
    }

    static std::string directorWord() {
        std::string line = ui::tr("shell.details.directed_by", {""});
        while (!line.empty() && (line.back() == ' ' || line.back() == ':')) {
            line.pop_back();
        }
        return line;
    }

    void toggleFavorite() {
        const auto client = session_.client();
        const std::string id = item_.id;
        const bool favorite = !item_.userData.favorite;
        const core::CancelToken token = token_;
        run<core::Result<core::Unit>>([client, id, favorite, token] { return client->setFavorite(id, favorite, token); },
                                      [this, favorite](core::Result<core::Unit> result) {
                                          if (!result.ok()) {
                                              ui::Application::current()->toast(errorText(result.error()));
                                              return;
                                          }
                                          item_.userData.favorite = favorite;
                                          refreshActions();
                                      });
    }

    void toggleWatched() {
        const auto client = session_.client();
        const std::string id = item_.id;
        const bool played = !item_.userData.played;
        const core::CancelToken token = token_;
        run<core::Result<core::Unit>>([client, id, played, token] { return client->setPlayed(id, played, token); },
                                      [this, played](core::Result<core::Unit> result) {
                                          if (!result.ok()) {
                                              ui::Application::current()->toast(errorText(result.error()));
                                              return;
                                          }
                                          item_.userData.played = played;
                                          item_.userData.positionTicks = 0;  // the server forgets the position too
                                          refreshActions();
                                      });
    }

    // the previous or next episode takes this page's place (○ still returns to where the user came from)
    void openSibling(int step) {
        const int index = siblingIndex() + step;
        if (siblingIndex() < 0 || index < 0 || index >= static_cast<int>(siblings_.size())) {
            return;
        }
        AppSession* session = &session_;
        jellyfin::Item sibling = siblings_[static_cast<std::size_t>(index)];
        // not from inside this screen's button handler: replaceTop destroys the screen
        later([session, sibling] { ui::Application::current()->replaceTop(makeDetails(*session, sibling)); });
    }

    void showItem(const jellyfin::Item& item) {
        item_ = item;
        title_->setText(item_.name);
        setLine(meta_, metaLine(item_));
        setLine(genres_, joined(item_.genres));
        setLine(overview_, item_.overview);
        column_->markDirty();
        refreshQuality();
        refreshDownload();
        refreshActions();
    }

    void show(DetailsData data) {
        if (data.siblings.ok()) {
            siblings_ = data.siblings.value().items;
        }
        if (data.item.ok()) {
            showItem(data.item.value());
            if (const auto client = session_.client()) {
                backdrop_->setUrl(backdropUrlFor(*client, item_, 1920, 1080));
            }
        } else {
            refreshActions();
        }
        const std::size_t firstRow = column_->children().size();
        showPeople();
        if (item_.type == jellyfin::ItemType::Series && data.seasons.ok() && !data.seasons.value().items.empty()) {
            const auto& seasons = data.seasons.value().items;
            std::string seasonName = seasons.front().name;
            for (const auto& season : seasons) {
                if (season.id == data.seasonId) {
                    seasonName = season.name;
                }
            }
            addItemRow(*column_, ui::tr("shell.details.seasons"), session_, seasons, false,
                       [this](const jellyfin::Item& season) { showSeason(season); });
            std::vector<jellyfin::Item> episodes = data.episodes.ok() ? data.episodes.value().items
                                                                      : std::vector<jellyfin::Item>{};
            episodesRow_ = addItemRow(*column_, seasonName, session_, std::move(episodes), true,
                                      [this](const jellyfin::Item& episode) {
                                          ui::Application::current()->push(makeDetails(session_, episode));
                                      });
            episodesTitle_ = dynamic_cast<ui::Label*>(episodesRow_->parent()->children().front().get());
        }
        refreshDownload();
        ui::revealChildren(*column_, firstRow);
        focusIfEmpty();
    }

    void showSeason(const jellyfin::Item& season) {
        if (episodesRow_ == nullptr) {
            return;
        }
        const auto client = session_.client();
        const std::string seriesId = item_.id;
        const std::string seasonId = season.id;
        const std::string seasonName = season.name;
        const core::CancelToken token = token_;
        run<core::Result<jellyfin::ItemPage>>(
            [client, seriesId, seasonId, token] { return client->episodes(seriesId, seasonId, token); },
            [this, seasonName](core::Result<jellyfin::ItemPage> page) {
                if (!page.ok() || episodesRow_ == nullptr) {
                    return;
                }
                auto* source = static_cast<ItemsSource*>(episodesRow_->source());
                source->items() = page.value().items;
                episodesRow_->reload();
                if (episodesTitle_ != nullptr) {
                    episodesTitle_->setText(seasonName);
                }
            });
    }

    jellyfin::Item item_;
    std::vector<jellyfin::Item> siblings_;
    ui::VScroll* scroll_;
    ui::Box* column_;
    ui::Label* title_;
    ui::Label* meta_;
    ui::Label* genres_;
    ui::Label* overview_;
    ui::Box* chips_ = nullptr;
    ui::Label* directors_ = nullptr;
    ui::Button* version_ = nullptr;
    ui::Button* download_ = nullptr;
    ui::HList* castRow_ = nullptr;
    std::string sourceId_;  // the version chosen (empty: the server's first)
    ui::ImageView* backdrop_ = nullptr;
    ui::Box* actions_ = nullptr;
    ui::Button* play_ = nullptr;
    ui::Button* resume_ = nullptr;
    ui::Button* fromStart_ = nullptr;
    ui::Button* favorite_ = nullptr;
    ui::Button* watched_ = nullptr;
    ui::Button* previous_ = nullptr;
    ui::Button* next_ = nullptr;
    ui::HList* episodesRow_ = nullptr;
    ui::Label* episodesTitle_ = nullptr;
    bool loaded_ = false;
};

}  // namespace

void openItem(AppSession& session, const jellyfin::Item& item) {
    ui::Application* app = ui::Application::current();
    switch (item.type) {
    case jellyfin::ItemType::CollectionFolder:
    case jellyfin::ItemType::UserView:
    case jellyfin::ItemType::Folder:
    case jellyfin::ItemType::BoxSet:  // a collection: its films and series, like a library
        app->push(item.collectionType == "music"    ? makeMusicLibrary(session, item)
                  : item.collectionType == "livetv" ? makeLiveTv(session, item)
                                                    : makeLibrary(session, item));
        break;
    case jellyfin::ItemType::TvChannel:
        app->push(makePlayer(session, item, 0));
        break;
    case jellyfin::ItemType::Program: {  // a programme on now: its channel
        jellyfin::Item channel;
        channel.id = item.channelId;
        channel.type = jellyfin::ItemType::TvChannel;
        channel.name = item.channelName;
        app->push(makePlayer(session, channel, 0));
        break;
    }
    case jellyfin::ItemType::Playlist:
        app->push(item.mediaType == "Video" ? makeLibrary(session, item) : makeMusicItem(session, item));
        break;
    case jellyfin::ItemType::MusicAlbum:
    case jellyfin::ItemType::MusicArtist:
        app->push(makeMusicItem(session, item));
        break;
    case jellyfin::ItemType::Person:
        app->push(makePerson(session, item));
        break;
    case jellyfin::ItemType::Audio:
        session.music().play({item}, 0, false);
        app->push(makeNowPlaying(session));
        break;
    default:
        app->push(makeDetails(session, item));
        break;
    }
}

std::unique_ptr<ui::Screen> makeHome(AppSession& session) { return std::make_unique<HomeScreen>(session); }
std::unique_ptr<ui::Screen> makeLibrary(AppSession& session, jellyfin::Item library) {
    return std::make_unique<LibraryScreen>(session, std::move(library));
}
std::unique_ptr<ui::Screen> makeDetails(AppSession& session, jellyfin::Item item) {
    return std::make_unique<DetailsScreen>(session, std::move(item));
}

}  // namespace playfin::app
