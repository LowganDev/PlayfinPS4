#include <random>
#include <string>
#include <vector>

#include "app/shell/cards.hpp"
#include "app/shell/chrome.hpp"
#include "app/shell/format.hpp"
#include "app/shell/library_query.hpp"
#include "app/shell/music_player.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "ui/i18n.hpp"
#include "ui/image_view.hpp"
#include "ui/reveal.hpp"
#include "ui/scroll.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

constexpr int kPage = 100;
constexpr float kCover = 420;
constexpr float kNowPlayingCover = 460;
constexpr float kUpNextWidth = 440;
constexpr std::size_t kUpNext = 8;

int randomIndex(std::size_t count) {
    if (count == 0) {
        return 0;
    }
    static std::mt19937 rng{std::random_device{}()};
    return static_cast<int>(std::uniform_int_distribution<std::size_t>(0, count - 1)(rng));
}

// plays `tracks` from `start` and shows Now playing
void playTracks(AppSession& session, std::vector<jellyfin::Item> tracks, int start, bool shuffle) {
    if (tracks.empty()) {
        return;
    }
    session.music().play(std::move(tracks), start, shuffle);
    ui::Application::current()->push(makeNowPlaying(session));
}

std::string durationText(std::int64_t ticks) {
    const long long minutes = (ticks + 300000000LL) / 600000000LL;
    return ui::tr("shell.details.minutes", {std::to_string(minutes)});
}

// ---- the music library: albums, artists, playlists

class MusicLibraryScreen : public ShellScreen {
public:
    enum class Mode { Albums, Artists, Playlists };

    MusicLibraryScreen(AppSession& session, jellyfin::Item library)
        : ShellScreen(session), library_(std::move(library)), choice_(session.libraryChoices[library_.id]) {
        auto* column = addPage(*this, library_.name);
        count_ = column->add<ui::Label>(ui::tr("shell.loading"), bodyText());
        count_->color = ui::theme().textSecondary;
        auto* bar = column->add<ui::Box>(ui::Axis::Row);
        bar->gap = ui::theme().spacingM;
        albums_ = bar->add<ui::Button>(ui::tr("music.albums"), [this] { show(Mode::Albums); });
        artists_ = bar->add<ui::Button>(ui::tr("music.artists"), [this] { show(Mode::Artists); });
        playlists_ = bar->add<ui::Button>(ui::tr("music.playlists"), [this] { show(Mode::Playlists); });
        sort_ = bar->add<ui::Button>("", [this] {
            choice_ = nextSort(choice_);
            session_.libraryChoices[library_.id] = choice_;
            show(Mode::Albums);
        });
        grid_ = column->add<ui::Grid>(cardSize(CardShape::Square), ui::theme().cardGap, 6);
        grid_->padding = ui::Insets::all(40);
        grid_->margin = {0, -40, 0, -40};
        auto source = std::make_unique<ItemsSource>(session_, std::vector<jellyfin::Item>{}, CardShape::Square,
                                                    [this](const jellyfin::Item& item) { openItem(session_, item); });
        source_ = source.get();
        grid_->setSource(std::move(source));
        grid_->onNearEnd(14, [this] { loadMore(); });
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
        albums_->setPrimary(mode_ == Mode::Albums);
        artists_->setPrimary(mode_ == Mode::Artists);
        playlists_->setPrimary(mode_ == Mode::Playlists);
        sort_->visible = mode_ == Mode::Albums;
        const char* sortKey = choice_.sort == LibrarySort::DateAdded ? "shell.library.sort.date_added"
                              : choice_.sort == LibrarySort::Year    ? "shell.library.sort.year"
                              : choice_.sort == LibrarySort::Rating  ? "shell.library.sort.rating"
                                                                     : "shell.library.sort.name";
        sort_->setText(ui::tr("shell.library.sort", {ui::tr(sortKey)}));
        sort_->parent()->markDirty();
    }

    // a new listing: pages of the previous one still on their way are dropped
    void show(Mode mode) {
        mode_ = mode;
        refreshBar();
        ++generation_;
        loading_ = false;
        total_ = -1;
        source_->items().clear();
        grid_->reload();
        count_->setText(ui::tr("shell.loading"));
        loadMore();
    }

    void loadMore() {
        if (loading_ || (total_ >= 0 && static_cast<int>(source_->items().size()) >= total_)) {
            return;
        }
        loading_ = true;
        const auto client = session_.client();
        const int start = static_cast<int>(source_->items().size());
        const Mode mode = mode_;
        const jellyfin::Item library = library_;
        const LibraryChoice choice = choice_;
        const core::CancelToken token = token_;
        const int generation = generation_;
        run<core::Result<jellyfin::ItemPage>>(
            [client, start, mode, library, choice, token]() -> core::Result<jellyfin::ItemPage> {
                if (mode == Mode::Artists) {
                    return client->artists(library.id, start, kPage, token);
                }
                jellyfin::ItemQuery query;
                if (mode == Mode::Albums) {
                    query = libraryQuery(library, choice, start);
                } else {
                    query.includeTypes = {"Playlist"};
                    query.startIndex = start;
                    query.limit = kPage;
                }
                return client->items(query, token);
            },
            [this, generation](core::Result<jellyfin::ItemPage> page) {
                if (generation != generation_) {
                    return;
                }
                loading_ = false;
                if (!page.ok()) {
                    count_->setText(errorText(page.error()));
                    return;
                }
                total_ = page.value().total;
                auto& items = source_->items();
                items.insert(items.end(), page.value().items.begin(), page.value().items.end());
                count_->setText(ui::translator().plural("shell.library.count", total_));
                grid_->reload();
                if (firstPage_) {
                    firstPage_ = false;
                    focusContent(grid_, albums_);
                } else {
                    focusIfEmpty(grid_);
                }
            });
    }

    jellyfin::Item library_;
    LibraryChoice choice_;
    Mode mode_ = Mode::Albums;
    ui::Label* count_;
    ui::Button* albums_;
    ui::Button* artists_;
    ui::Button* playlists_;
    ui::Button* sort_;
    ui::Grid* grid_;
    ItemsSource* source_;
    int total_ = -1;
    int generation_ = 0;
    bool loading_ = false;
    bool started_ = false;
    bool firstPage_ = true;
};

// ---- an album, a playlist or an artist

// A track in a list: number, title, its artist when it is not the album's, duration. Named after the track.
class TrackRow : public ui::Box {
public:
    TrackRow(const jellyfin::Item& track, int number, bool showArtist, std::function<void()> onPress)
        : ui::Box(ui::Axis::Row) {
        focusable = true;
        drawsOwnFocus = true;
        name = track.name;
        width = ui::SizeSpec::fill();
        padding = rowPadding();
        cornerRadius = ui::theme().radius;
        gap = ui::theme().spacingL;
        crossAlign = ui::CrossAlign::Center;
        const ui::FontStyle body{ui::theme().fontBody, ui::FontWeight::Regular};
        number_ = add<ui::Label>(number > 0 ? std::to_string(number) : "", body);
        number_->width = ui::SizeSpec::fixed(48);
        number_->align = ui::TextAlign::Right;
        title_ = add<ui::Label>(track.name, body);
        title_->width = ui::SizeSpec::fill();
        if (showArtist && !track.artists.empty()) {
            artist_ = add<ui::Label>(track.artists.front(), body);
        }
        duration_ = add<ui::Label>(clockText(static_cast<double>(track.runTimeTicks) / 1e7), body);
        bind(platform::Action::Confirm, ui::tr("ui.select"), [onPress = std::move(onPress)] {
            onPress();
            return true;
        });
        refresh();
    }
    void onFocusChanged(bool focused) override {
        focused_ = focused;
        refresh();
    }
    void onThemeChanged() override {
        refresh();
        ui::Box::onThemeChanged();
    }

private:
    void refresh() {
        background = focused_ ? ui::theme().controlFocused : ui::Color{0, 0, 0, 0};
        const ui::Color text = focused_ ? ui::theme().onAccent : ui::theme().text;
        const ui::Color secondary = focused_ ? ui::theme().onAccent : ui::theme().textSecondary;
        title_->color = text;
        number_->color = secondary;
        duration_->color = secondary;
        if (artist_ != nullptr) {
            artist_->color = secondary;
        }
        markDirty();
    }

    bool focused_ = false;
    ui::Label* number_;
    ui::Label* title_;
    ui::Label* artist_ = nullptr;
    ui::Label* duration_;
};

class MusicItemScreen : public ShellScreen {
public:
    MusicItemScreen(AppSession& session, jellyfin::Item item) : ShellScreen(session), item_(std::move(item)) {
        auto* scroll = add<ui::VScroll>();
        scroll->policy = ui::VScroll::Policy::Nearest;
        auto content = std::make_unique<ui::Box>(ui::Axis::Column);
        content->padding = pagePadding();
        content->gap = ui::theme().spacingL;
        column_ = content.get();
        auto* header = column_->add<ui::Box>(ui::Axis::Row);
        header->gap = 48;
        header->width = ui::SizeSpec::fill();
        auto* cover = header->add<ui::ImageView>();
        cover->width = ui::SizeSpec::fixed(kCover);
        cover->height = ui::SizeSpec::fixed(kCover);
        if (const auto client = session_.client()) {
            cover->setUrl(imageUrlFor(*client, item_, false, static_cast<int>(kCover), static_cast<int>(kCover)));
        }
        auto* info = header->add<ui::Box>(ui::Axis::Column);
        info->width = ui::SizeSpec::fill();
        info->gap = ui::theme().spacingM;
        const bool artist = item_.type == jellyfin::ItemType::MusicArtist;
        const char* kind = artist ? "music.artist"
                           : item_.type == jellyfin::ItemType::Playlist ? "music.playlist"
                                                                        : "music.album";
        auto* kicker = info->add<ui::Label>(ui::tr(kind), bodyText());
        kicker->color = ui::theme().textSecondary;
        auto* title = info->add<ui::Label>(item_.name, ui::FontStyle{64, ui::FontWeight::Bold});
        title->width = ui::SizeSpec::fill();
        title->maxLines = 2;
        if (!item_.albumArtist.empty()) {
            info->add<ui::Label>(item_.albumArtist, titleText(ui::FontWeight::SemiBold));
        }
        meta_ = info->add<ui::Label>("", bodyText());
        meta_->color = ui::theme().textSecondary;
        auto* actions = info->add<ui::Box>(ui::Axis::Row);
        actions->gap = ui::theme().spacingM;
        actions->margin = {ui::theme().spacingL, 0, 0, 0};
        play_ = actions->add<ui::Button>(ui::tr(artist ? "music.play_all" : "music.play"), [this] {
            playTracks(session_, tracks_, 0, false);
        });
        actions->add<ui::Button>(ui::tr("music.shuffle"), [this] {
            playTracks(session_, tracks_, randomIndex(tracks_.size()), true);
        });
        status_ = column_->add<ui::Label>(ui::tr("shell.loading"), bodyText());
        status_->color = ui::theme().textSecondary;
        scroll->setContent(std::move(content));
        remembered = play_;
    }
    void onShow() override {
        if (!loaded_) {
            loaded_ = true;
            later([this] { load(); });
        }
    }

private:
    struct Data {
        core::Result<jellyfin::ItemPage> tracks = jellyfin::ItemPage{};
        core::Result<jellyfin::ItemPage> albums = jellyfin::ItemPage{};
    };

    void load() {
        const auto client = session_.client();
        const jellyfin::Item item = item_;
        const core::CancelToken token = token_;
        run<Data>(
            [client, item, token] {
                Data data;
                if (item.type == jellyfin::ItemType::Playlist) {
                    data.tracks = client->playlistItems(item.id, token);
                } else if (item.type == jellyfin::ItemType::MusicArtist) {
                    jellyfin::ItemQuery tracks;
                    tracks.includeTypes = {"Audio"};
                    tracks.albumArtistId = item.id;
                    tracks.sortBy = "ProductionYear,Album,ParentIndexNumber,IndexNumber,SortName";
                    tracks.limit = 500;
                    data.tracks = client->items(tracks, token);
                    jellyfin::ItemQuery albums;
                    albums.includeTypes = {"MusicAlbum"};
                    albums.albumArtistId = item.id;
                    albums.sortBy = "ProductionYear,SortName";
                    albums.descending = true;
                    data.albums = client->items(albums, token);
                } else {
                    jellyfin::ItemQuery tracks;
                    tracks.parentId = item.id;
                    tracks.sortBy = "ParentIndexNumber,IndexNumber,SortName";
                    tracks.limit = 500;
                    data.tracks = client->items(tracks, token);
                }
                return data;
            },
            [this](Data data) { show(std::move(data)); });
    }

    void show(Data data) {
        if (!data.tracks.ok()) {
            status_->setText(errorText(data.tracks.error()));
            status_->color = ui::theme().error;
            return;
        }
        status_->visible = false;
        tracks_ = data.tracks.value().items;
        std::int64_t ticks = 0;
        for (const auto& track : tracks_) {
            ticks += track.runTimeTicks;
        }
        std::vector<std::string> meta;
        if (item_.productionYear > 0) {
            meta.push_back(std::to_string(item_.productionYear));
        }
        meta.push_back(ui::translator().plural("music.track_count", static_cast<long long>(tracks_.size())));
        meta.push_back(durationText(ticks));
        std::string line;
        for (const auto& part : meta) {
            line += (line.empty() ? "" : " \xC2\xB7 ") + part;
        }
        meta_->setText(line);
        const std::size_t firstRow = column_->children().size();
        if (item_.type == jellyfin::ItemType::MusicArtist) {
            if (data.albums.ok() && !data.albums.value().items.empty()) {
                addItemRow(*column_, ui::tr("music.albums"), session_, data.albums.value().items, CardShape::Square,
                           [this](const jellyfin::Item& album) { openItem(session_, album); });
            }
        } else {
            addTracks();
        }
        ui::revealChildren(*column_, firstRow);
        focusIfEmpty();
    }

    // the track list, with a heading per disc when the album has several
    void addTracks() {
        int discs = 0;
        for (const auto& track : tracks_) {
            discs = std::max(discs, track.parentIndexNumber);
        }
        const bool playlist = item_.type == jellyfin::ItemType::Playlist;
        auto* list = column_->add<ui::Box>(ui::Axis::Column);
        list->width = ui::SizeSpec::fill();
        list->gap = 4;
        int disc = 0;
        for (std::size_t i = 0; i < tracks_.size(); ++i) {
            const jellyfin::Item& track = tracks_[i];
            if (!playlist && discs > 1 && track.parentIndexNumber != disc) {
                disc = track.parentIndexNumber;
                auto* heading = list->add<ui::Label>(ui::tr("music.disc", {std::to_string(disc)}),
                                                     headingText());
                heading->margin = {disc > 1 ? ui::theme().spacingL : 0, 0, ui::theme().spacingS, 0};
            }
            const bool otherArtist = playlist || (!track.artists.empty() && track.artists.front() != item_.albumArtist);
            const int number = playlist ? static_cast<int>(i) + 1 : track.indexNumber;
            const int index = static_cast<int>(i);
            list->add<TrackRow>(track, number, otherArtist, [this, index] {
                playTracks(session_, tracks_, index, false);
            });
        }
    }

    jellyfin::Item item_;
    std::vector<jellyfin::Item> tracks_;
    ui::Box* column_;
    ui::Label* meta_;
    ui::Label* status_;
    ui::Button* play_;
    bool loaded_ = false;
};

// ---- Now playing

// The progress of the track; ← / → seek 10 s while it has the focus.
class SeekBar : public ui::Box {
public:
    explicit SeekBar(MusicPlayer& music) : ui::Box(ui::Axis::Column), music_(music) {
        focusable = true;
        width = ui::SizeSpec::fill();
        gap = ui::theme().spacingS;
        padding = ui::Insets::all(ui::theme().spacingS);
        cornerRadius = ui::theme().radius;
        bar_ = add<ui::ProgressBar>();
        bar_->width = ui::SizeSpec::fill();
        bar_->height = ui::SizeSpec::fixed(6);
        auto* times = add<ui::Box>(ui::Axis::Row);
        times->width = ui::SizeSpec::fill();
        times->align = ui::Align::SpaceBetween;
        const ui::FontStyle style{ui::theme().fontSmall, ui::FontWeight::Regular};
        elapsed_ = times->add<ui::Label>("0:00", style);
        total_ = times->add<ui::Label>("0:00", style);
        elapsed_->color = ui::theme().textSecondary;
        total_->color = ui::theme().textSecondary;
        bind(platform::Action::Left, ui::tr("ui.back"), [this] {
            music_.seekTo(music_.status().position - 10);
            return true;
        });
        bind(platform::Action::Right, "", [this] {
            music_.seekTo(music_.status().position + 10);
            return true;
        });
    }
    void show(const MusicPlayer::Status& status) {
        bar_->value = status.duration > 0 ? static_cast<float>(status.position / status.duration) : 0.0f;
        elapsed_->setText(clockText(status.position));
        total_->setText(clockText(status.duration));
        markDirty();
    }

private:
    MusicPlayer& music_;
    ui::ProgressBar* bar_;
    ui::Label* elapsed_;
    ui::Label* total_;
};

class NowPlayingScreen : public ShellScreen {
public:
    explicit NowPlayingScreen(AppSession& session) : ShellScreen(session), music_(session.music()) {
        auto* row = add<ui::Box>(ui::Axis::Row);
        row->width = ui::SizeSpec::fill();
        row->height = ui::SizeSpec::fill();
        row->padding = pagePadding();
        row->gap = 56;
        row->crossAlign = ui::CrossAlign::Center;
        cover_ = row->add<ui::ImageView>();
        cover_->width = ui::SizeSpec::fixed(kNowPlayingCover);
        cover_->height = ui::SizeSpec::fixed(kNowPlayingCover);
        auto* middle = row->add<ui::Box>(ui::Axis::Column);
        middle->width = ui::SizeSpec::fill();
        middle->gap = ui::theme().spacingM;
        auto* kicker = middle->add<ui::Label>(ui::tr("music.now_playing"), bodyText());
        kicker->color = ui::theme().textSecondary;
        title_ = middle->add<ui::Label>("", ui::FontStyle{56, ui::FontWeight::Bold});
        title_->width = ui::SizeSpec::fill();
        title_->maxLines = 2;
        artist_ = middle->add<ui::Label>("", titleText(ui::FontWeight::SemiBold));
        album_ = middle->add<ui::Label>("", bodyText());
        album_->color = ui::theme().textSecondary;
        seek_ = middle->add<SeekBar>(music_);
        seek_->margin = {ui::theme().spacingL, 0, 0, 0};
        auto* controls = middle->add<ui::Box>(ui::Axis::Row);
        controls->gap = ui::theme().spacingM;
        controls->add<ui::Button>(ui::tr("music.previous"), [this] { music_.previous(); });
        pause_ = controls->add<ui::Button>(ui::tr("music.pause"), [this] { music_.togglePause(); });
        controls->add<ui::Button>(ui::tr("music.next"), [this] { music_.next(); });
        auto* modes = middle->add<ui::Box>(ui::Axis::Row);
        modes->gap = ui::theme().spacingM;
        modes->margin = {ui::theme().spacingS, 0, 0, 0};
        shuffle_ = modes->add<ui::Button>(ui::tr("music.shuffle"),
                                          [this] { music_.setShuffle(!music_.queue().shuffle()); });
        repeat_ = modes->add<ui::Button>("", [this] {
            const player::Repeat repeat = music_.queue().repeat();
            music_.setRepeat(repeat == player::Repeat::Off   ? player::Repeat::All
                             : repeat == player::Repeat::All ? player::Repeat::One
                                                             : player::Repeat::Off);
            refresh();
        });
        modes->add<ui::Button>(ui::tr("music.stop"), [this] { music_.stop(); });
        auto* side = row->add<ui::Box>(ui::Axis::Column);
        side->width = ui::SizeSpec::fixed(kUpNextWidth);
        side->gap = 4;
        side->add<ui::Label>(ui::tr("music.up_next"), headingText())
            ->margin = {0, 0, ui::theme().spacingS, 0};
        upNext_ = side->add<ui::Box>(ui::Axis::Column);
        upNext_->width = ui::SizeSpec::fill();
        upNext_->gap = 4;
        bind(platform::Action::L1, ui::tr("music.previous"), [this] {
            music_.previous();
            return true;
        });
        bind(platform::Action::R1, ui::tr("music.next"), [this] {
            music_.next();
            return true;
        });
        remembered = pause_;
        music_.listen(this, [this] { refresh(); });
        refresh();
    }
    ~NowPlayingScreen() override { music_.unlisten(this); }

private:
    void refresh() {
        if (!music_.active()) {
            // nothing left to show: back to where the user came from
            ui::Application* app = ui::Application::current();
            if (app != nullptr && app->top() == this && !leaving_) {
                leaving_ = true;
                later([] { ui::Application::current()->pop(); });
            }
            return;
        }
        const MusicPlayer::Status status = music_.status();
        const jellyfin::Item& track = *status.track;
        if (track.id != trackId_) {
            trackId_ = track.id;
            title_->setText(track.name);
            artist_->setText(!track.artists.empty() ? track.artists.front() : track.albumArtist);
            album_->setText(track.album);
            if (const auto client = session_.client()) {
                const int size = static_cast<int>(kNowPlayingCover);
                // the album's cover (tracks rarely have their own)
                cover_->setUrl(!track.albumId.empty() && !track.images.albumPrimary.empty()
                                   ? client->imageUrl(track.albumId, jellyfin::ImageType::Primary,
                                                      track.images.albumPrimary, size, size)
                                   : imageUrlFor(*client, track, false, size, size));
            }
        }
        seek_->show(status);
        pause_->setText(ui::tr(status.paused ? "music.resume" : "music.pause"));
        shuffle_->setPrimary(music_.queue().shuffle());
        const player::Repeat repeat = music_.queue().repeat();
        repeat_->setText(ui::tr(repeat == player::Repeat::Off   ? "music.repeat.off"
                                : repeat == player::Repeat::All ? "music.repeat.all"
                                                                : "music.repeat.one"));
        refreshUpNext();
        markDirty();
    }

    void refreshUpNext() {
        const auto upcoming = music_.queue().upcoming(kUpNext);
        std::string signature;
        for (const auto* item : upcoming) {
            signature += item->id + ",";
        }
        if (signature == upNextSignature_) {
            return;
        }
        upNextSignature_ = signature;
        ui::Application* app = ui::Application::current();
        const bool hadFocus = app != nullptr && upNext_->isAncestorOf(app->focus().focused());
        upNext_->clearChildren();
        for (std::size_t i = 0; i < upcoming.size(); ++i) {
            const jellyfin::Item& item = *upcoming[i];
            const int offset = static_cast<int>(i) + 1;
            auto* entry = upNext_->add<ui::ValueRow>(item.name, clockText(static_cast<double>(item.runTimeTicks) / 1e7),
                                                     [this, offset] {
                                                         // not from inside this row: the list is rebuilt
                                                         later([this, offset] { music_.jump(offset); });
                                                     });
            entry->name = item.name;
            entry->width = ui::SizeSpec::fill();
        }
        if (hadFocus) {
            app->focus().focus(upNext_->children().empty() ? static_cast<ui::View*>(pause_) : upNext_);
        }
    }

    MusicPlayer& music_;
    ui::ImageView* cover_;
    ui::Label* title_;
    ui::Label* artist_;
    ui::Label* album_;
    SeekBar* seek_;
    ui::Button* pause_;
    ui::Button* shuffle_;
    ui::Button* repeat_;
    ui::Box* upNext_;
    std::string trackId_;
    std::string upNextSignature_;
    bool leaving_ = false;
};

}  // namespace

std::unique_ptr<ui::Screen> makeMusicLibrary(AppSession& session, jellyfin::Item library) {
    return std::make_unique<MusicLibraryScreen>(session, std::move(library));
}
std::unique_ptr<ui::Screen> makeMusicItem(AppSession& session, jellyfin::Item item) {
    return std::make_unique<MusicItemScreen>(session, std::move(item));
}
std::unique_ptr<ui::Screen> makeNowPlaying(AppSession& session) { return std::make_unique<NowPlayingScreen>(session); }

}  // namespace playfin::app
