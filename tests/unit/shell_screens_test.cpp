#include <doctest/doctest.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app/shell/cards.hpp"
#include "app/shell/music_player.hpp"
#include "player/queue.hpp"
#include "app/shell/nav_bar.hpp"
#include "app/shell/screens.hpp"
#include "app/shell/session.hpp"
#include "app/settings.hpp"
#include "core/task.hpp"
#include "core/version.hpp"
#include "support/fake_jellyfin.hpp"
#include "support/files.hpp"
#include "support/fixed_metrics.hpp"
#include "ui/application.hpp"
#include "ui/i18n.hpp"
#include "ui/image_view.hpp"
#include "ui/label.hpp"
#include "ui/theme.hpp"
#include "ui/video_source.hpp"
#include "ui/scroll.hpp"
#include "ui/widgets.hpp"

using namespace playfin;
using platform::Action;
using platform::Event;

namespace {

struct TempDir {
    std::string path;
    TempDir() {
        char tmpl[] = "/tmp/playfin-screens-XXXXXX";
        REQUIRE(mkdtemp(tmpl) != nullptr);
        path = tmpl;
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};

net::HttpConfig testHttp() {
    net::HttpConfig config;
    config.caFile = test::fixturePath("tls/ca.crt");
    return config;
}

// The shell on an Application with network jobs inline (as in the screenshot tests).
struct Shell {
    TempDir dir;
    core::WorkerPool pool{1};
    core::MainQueue mainQueue;
    test::FixedMetrics metrics;
    app::AppSession session{testHttp(), dir.path + "/servers.json", dir.path + "/settings.json"};
    std::unique_ptr<ui::Application> application;
    Shell() {
        ui::translator().load(std::string(PLAYFIN_SOURCE_DIR) + "/resources/i18n", "en");
        REQUIRE(session.load().ok());
        session.synchronous = true;
        session.discovery = false;
        session.licensesDir = std::string(PLAYFIN_SOURCE_DIR) + "/resources/licenses";
        session.noticesFile = std::string(PLAYFIN_SOURCE_DIR) + "/THIRD_PARTY_LICENSES";
        application =
            std::make_unique<ui::Application>(ui::Application::Services{pool, mainQueue, metrics, nullptr});
    }
    // A server known at `url` with user u-demo signed in with `token`, used last.
    void remember(const std::string& url, const std::string& token) {
        auto& server = jellyfin::upsertServer(session.data(), "f1e2d3c4b5a6");
        server.name = "Test Server";
        jellyfin::rememberUrl(server, url);
        auto& user = jellyfin::upsertUser(server, "u-demo");
        user.name = "demo";
        user.token = token;
        session.data().lastServerId = "f1e2d3c4b5a6";
        session.data().lastUserId = "u-demo";
        REQUIRE(session.save());
    }
    void frames(int count = 5) {
        for (int i = 0; i < count; ++i) {
            session.updateMusic();
            application->update(1000.0f / 60.0f);
        }
    }
    void press(Action action) {
        Event event;
        event.type = Event::Type::ActionDown;
        event.action = action;
        application->handle(event);
        frames();
    }
    std::string storedToken() {
        jellyfin::ServerStore store(dir.path + "/servers.json");
        const auto data = store.load();
        REQUIRE(data.ok());
        for (const auto& server : data.value().servers) {
            for (const auto& user : server.users) {
                if (user.id == "u-demo") {
                    return user.token;
                }
            }
        }
        return {};
    }
};

// Every visible label text under `view`.
void texts(const ui::View& view, std::vector<std::string>& out) {
    if (!view.visible) {
        return;
    }
    if (const auto* label = dynamic_cast<const ui::Label*>(&view)) {
        out.push_back(label->text());
    }
    for (const auto& child : view.children()) {
        texts(*child, out);
    }
}

bool shows(ui::Application& app, const std::string& text) {
    std::vector<std::string> all;
    texts(app.root(), all);
    for (const std::string& t : all) {
        if (t == text) {
            return true;
        }
    }
    return false;
}

jellyfin::Item episode() {
    jellyfin::Item item;
    item.id = "e-1";
    item.name = "Pilot";
    item.type = jellyfin::ItemType::Episode;
    item.seriesId = "s-1";
    item.seasonId = "se-1";
    item.indexNumber = 1;
    item.runTimeTicks = 40000000;
    return item;
}

// The shell signed in on a fake server (direct play of the test clip).
struct Playing {
    test::FakeJellyfin server;
    Shell shell;
    Playing() {
        shell.remember(server.baseUrl(), "tok-demo");
        shell.session.useServer("f1e2d3c4b5a6", server.baseUrl());
        jellyfin::StoredUser user;
        user.id = "u-demo";
        user.token = "tok-demo";
        shell.session.useStoredUser(user);
        shell.application->push(std::make_unique<ui::Screen>());  // what the player goes back to
    }
    // frames with the video source polled, as the app's frame loop does
    void frames(int count = 5) {
        for (int i = 0; i < count; ++i) {
            if (ui::VideoSource* video = shell.application->videoSource()) {
                video->renderVideo();
            }
            shell.application->update(1000.0f / 60.0f);
        }
    }
};

}  // namespace

TEST_CASE("details of a partly watched item offer to resume or to start over") {
    test::FakeJellyfin server;
    Shell shell;
    shell.remember(server.baseUrl(), "tok-demo");
    shell.session.useServer("f1e2d3c4b5a6", server.baseUrl());
    jellyfin::StoredUser user;
    user.id = "u-demo";
    user.token = "tok-demo";
    shell.session.useStoredUser(user);
    jellyfin::Item movie;
    movie.id = "m-1";
    movie.type = jellyfin::ItemType::Movie;
    shell.application->push(app::makeDetails(shell.session, movie));
    shell.frames(10);
    CHECK(shows(*shell.application, "Resume (2:00)"));
    CHECK(shows(*shell.application, "Play from the beginning"));
}

TEST_CASE("the player reports the session to the server and stops when leaving") {
    Playing p;
    p.shell.application->push(app::makePlayer(p.shell.session, episode(), 0));
    p.frames(10);
    REQUIRE(p.shell.application->videoSource() != nullptr);
    CHECK(p.shell.application->videoSource()->videoLayer().width == 320);
    auto reports = p.server.reports();
    REQUIRE_FALSE(reports.empty());
    CHECK(reports[0].path == "/Sessions/Playing");
    CHECK(reports[0].body.find(R"("PlaySessionId":"ps-direct-1")") != std::string::npos);
    p.shell.press(Action::Back);
    CHECK(p.shell.application->depth() == 1);
    CHECK(p.shell.application->videoSource() == nullptr);
    reports = p.server.reports();
    CHECK(reports.back().path == "/Sessions/Playing/Stopped");
}

TEST_CASE("the player asks for the quality chosen in the settings") {
    Playing p;
    p.shell.session.settings().maxBitrate = 4000000;
    p.shell.application->push(app::makePlayer(p.shell.session, episode(), 0));
    p.frames(10);
    CHECK(p.server.playbackInfoBody().find(R"("MaxStreamingBitrate":4000000)") != std::string::npos);
    p.shell.press(Action::Back);
}

TEST_CASE("settings are saved next to servers.json and read back by a new session") {
    TempDir dir;
    {
        app::AppSession session(testHttp(), dir.path + "/servers.json", dir.path + "/settings.json");
        session.settings().accent = "green";
        CHECK(session.saveSettings());
    }
    app::AppSession session(testHttp(), dir.path + "/servers.json", dir.path + "/settings.json");
    CHECK(session.settings().accent == "green");
    app::AppSession noFile(testHttp(), dir.path + "/servers.json");
    CHECK_FALSE(noFile.saveSettings());
}

TEST_CASE("the intro skip button shows at the start of an episode with an intro and seeks past it") {
    Playing p;
    jellyfin::Item alone = episode();
    alone.seasonId.clear();  // no next episode: the 4 s clip is all "last 30 s", the next-episode button would win
    p.shell.application->push(app::makePlayer(p.shell.session, alone, 0));
    p.frames(10);
    CHECK(shows(*p.shell.application, "Skip intro"));
    p.shell.press(Action::Confirm);
    p.frames(30);
    CHECK_FALSE(shows(*p.shell.application, "Skip intro"));
}

TEST_CASE("the track menu lists the audio and subtitle choices") {
    Playing p;
    p.shell.application->push(app::makePlayer(p.shell.session, episode(), 0));
    p.frames(10);
    p.shell.press(Action::Options);
    CHECK(p.shell.application->hasModal());
    CHECK(shows(*p.shell.application, "Audio"));
    CHECK(shows(*p.shell.application, "Subtitles"));
    p.shell.press(Action::Back);
    CHECK_FALSE(p.shell.application->hasModal());
}

TEST_CASE("an audio change that restarts a transcode ends the first play session and starts the next") {
    Playing p;
    jellyfin::Item film = episode();
    film.id = "m-transcode";
    p.shell.application->push(app::makePlayer(p.shell.session, film, 0));
    p.frames(10);
    p.shell.press(Action::Options);
    p.shell.press(Action::Confirm);  // Audio
    p.frames(2);
    REQUIRE(p.shell.application->hasModal());
    p.shell.press(Action::Confirm);  // the first audio track: a transcode restarts for it
    p.frames(10);
    CHECK(p.server.count("/Items/m-transcode/PlaybackInfo") == 2);
    std::vector<std::string> paths;
    for (const auto& report : p.server.reports()) {
        if (report.path != "/Sessions/Playing/Progress") {
            paths.push_back(report.path);
        }
    }
    CHECK(paths == std::vector<std::string>{"/Sessions/Playing", "/Sessions/Playing/Stopped", "/Sessions/Playing"});
    p.shell.press(Action::Back);
}

TEST_CASE("near the end of an episode with a next one, the next-episode button shows") {
    Playing p;
    p.shell.application->push(app::makePlayer(p.shell.session, episode(), 0));
    p.frames(10);
    CHECK(shows(*p.shell.application, "Next episode"));
}

TEST_CASE("at the end of a film the player closes itself cleanly") {
    Playing p;
    jellyfin::Item film = episode();
    film.id = "m-1";        // no segments: no skip button takes the Confirm press
    film.seasonId.clear();  // no next episode: the end goes back
    p.shell.application->push(app::makePlayer(p.shell.session, film, 35000000));  // 3.5 s of 4
    p.frames(5);
    REQUIRE(p.shell.application->depth() == 2);
    p.shell.press(Action::Confirm);  // the test player starts paused: play to the end
    for (int i = 0; i < 400 && p.shell.application->depth() == 2; ++i) {
        p.frames(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(p.shell.application->depth() == 1);
    CHECK(p.shell.application->videoSource() == nullptr);
}

TEST_CASE("leaving the player while it opens the stream is clean and reports at most one stop") {
    Playing p;
    p.shell.application->push(app::makePlayer(p.shell.session, episode(), 0));
    p.shell.press(Action::Back);
    p.frames(5);
    int stops = 0;
    for (const auto& report : p.server.reports()) {
        stops += report.path == "/Sessions/Playing/Stopped" ? 1 : 0;
    }
    CHECK(stops <= 1);
    CHECK(p.shell.application->videoSource() == nullptr);
}

TEST_CASE("a refused PlaybackInfo shows the error with retry and back") {
    test::FakeServer refusing;
    refusing.http().Post(R"(/Items/([^/]+)/PlaybackInfo)", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"MediaSources":[],"ErrorCode":"NoCompatibleStream"})", "application/json");
    });
    refusing.start();
    Shell shell;
    shell.remember(refusing.baseUrl(), "tok-demo");
    shell.session.useServer("f1e2d3c4b5a6", refusing.baseUrl());
    shell.application->push(std::make_unique<ui::Screen>());
    shell.application->push(app::makePlayer(shell.session, episode(), 0));
    shell.frames(10);
    CHECK(shell.application->hasModal());
    CHECK(shows(*shell.application, "This video cannot be played."));
    CHECK(shows(*shell.application, "Try again"));
}

TEST_CASE("a library whose server counts more items than it returns stops asking") {
    test::FakeServer server;
    std::atomic<int> pages{0};
    server.http().Get(R"(/(Users/[^/]+/)?Items)", [&pages](const httplib::Request& req, httplib::Response& res) {
        ++pages;
        const bool first = !req.has_param("startIndex") || req.get_param_value("startIndex") == "0";
        std::string items;
        for (int i = 0; first && i < 20; ++i) {
            items += std::string(i > 0 ? "," : "") + R"({"Id":"m-)" + std::to_string(i) + R"(","Name":"Film","Type":"Movie"})";
        }
        res.set_content(R"({"Items":[)" + items + R"(],"TotalRecordCount":50})", "application/json");
    });
    server.start();
    Shell shell;
    shell.remember(server.baseUrl(), "tok-demo");
    shell.session.useServer("f1e2d3c4b5a6", server.baseUrl());
    shell.session.useStoredUser(shell.session.data().servers.front().users.front());
    jellyfin::Item library;
    library.id = "lib-movies";
    library.collectionType = "movies";
    library.type = jellyfin::ItemType::CollectionFolder;
    shell.application->push(app::makeLibrary(shell.session, library));
    shell.frames(10);
    for (int i = 0; i < 4; ++i) {
        shell.press(Action::Down);
    }
    for (int i = 0; i < 6; ++i) {
        shell.press(i % 2 == 0 ? Action::Right : Action::Left);
    }
    CHECK(pages == 2);
}

TEST_CASE("a network error when picking a saved user keeps the saved token") {
    test::FakeServer flaky;
    flaky.http().Get("/Users/Public", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(test::readFixture("jellyfin/users_public.json"), "application/json");
    });
    flaky.http().Get("/QuickConnect/Enabled", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("false", "application/json");
    });
    flaky.http().Get("/Users/Me", [](const httplib::Request&, httplib::Response& res) { res.status = 503; });
    flaky.start();
    Shell shell;
    shell.remember(flaky.baseUrl(), "tok-demo");
    shell.session.useServer("f1e2d3c4b5a6", flaky.baseUrl());
    shell.application->push(app::makeUsers(shell.session));
    shell.frames();
    shell.press(Action::Confirm);  // "demo", the first user
    CHECK(shell.storedToken() == "tok-demo");
    CHECK(shell.application->depth() == 1);  // still choosing: the error is shown there
}

TEST_CASE("a token refused at startup leads to the users screen, with the servers screen behind it") {
    test::FakeJellyfin server;
    Shell shell;
    shell.remember(server.baseUrl(), "revoked");
    shell.application->push(app::makeStart(shell.session));
    shell.frames(10);
    CHECK(shell.storedToken().empty());
    REQUIRE(shell.application->depth() == 2);
    shell.press(Action::Back);
    CHECK(shell.application->depth() == 1);
}

TEST_CASE("a server pressed twice while it connects opens one users screen") {
    test::FakeJellyfin server;
    Shell shell;
    shell.remember(server.baseUrl(), "tok");
    shell.session.synchronous = false;
    shell.application->push(app::makeServers(shell.session));
    shell.frames(2);
    Event confirm;
    confirm.type = Event::Type::ActionDown;
    confirm.action = Action::Confirm;
    shell.application->handle(confirm);
    shell.application->handle(confirm);
    for (int i = 0; i < 200; ++i) {
        shell.frames(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(shell.application->depth() == 2);
    CHECK(server.count("/System/Info/Public") == 1);
}

TEST_CASE("a card steps forward when focused and back when the focus leaves") {
    Shell shell;
    struct Cards : ui::Screen {
        app::ItemCard* first;
        app::ItemCard* second;
        Cards() {
            auto* row = add<ui::Box>(ui::Axis::Row);
            first = row->add<app::ItemCard>(true);
            second = row->add<app::ItemCard>(true);
        }
    };
    auto* screen = static_cast<Cards*>(shell.application->push(std::make_unique<Cards>()));
    shell.frames(20);  // 333 ms
    CHECK(screen->first->focusProgress() == doctest::Approx(1));
    CHECK(screen->second->focusProgress() == doctest::Approx(0));
    CHECK(screen->first->drawsOwnFocus);
    shell.press(Action::Right);
    shell.frames(20);
    CHECK(screen->first->focusProgress() == doctest::Approx(0));
    CHECK(screen->second->focusProgress() == doctest::Approx(1));
}

TEST_CASE("cards have the identity's sizes") {
    CHECK(app::cardImageSize(true).width == doctest::Approx(400));
    CHECK(app::cardImageSize(true).height == doctest::Approx(225));
    CHECK(app::cardImageSize(false).width == doctest::Approx(260));
    CHECK(app::cardImageSize(false).height == doctest::Approx(390));
}

namespace {
// the URL of every ImageView under `view`
void imageUrls(const ui::View& view, std::vector<std::string>& out) {
    if (const auto* image = dynamic_cast<const ui::ImageView*>(&view)) {
        out.push_back(image->url());
    }
    for (const auto& child : view.children()) {
        imageUrls(*child, out);
    }
}
}  // namespace

TEST_CASE("the home screen shows the logo and who is watching") {
    test::FakeJellyfin server;
    Shell shell;
    shell.remember(server.baseUrl(), "tok-demo");
    shell.session.useServer("f1e2d3c4b5a6", server.baseUrl());
    jellyfin::StoredUser user;
    user.id = "u-demo";
    user.token = "tok-demo";
    shell.session.useStoredUser(user);
    shell.application->push(app::makeHome(shell.session));
    shell.frames(10);
    CHECK(shows(*shell.application, "Playfin"));
    CHECK(shows(*shell.application, "Test Server · demo"));
    CHECK(shows(*shell.application, "D"));  // the avatar's initial
}

TEST_CASE("details show the item's backdrop behind the text") {
    test::FakeJellyfin server;
    Shell shell;
    shell.remember(server.baseUrl(), "tok-demo");
    shell.session.useServer("f1e2d3c4b5a6", server.baseUrl());
    jellyfin::StoredUser user;
    user.id = "u-demo";
    user.token = "tok-demo";
    shell.session.useStoredUser(user);
    jellyfin::Item movie;
    movie.id = "m-1";
    movie.type = jellyfin::ItemType::Movie;
    shell.application->push(app::makeDetails(shell.session, movie));
    shell.frames(10);
    std::vector<std::string> urls;
    imageUrls(shell.application->root(), urls);
    bool backdrop = false;
    for (const auto& url : urls) {
        backdrop = backdrop || url.find("/Images/Backdrop") != std::string::npos;
    }
    CHECK(backdrop);
}

namespace {
jellyfin::Item moviesLibrary() {
    jellyfin::Item library;
    library.id = "lib-movies";
    library.name = "Movies";
    library.type = jellyfin::ItemType::CollectionFolder;
    library.collectionType = "movies";
    return library;
}

// Signed in on `server` as demo.
void signIn(Shell& shell, const test::FakeJellyfin& server) {
    shell.remember(server.baseUrl(), "tok-demo");
    shell.session.useServer("f1e2d3c4b5a6", server.baseUrl());
    jellyfin::StoredUser user;
    user.id = "u-demo";
    user.token = "tok-demo";
    shell.session.useStoredUser(user);
}

const ui::Button* focusedButton(ui::Application& app) {
    return dynamic_cast<const ui::Button*>(app.focus().focused());
}

std::string lastItemsTarget(const test::FakeJellyfin& server) {
    std::string target;
    for (const auto& seen : server.requests()) {
        if (seen.path == "/Items") {
            target = seen.target;
        }
    }
    return target;
}
}  // namespace

TEST_CASE("a library can be sorted and filtered, and keeps the choice for the session") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.application->push(std::make_unique<ui::Screen>());
    shell.application->push(app::makeLibrary(shell.session, moviesLibrary()));
    shell.frames(10);
    CHECK(shows(*shell.application, "250 items"));
    CHECK(shows(*shell.application, "Sort: Name"));
    CHECK(shows(*shell.application, "Ascending"));
    CHECK(shows(*shell.application, "Filter: All"));
    CHECK(dynamic_cast<const app::ItemCard*>(shell.application->focus().focused()) != nullptr);  // content first
    shell.press(Action::Up);
    REQUIRE(focusedButton(*shell.application) != nullptr);
    shell.press(Action::Confirm);
    CHECK(shows(*shell.application, "Sort: Date added"));
    CHECK(shows(*shell.application, "Descending"));
    CHECK(lastItemsTarget(server).find("sortBy=DateCreated") != std::string::npos);
    CHECK(lastItemsTarget(server).find("sortOrder=Descending") != std::string::npos);
    CHECK(shows(*shell.application, "Movie 249"));  // the fake reverses descending lists
    shell.press(Action::Right);  // the order button
    shell.press(Action::Confirm);
    CHECK(shows(*shell.application, "Ascending"));
    CHECK(shows(*shell.application, "Movie 000"));
    shell.press(Action::Right);  // the filter button
    shell.press(Action::Confirm);
    shell.press(Action::Confirm);
    CHECK(shows(*shell.application, "Filter: Favorites"));
    CHECK(shows(*shell.application, "25 items"));
    CHECK(lastItemsTarget(server).find("filters=IsFavorite") != std::string::npos);
    shell.press(Action::Back);
    shell.application->push(app::makeLibrary(shell.session, moviesLibrary()));
    shell.frames(10);
    CHECK(shows(*shell.application, "Sort: Date added"));
    CHECK(shows(*shell.application, "Filter: Favorites"));
    CHECK(shows(*shell.application, "25 items"));
}

TEST_CASE("a page of the previous order arriving after a change of sort is dropped") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.session.synchronous = false;  // pages arrive later, through the main queue
    shell.application->push(std::make_unique<ui::Screen>());
    shell.application->push(app::makeLibrary(shell.session, moviesLibrary()));
    shell.frames(1);  // the first page is asked for; its answer waits in the main queue until the next frame
    // change the sort before the first page arrived
    std::vector<ui::Button*> buttons;
    std::function<void(ui::View&)> collect = [&](ui::View& view) {
        if (auto* button = dynamic_cast<ui::Button*>(&view)) {
            buttons.push_back(button);
        }
        for (const auto& child : view.children()) {
            collect(*child);
        }
    };
    collect(*shell.application->top());
    REQUIRE(buttons.size() == 3);
    shell.application->focus().focus(buttons[0]);
    shell.press(Action::Confirm);
    for (int i = 0; i < 200 && !shows(*shell.application, "Movie 249"); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        shell.frames(1);
    }
    shell.frames(20);
    CHECK(shows(*shell.application, "Movie 249"));
    CHECK_FALSE(shows(*shell.application, "Movie 000"));
}

namespace {
// the visible button showing `text` under `view`
ui::Button* findButton(ui::View& view, const std::string& text) {
    if (!view.visible) {
        return nullptr;
    }
    if (auto* button = dynamic_cast<ui::Button*>(&view)) {
        std::vector<std::string> labels;
        texts(*button, labels);
        if (!labels.empty() && labels.front() == text) {
            return button;
        }
    }
    for (const auto& child : view.children()) {
        if (ui::Button* found = findButton(*child, text)) {
            return found;
        }
    }
    return nullptr;
}

// focuses the button showing `text` on the top screen and presses it
void pressButton(Shell& shell, const std::string& text) {
    ui::Button* button = findButton(*shell.application->top(), text);
    REQUIRE_MESSAGE(button != nullptr, text);
    REQUIRE(shell.application->focus().focus(button));
    shell.press(Action::Confirm);
}

jellyfin::Item bigBuckBunny() {
    jellyfin::Item movie;
    movie.id = "m-1";
    movie.name = "Big Buck Bunny";
    movie.type = jellyfin::ItemType::Movie;
    return movie;
}

int requestsTo(const test::FakeJellyfin& server, const std::string& method, const std::string& path) {
    int n = 0;
    for (const auto& seen : server.requests()) {
        n += seen.method == method && seen.path == path ? 1 : 0;
    }
    return n;
}
}  // namespace

TEST_CASE("details show the genres and toggle favourite and watched") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.application->push(std::make_unique<ui::Screen>());
    shell.application->push(app::makeDetails(shell.session, bigBuckBunny()));
    shell.frames(10);
    CHECK(shows(*shell.application, "Animation · Comedy"));
    CHECK(shows(*shell.application, "\xE2\x99\xA5 Favorite"));  // a favourite already
    CHECK(shows(*shell.application, "Watched"));
    pressButton(shell, "\xE2\x99\xA5 Favorite");
    CHECK(requestsTo(server, "DELETE", "/UserFavoriteItems/m-1") == 1);
    CHECK(shows(*shell.application, "Favorite"));
    CHECK(dynamic_cast<ui::Button*>(shell.application->focus().focused()) == findButton(*shell.application->top(), "Favorite"));
    pressButton(shell, "Watched");
    CHECK(requestsTo(server, "POST", "/UserPlayedItems/m-1") == 1);
    CHECK(shows(*shell.application, "\xE2\x9C\x93 Watched"));
    pressButton(shell, "\xE2\x9C\x93 Watched");
    CHECK(requestsTo(server, "DELETE", "/UserPlayedItems/m-1") == 1);
    CHECK(shows(*shell.application, "Watched"));
}

TEST_CASE("a series opens on the first season with unwatched episodes and switches seasons in place") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    jellyfin::Item series;
    series.id = "s-1";
    series.name = "Test Show";
    series.type = jellyfin::ItemType::Series;
    shell.application->push(std::make_unique<ui::Screen>());
    shell.application->push(app::makeDetails(shell.session, series));
    shell.frames(10);
    CHECK(shows(*shell.application, "Season 1"));
    CHECK(shows(*shell.application, "Third"));  // season 2's episode
    CHECK_FALSE(shows(*shell.application, "Pilot"));
    shell.press(Action::Down);  // from the action buttons to the seasons, Season 1 first
    REQUIRE(dynamic_cast<app::ItemCard*>(shell.application->focus().focused()) != nullptr);
    shell.press(Action::Confirm);
    CHECK(shows(*shell.application, "Pilot"));
    CHECK_FALSE(shows(*shell.application, "Third"));
    CHECK(shell.application->depth() == 2);
}

TEST_CASE("an episode offers the next one and swaps the page for it") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.application->push(std::make_unique<ui::Screen>());
    shell.application->push(app::makeDetails(shell.session, episode()));
    shell.frames(10);
    CHECK(shows(*shell.application, "Next episode"));
    CHECK_FALSE(shows(*shell.application, "Previous episode"));
    pressButton(shell, "Next episode");
    shell.frames(10);
    CHECK(shell.application->depth() == 2);
    CHECK(shows(*shell.application, "Second"));
    CHECK(shows(*shell.application, "Previous episode"));
    CHECK_FALSE(shows(*shell.application, "Next episode"));
    shell.press(Action::Back);
    CHECK(shell.application->depth() == 1);
}

TEST_CASE("details reload the item when shown again (back from the player) and keep the focus") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.application->push(std::make_unique<ui::Screen>());
    shell.application->push(app::makeDetails(shell.session, bigBuckBunny()));
    shell.frames(10);
    CHECK(server.count("/Items/m-1") == 1);
    pressButton(shell, "Watched");
    ui::View* focused = shell.application->focus().focused();
    shell.application->push(std::make_unique<ui::Screen>());  // the player
    shell.frames(5);
    shell.application->pop();
    shell.frames(10);
    CHECK(server.count("/Items/m-1") == 2);
    CHECK(shell.application->focus().focused() == focused);
}

TEST_CASE("details slide up further than other screens") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    ui::Screen* details = shell.application->push(app::makeDetails(shell.session, bigBuckBunny()));
    CHECK(details->shiftY == doctest::Approx(40));
}

TEST_CASE("replacing the top screen keeps the ones below") {
    Shell shell;
    shell.application->push(std::make_unique<ui::Screen>());
    shell.application->push(std::make_unique<ui::Screen>());
    ui::Screen* swapped = shell.application->replaceTop(std::make_unique<ui::Screen>());
    CHECK(shell.application->depth() == 2);
    CHECK(shell.application->top() == swapped);
}

namespace {
// opens the search field, types `term` and accepts it
void search(Shell& shell, const std::string& term) {
    ui::View* field = nullptr;
    std::function<void(ui::View&)> find = [&](ui::View& view) {
        if (dynamic_cast<ui::TextField*>(&view) != nullptr) {
            field = &view;
        }
        for (const auto& child : view.children()) {
            find(*child);
        }
    };
    find(*shell.application->top());
    REQUIRE(field != nullptr);
    static_cast<ui::TextField*>(field)->setValue("");  // the editor starts from the field's value
    REQUIRE(shell.application->focus().focus(field));
    shell.press(Action::Confirm);
    Event text;
    text.type = Event::Type::Text;
    text.text = term;
    shell.application->handle(text);
    shell.press(Action::Confirm);
    shell.frames(5);
}
}  // namespace

TEST_CASE("search shows movies, series and episodes in their rows and remembers the search") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.application->push(app::makeSearch(shell.session));
    shell.frames(5);
    search(shell, "movie 01");
    CHECK(shows(*shell.application, "Movies"));
    CHECK(shows(*shell.application, "Movie 010"));
    CHECK_FALSE(shows(*shell.application, "Series"));
    CHECK_FALSE(shows(*shell.application, "Episodes"));
    REQUIRE_FALSE(shell.session.settings().recentSearches.empty());
    CHECK(shell.session.settings().recentSearches.front() == "movie 01");
    CHECK(lastItemsTarget(server).find("searchTerm=movie") != std::string::npos);
    CHECK(lastItemsTarget(server).find("limit=24") != std::string::npos);
    search(shell, "pilot");
    CHECK(shows(*shell.application, "Episodes"));
    CHECK(shows(*shell.application, "Test Show"));
    CHECK_FALSE(shows(*shell.application, "Movies"));
    CHECK_FALSE(shows(*shell.application, "Movie 010"));
}

TEST_CASE("search says when nothing matches") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.application->push(app::makeSearch(shell.session));
    shell.frames(5);
    search(shell, "zzz");
    CHECK(shows(*shell.application, "No results for \u201Czzz\u201D"));
}

TEST_CASE("recent searches show while the field is empty and search again when chosen") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.session.settings().recentSearches = {"movie 24", "pilot"};
    shell.application->push(app::makeSearch(shell.session));
    shell.frames(5);
    CHECK(shows(*shell.application, "Recent searches"));
    pressButton(shell, "movie 24");
    shell.frames(5);
    CHECK(shows(*shell.application, "Movie 240"));
    CHECK_FALSE(shows(*shell.application, "Recent searches"));
    CHECK(shell.session.settings().recentSearches.front() == "movie 24");
}

TEST_CASE("search results are still there when coming back from an item") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.application->push(app::makeSearch(shell.session));
    shell.frames(5);
    search(shell, "movie 01");
    shell.application->push(app::makeDetails(shell.session, bigBuckBunny()));
    shell.frames(5);
    shell.press(Action::Back);
    shell.frames(5);
    CHECK(shows(*shell.application, "Movie 010"));
}

namespace {
// the visible button or value row whose first label is `text`
ui::View* findPressable(ui::View& view, const std::string& text) {
    if (!view.visible) {
        return nullptr;
    }
    if (dynamic_cast<ui::Button*>(&view) != nullptr || dynamic_cast<ui::ValueRow*>(&view) != nullptr) {
        std::vector<std::string> labels;
        texts(view, labels);
        if (!labels.empty() && labels.front() == text) {
            return &view;
        }
    }
    for (const auto& child : view.children()) {
        if (ui::View* found = findPressable(*child, text)) {
            return found;
        }
    }
    return nullptr;
}

void pressAnywhere(Shell& shell, const std::string& text) {
    ui::View* view = findPressable(shell.application->root(), text);
    REQUIRE_MESSAGE(view != nullptr, text);
    REQUIRE(shell.application->focus().focus(view));
    shell.press(Action::Confirm);
    shell.frames(3);
}

struct SettingsShell {
    test::FakeJellyfin server;
    Shell shell;
    SettingsShell() {
        signIn(shell, server);
        shell.application->push(app::makeSettings(shell.session));
        shell.frames(10);
    }
    ~SettingsShell() { ui::applyAccent(ui::accentPalette().front()); }
};
}  // namespace

TEST_CASE("the accent colour applies at once and is saved") {
    SettingsShell s;
    CHECK(shows(*s.shell.application, "Accent colour"));
    CHECK(shows(*s.shell.application, "PlayStation blue"));
    pressAnywhere(s.shell, "Accent colour");
    REQUIRE(s.shell.application->hasModal());
    pressAnywhere(s.shell, "Red");
    CHECK_FALSE(s.shell.application->hasModal());
    CHECK(ui::theme().accent.r == 0xE5);
    CHECK(ui::theme().accent.g == 0x48);
    CHECK(shows(*s.shell.application, "Red"));
    CHECK(app::loadSettings(s.shell.dir.path + "/settings.json").accent == "red");
}

TEST_CASE("the playback quality is saved") {
    SettingsShell s;
    CHECK(shows(*s.shell.application, "Auto (40 Mbit/s)"));
    pressAnywhere(s.shell, "Maximum playback quality");
    pressAnywhere(s.shell, "1.5 Mbit/s");
    CHECK(s.shell.session.settings().maxBitrate == 1500000);
    CHECK(shows(*s.shell.application, "1.5 Mbit/s"));
    CHECK(app::loadSettings(s.shell.dir.path + "/settings.json").maxBitrate == 1500000);
}

TEST_CASE("a new language is saved and applies at the next start") {
    SettingsShell s;
    CHECK(shows(*s.shell.application, "System"));
    pressAnywhere(s.shell, "Language");
    pressAnywhere(s.shell, "Deutsch");
    CHECK(s.shell.session.settings().language == "de");
    CHECK(shows(*s.shell.application, "Deutsch (at next start)"));
    pressAnywhere(s.shell, "Language");
    pressAnywhere(s.shell, "System");
    CHECK(shows(*s.shell.application, "System"));
}

TEST_CASE("about shows the version and the server, and the licences page opens each licence") {
    SettingsShell s;
    CHECK(shows(*s.shell.application, core::version()));
    CHECK(shows(*s.shell.application, "Test Server \xC2\xB7 10.10.7"));
    pressAnywhere(s.shell, "Third-party licences");
    s.shell.frames(10);
    CHECK(s.shell.application->depth() == 2);
    CHECK(shows(*s.shell.application, "FFmpeg"));
    CHECK(shows(*s.shell.application, "LGPL-3.0-or-later"));
    pressAnywhere(s.shell, "FFmpeg");
    s.shell.frames(10);
    CHECK(s.shell.application->depth() == 3);
    std::vector<std::string> all;
    texts(s.shell.application->root(), all);
    bool lgpl = false;
    for (const auto& text : all) {
        lgpl = lgpl || text.find("GNU LESSER GENERAL PUBLIC LICENSE") != std::string::npos;
    }
    CHECK(lgpl);
}

TEST_CASE("the licences page opens the copyright notices, with the FreeType credit") {
    SettingsShell s;
    pressAnywhere(s.shell, "Third-party licences");
    s.shell.frames(10);
    pressAnywhere(s.shell, "Copyright notices");
    s.shell.frames(10);
    CHECK(s.shell.application->depth() == 3);
    std::vector<std::string> all;
    texts(s.shell.application->root(), all);
    bool freetype = false;
    bool fribidi = false;
    for (const auto& text : all) {
        freetype = freetype || text.find("Portions of this software are copyright \xC2\xA9 2024 The FreeType Project") !=
                                   std::string::npos;
        fribidi = fribidi || text.find("Copyright (C) 1999-2000, 2017 Dov Grobgeld") != std::string::npos;
    }
    CHECK(freetype);
    CHECK(fribidi);
}

TEST_CASE("signing out from the settings forgets the token and shows the users") {
    SettingsShell s;
    pressAnywhere(s.shell, "Sign out");
    s.shell.frames(10);
    CHECK(s.shell.storedToken().empty());
    CHECK(shows(*s.shell.application, "demo"));
}

namespace {
struct HomeShell {
    test::FakeJellyfin server;
    Shell shell;
    HomeShell() {
        signIn(shell, server);
        shell.application->replace(app::makeHome(shell.session));
        shell.frames(10);
    }
};

bool focusedIsTab(ui::Application& app, const std::string& text) {
    ui::View* focused = app.focus().focused();
    if (focused == nullptr) {
        return false;
    }
    std::vector<std::string> labels;
    texts(*focused, labels);
    return dynamic_cast<app::NavTab*>(focused) != nullptr && !labels.empty() && labels.front() == text;
}
}  // namespace

TEST_CASE("the home screen has the navigation bar and starts on its content") {
    HomeShell h;
    CHECK(shows(*h.shell.application, "Home"));
    CHECK(shows(*h.shell.application, "Search"));
    CHECK(shows(*h.shell.application, "Settings"));
    CHECK(dynamic_cast<app::ItemCard*>(h.shell.application->focus().focused()) != nullptr);
    h.shell.press(Action::Up);
    CHECK(focusedIsTab(*h.shell.application, "Home"));
    h.shell.press(Action::Down);
    CHECK(dynamic_cast<app::ItemCard*>(h.shell.application->focus().focused()) != nullptr);
}

TEST_CASE("a tab replaces the screen, and back on a tab screen does nothing") {
    HomeShell h;
    h.shell.press(Action::Up);
    h.shell.press(Action::Right);
    REQUIRE(focusedIsTab(*h.shell.application, "Search"));
    h.shell.press(Action::Confirm);
    h.shell.frames(5);
    CHECK(h.shell.application->depth() == 1);
    CHECK(shows(*h.shell.application, "Recent searches") == false);  // none yet
    CHECK(dynamic_cast<ui::TextField*>(h.shell.application->focus().focused()) != nullptr);  // the field first
    ui::Screen* search = h.shell.application->top();
    h.shell.press(Action::Back);
    CHECK(h.shell.application->depth() == 1);
    CHECK(h.shell.application->top() == search);
    h.shell.press(Action::Up);
    h.shell.press(Action::Right);
    REQUIRE(focusedIsTab(*h.shell.application, "Settings"));
    h.shell.press(Action::Confirm);
    h.shell.frames(10);
    CHECK(shows(*h.shell.application, "Accent colour"));
    CHECK(dynamic_cast<ui::ValueRow*>(h.shell.application->focus().focused()) != nullptr);
    h.shell.press(Action::Up);
    h.shell.press(Action::Left);
    h.shell.press(Action::Left);
    REQUIRE(focusedIsTab(*h.shell.application, "Home"));
    h.shell.press(Action::Confirm);
    h.shell.frames(10);
    CHECK(dynamic_cast<app::ItemCard*>(h.shell.application->focus().focused()) != nullptr);
}

TEST_CASE("coming back to the home screen refreshes Resume without moving the focus") {
    HomeShell h;
    CHECK(h.server.count("/UserItems/Resume") == 1);
    h.shell.press(Action::Right);  // the second card of the first row
    ui::View* focused = h.shell.application->focus().focused();
    h.shell.application->push(app::makeDetails(h.shell.session, bigBuckBunny()));
    h.shell.frames(10);
    h.shell.press(Action::Back);
    h.shell.frames(10);
    CHECK(h.server.count("/UserItems/Resume") == 2);
    CHECK(h.server.count("/Shows/NextUp") == 2);
    CHECK(h.shell.application->focus().focused() == focused);
}

TEST_CASE("home rows arrive one after the other") {
    HomeShell h;  // transitions are on in unit tests; the rows were revealed when loaded
    h.shell.application->replace(app::makeHome(h.shell.session));
    h.shell.frames(2);  // load ran; the rows started their arrival
    bool arriving = false;
    std::function<void(const ui::View&)> scan = [&](const ui::View& view) {
        arriving = arriving || (view.opacity < 1 && view.shiftY > 0);
        for (const auto& child : view.children()) {
            scan(*child);
        }
    };
    scan(*h.shell.application->top());
    CHECK(arriving);
}

TEST_CASE("profiles show the users' pictures or their initial, then another user and Quick Connect") {
    test::FakeJellyfin server;
    Shell shell;
    shell.session.useServer("f1e2d3c4b5a6", server.baseUrl());
    shell.application->push(app::makeUsers(shell.session));
    shell.frames(10);
    std::vector<std::string> urls;
    imageUrls(shell.application->root(), urls);
    bool demoPicture = false;
    for (const auto& url : urls) {
        demoPicture = demoPicture || url.find("/Users/u-demo/Images/Primary?tag=img-demo") != std::string::npos;
    }
    CHECK(demoPicture);
    CHECK(shows(*shell.application, "demo"));
    CHECK(shows(*shell.application, "kids"));
    CHECK(shows(*shell.application, "K"));  // no picture: the initial
    CHECK(shows(*shell.application, "Another user"));
    CHECK(shows(*shell.application, "Quick Connect"));
    CHECK(dynamic_cast<app::ProfileCard*>(shell.application->focus().focused()) != nullptr);
    ui::View* quick = nullptr;
    std::function<void(ui::View&)> find = [&](ui::View& view) {
        if (auto* card = dynamic_cast<app::ProfileCard*>(&view)) {
            std::vector<std::string> labels;
            texts(*card, labels);
            if (!labels.empty() && labels.back() == "Quick Connect") {
                quick = card;
            }
        }
        for (const auto& child : view.children()) {
            find(*child);
        }
    };
    find(*shell.application->top());
    REQUIRE(quick != nullptr);
    REQUIRE(shell.application->focus().focus(quick));
    server.approveQuickConnect();  // the screen polls until the code is approved (inline in tests)
    shell.press(Action::Confirm);
    shell.frames(10);
    CHECK(shell.storedToken() == "tok-demo");  // signed in through Quick Connect
}

TEST_CASE("after coming back to the home screen, up still reaches the navigation bar") {
    HomeShell h;
    h.shell.application->push(app::makeDetails(h.shell.session, bigBuckBunny()));
    h.shell.frames(10);
    h.shell.press(Action::Back);
    h.shell.frames(10);
    REQUIRE(dynamic_cast<app::ItemCard*>(h.shell.application->focus().focused()) != nullptr);
    h.shell.press(Action::Up);
    CHECK(focusedIsTab(*h.shell.application, "Home"));
}

namespace {
std::vector<jellyfin::Item> albumTracks() {
    auto page = jellyfin::parseItemPage(test::readFixture("jellyfin/ts/album_tracks.json"));
    REQUIRE(page.ok());
    return page.value().items;
}

// updates the music player (as the frame loop does) until `done` or 5 s
template <typename Done>
bool pump(app::MusicPlayer& music, Done done) {
    for (int i = 0; i < 500; ++i) {
        music.update();
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool reported(const test::FakeJellyfin& server, const std::string& path, const std::string& itemId) {
    for (const auto& report : server.reports()) {
        if (report.path == path && report.body.find(R"("ItemId":")" + itemId + "\"") != std::string::npos) {
            return true;
        }
    }
    return false;
}

jellyfin::Item missingTrack(const std::string& id) {
    jellyfin::Item item;
    item.id = id;
    item.name = id;
    item.type = jellyfin::ItemType::Audio;
    return item;
}
}  // namespace

TEST_CASE("music plays an album from a track, reports it and skips to the next one") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    app::MusicPlayer& music = shell.session.music();
    CHECK_FALSE(music.active());
    const auto tracks = albumTracks();
    music.play(tracks, 2, false);
    CHECK(music.active());
    REQUIRE(music.status().track != nullptr);
    CHECK(music.status().track->name == "Minuit");
    CHECK(pump(music, [&] { return reported(server, "/Sessions/Playing", tracks[2].id); }));
    CHECK(server.count("/Audio/" + tracks[2].id + "/stream") >= 1);
    music.next();
    CHECK(pump(music, [&] { return reported(server, "/Sessions/Playing", tracks[3].id); }));
    CHECK(reported(server, "/Sessions/Playing/Stopped", tracks[2].id));
    CHECK(music.status().track->name == "Rétroviseur");
    music.stop();
    CHECK_FALSE(music.active());
    CHECK(reported(server, "/Sessions/Playing/Stopped", tracks[3].id));
}

TEST_CASE("a track that ends hands over to the next one") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    app::MusicPlayer& music = shell.session.music();
    const auto tracks = albumTracks();
    music.play({tracks[0], tracks[1]}, 0, false);
    CHECK(pump(music, [&] { return reported(server, "/Sessions/Playing", tracks[1].id); }));
    CHECK(music.status().track->name == "Autoroute");
    CHECK(pump(music, [&] { return !music.active(); }));  // the queue ends after the last track
    CHECK(reported(server, "/Sessions/Playing/Stopped", tracks[1].id));
}

TEST_CASE("tracks that cannot be played are skipped; a queue of them stops") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    app::MusicPlayer& music = shell.session.music();
    const auto tracks = albumTracks();
    music.play({missingTrack("gone-1"), tracks[0]}, 0, false);
    CHECK(pump(music, [&] { return reported(server, "/Sessions/Playing", tracks[0].id); }));
    music.stop();
    music.play({missingTrack("gone-1"), missingTrack("gone-2")}, 0, false);
    CHECK(pump(music, [&] { return !music.active(); }));
}

TEST_CASE("switching user stops the music before the session changes") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    app::MusicPlayer& music = shell.session.music();
    const auto tracks = albumTracks();
    music.play(tracks, 0, false);
    CHECK(pump(music, [&] { return reported(server, "/Sessions/Playing", tracks[0].id); }));
    shell.session.forgetToken();
    CHECK_FALSE(music.active());
    CHECK(reported(server, "/Sessions/Playing/Stopped", tracks[0].id));
}

namespace {
jellyfin::Item fixtureItem(const char* fixture, const std::string& name) {
    auto page = jellyfin::parseItemPage(test::readFixture(fixture));
    REQUIRE(page.ok());
    for (const auto& item : page.value().items) {
        if (item.name == name) {
            return item;
        }
    }
    FAIL("no item " << name);
    return {};
}

jellyfin::Item musicLibrary() {
    return fixtureItem("jellyfin/ts/views_media.json", "Musique");
}

// the view named `name` under the top screen (track rows are named after their track)
ui::View* named(ui::View& view, const std::string& name) {
    if (view.name == name && view.visible) {
        return &view;
    }
    for (const auto& child : view.children()) {
        if (ui::View* found = named(*child, name)) {
            return found;
        }
    }
    return nullptr;
}

// waits for the music to play (frames with time passing)
void untilPlaying(Shell& shell) {
    for (int i = 0; i < 300 && (!shell.session.music().active() || shell.session.music().status().loading); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        shell.frames(1);
    }
}
}  // namespace

TEST_CASE("a music library shows its albums, artists and playlists") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.application->push(std::make_unique<ui::Screen>());
    shell.application->push(app::makeMusicLibrary(shell.session, musicLibrary()));
    shell.frames(10);
    CHECK(shows(*shell.application, "Aurore synthétique"));
    CHECK(shows(*shell.application, "Les Ondes Calmes"));  // the album's artist under its cover
    pressButton(shell, "Artists");
    shell.frames(10);
    CHECK(shows(*shell.application, "Kilohertz"));
    CHECK(server.count("/Artists/AlbumArtists") >= 1);
    pressButton(shell, "Playlists");
    shell.frames(10);
    CHECK(shows(*shell.application, "Soirée tests"));
}

TEST_CASE("an album lists its tracks by disc and plays from the chosen one") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.application->push(std::make_unique<ui::Screen>());
    shell.application->push(app::makeMusicItem(shell.session, fixtureItem("jellyfin/ts/music_albums.json", "Circuit doré")));
    shell.frames(10);
    CHECK(shows(*shell.application, "Circuit doré"));
    CHECK(shows(*shell.application, "Néon"));
    CHECK(shows(*shell.application, "Disc 2"));
    CHECK(shows(*shell.application, "Rétroviseur"));
    ui::View* row = named(*shell.application->top(), "Minuit");
    REQUIRE(row != nullptr);
    REQUIRE(shell.application->focus().focus(row));
    shell.press(Action::Confirm);
    untilPlaying(shell);
    REQUIRE(shell.session.music().active());
    CHECK(shell.session.music().status().track->name == "Minuit");
    CHECK(shell.application->depth() == 3);  // Now playing on top
    CHECK(shows(*shell.application, "Minuit"));
    CHECK(shows(*shell.application, "Kilohertz"));
    CHECK(shows(*shell.application, "Up next"));
    CHECK(shows(*shell.application, "Rétroviseur"));
    shell.session.music().stop();
}

TEST_CASE("now playing skips, goes back, pauses and jumps in the queue") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.application->push(std::make_unique<ui::Screen>());
    shell.session.music().play(albumTracks(), 0, false);
    shell.application->push(app::makeNowPlaying(shell.session));
    untilPlaying(shell);
    pressButton(shell, "Next");
    untilPlaying(shell);
    CHECK(shell.session.music().status().track->name == "Autoroute");
    CHECK(shows(*shell.application, "Autoroute"));
    pressButton(shell, "Previous");
    untilPlaying(shell);
    CHECK(shell.session.music().status().track->name == "Néon");
    pressButton(shell, "Pause");
    shell.frames(5);
    CHECK(shell.session.music().status().paused);
    CHECK(findButton(*shell.application->top(), "Play") != nullptr);
    pressButton(shell, "Repeat: off");
    CHECK(shell.session.music().queue().repeat() == player::Repeat::All);
    CHECK(shows(*shell.application, "Repeat: all"));
    ui::View* upcoming = named(*shell.application->top(), "Rétroviseur");
    REQUIRE(upcoming != nullptr);
    REQUIRE(shell.application->focus().focus(upcoming));
    shell.press(Action::Confirm);
    untilPlaying(shell);
    CHECK(shell.session.music().status().track->name == "Rétroviseur");
    shell.press(Action::R1);
    untilPlaying(shell);
    CHECK(shell.session.music().status().track->name == "Chrome");
    pressButton(shell, "Stop");
    shell.frames(5);
    CHECK_FALSE(shell.session.music().active());
    CHECK(shell.application->depth() == 1);  // nothing left to show: back
}

TEST_CASE("the navigation bar shows the music playing and opens now playing") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.session.music().play(albumTracks(), 1, false);
    shell.application->replace(app::makeHome(shell.session));
    shell.frames(10);
    ui::View* chip = named(*shell.application->top(), "now playing");
    REQUIRE(chip != nullptr);
    CHECK(shows(*shell.application, "Autoroute"));
    REQUIRE(shell.application->focus().focus(chip));
    shell.press(Action::Confirm);
    shell.frames(5);
    CHECK(shell.application->depth() == 2);
    CHECK(shows(*shell.application, "Up next"));
    shell.session.music().stop();
    shell.press(Action::Back);
    shell.frames(5);
    CHECK(named(*shell.application->top(), "now playing") == nullptr);  // hidden once the music stopped
}

TEST_CASE("the chip that keeps the focus after the music ends does not open an empty screen") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    shell.session.music().play(albumTracks(), 1, false);
    shell.application->replace(app::makeHome(shell.session));
    shell.frames(10);
    ui::View* chip = named(*shell.application->top(), "now playing");
    REQUIRE(chip != nullptr);
    REQUIRE(shell.application->focus().focus(chip));
    shell.session.music().stop();
    shell.frames(5);
    shell.press(Action::Confirm);
    shell.frames(5);
    CHECK(shell.application->depth() == 1);
}

TEST_CASE("a collection opens as a library of its own items") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    jellyfin::Item collection;
    collection.id = "bs-1";
    collection.type = jellyfin::ItemType::BoxSet;
    collection.name = "Trilogie";
    app::openItem(shell.session, collection);
    shell.frames(5);
    bool listed = false;
    for (const auto& request : server.requests()) {
        listed = listed || (request.target.find("parentId=bs-1") != std::string::npos &&
                            request.target.find("recursive=false") != std::string::npos);
    }
    CHECK(listed);
}

TEST_CASE("a playlist of videos opens as a library, a playlist of music as music") {
    test::FakeJellyfin server;
    Shell shell;
    signIn(shell, server);
    jellyfin::Item playlist;
    playlist.id = "pl-video";
    playlist.type = jellyfin::ItemType::Playlist;
    playlist.mediaType = "Video";
    app::openItem(shell.session, playlist);
    shell.frames(5);
    bool listed = false;
    for (const auto& request : server.requests()) {
        listed = listed || request.target.find("parentId=pl-video") != std::string::npos;
    }
    CHECK(listed);
    CHECK_FALSE(shows(*shell.application, "Shuffle"));
}

TEST_CASE("starting a video stops the music first") {
    Playing p;
    p.shell.session.music().play(albumTracks(), 0, false);
    untilPlaying(p.shell);
    p.shell.application->push(app::makePlayer(p.shell.session, episode(), 0));
    p.frames(10);
    CHECK_FALSE(p.shell.session.music().active());
    int stopped = -1;
    int videoInfo = -1;
    const auto requests = p.server.requests();
    for (std::size_t i = 0; i < requests.size(); ++i) {
        if (requests[i].path == "/Sessions/Playing/Stopped" && stopped < 0) {
            stopped = static_cast<int>(i);
        }
        if (requests[i].path == "/Items/e-1/PlaybackInfo" && videoInfo < 0) {
            videoInfo = static_cast<int>(i);
        }
    }
    CHECK(stopped >= 0);
    CHECK(stopped < videoInfo);
    p.shell.press(Action::Back);
}

TEST_CASE("home shows music as square covers and no latest row for Live TV") {
    test::FakeJellyfin server;
    server.showMediaViews();
    Shell shell;
    signIn(shell, server);
    shell.application->replace(app::makeHome(shell.session));
    shell.frames(10);
    CHECK(shows(*shell.application, "Musique"));
    CHECK_FALSE(shows(*shell.application, "Latest in Live TV"));
    CHECK(shows(*shell.application, "Latest in Musique"));
    bool square = false;
    std::function<void(const ui::View&)> scan = [&](const ui::View& view) {
        if (const auto* card = dynamic_cast<const app::ItemCard*>(&view)) {
            const ui::Size size = app::cardSize(app::CardShape::Square);
            square = square || (card->frame.width == doctest::Approx(size.width) &&
                                card->frame.height == doctest::Approx(size.height));
        }
        for (const auto& child : view.children()) {
            scan(*child);
        }
    };
    scan(*shell.application->top());
    CHECK(square);
}

namespace {
// signed in on a fake server with Live TV; the guide's clock is 2026-10-16 21:45 in Paris (UTC+2)
struct TvShell {
    test::FakeJellyfin server;
    Shell shell;
    TvShell() {
        server.showMediaViews();
        signIn(shell, server);
        shell.session.fixedNow = jellyfin::parseUtc("2026-10-16T19:45:00Z");
        shell.session.fixedUtcOffset = 120;
        shell.application->push(std::make_unique<ui::Screen>());
    }
    void openLiveTv() {
        shell.application->push(app::makeLiveTv(shell.session, fixtureItem("jellyfin/ts/views_media.json", "Live TV")));
        shell.frames(10);
    }
    int requests(const std::string& method, const std::string& path) const { return requestsTo(server, method, path); }
};
}  // namespace

TEST_CASE("Live TV lists the channels with the programme on now") {
    TvShell tv;
    tv.openLiveTv();
    CHECK(shows(*tv.shell.application, "Playfin Info"));
    CHECK(shows(*tv.shell.application, "Playfin Sport"));
    CHECK(shows(*tv.shell.application, "21:30 \xE2\x80\x93 22:30 \xC2\xB7 Le point du jour"));
    CHECK(dynamic_cast<ui::Button*>(tv.shell.application->focus().focused()) == nullptr);  // a channel first
}

TEST_CASE("the guide lists a channel's programmes by day and records a later one") {
    TvShell tv;
    tv.openLiveTv();
    pressButton(tv.shell, "Guide");
    tv.shell.frames(10);
    CHECK(tv.server.count("/LiveTv/Programs") >= 1);
    CHECK(shows(*tv.shell.application, "Today"));
    CHECK(shows(*tv.shell.application, "Tomorrow"));
    CHECK(shows(*tv.shell.application, "Live"));  // the programme on now
    ui::View* later = named(*tv.shell.application->top(), "Le journal");
    REQUIRE(later != nullptr);
    REQUIRE(tv.shell.application->focus().focus(later));
    tv.shell.press(Action::Confirm);
    REQUIRE(tv.shell.application->hasModal());
    CHECK(shows(*tv.shell.application, "Record"));
    pressAnywhere(tv.shell, "Record");
    CHECK(tv.server.timerBody().find(R"("ProgramId")") != std::string::npos);
    CHECK_FALSE(tv.shell.application->hasModal());
}

TEST_CASE("the guide's channels scroll, however many there are") {
    TvShell tv;
    tv.openLiveTv();
    pressButton(tv.shell, "Guide");
    tv.shell.frames(10);
    ui::View* row = named(*tv.shell.application->top(), "channel Playfin Info");
    REQUIRE(row != nullptr);
    bool scrolled = false;
    for (ui::View* view = row->parent(); view != nullptr; view = view->parent()) {
        scrolled = scrolled || dynamic_cast<ui::VScroll*>(view) != nullptr;
    }
    CHECK(scrolled);
}

TEST_CASE("recordings list the scheduled ones, which can be cancelled") {
    TvShell tv;
    tv.openLiveTv();
    pressButton(tv.shell, "Recordings");
    tv.shell.frames(10);
    CHECK(shows(*tv.shell.application, "No recordings yet."));
    CHECK(shows(*tv.shell.application, "Scheduled"));
    ui::View* timer = named(*tv.shell.application->top(), "Reportage");
    REQUIRE(timer != nullptr);
    REQUIRE(tv.shell.application->focus().focus(timer));
    tv.shell.press(Action::Confirm);
    pressAnywhere(tv.shell, "Cancel the recording");
    CHECK(tv.requests("DELETE", "/LiveTv/Timers/0b4eabd5708f3dc71f2c03c524916899") == 1);
}

TEST_CASE("home shows what is on TV now when the server has Live TV") {
    TvShell tv;
    tv.shell.application->replace(app::makeHome(tv.shell.session));
    tv.shell.frames(10);
    CHECK(shows(*tv.shell.application, "On TV now"));
    CHECK(shows(*tv.shell.application, "Tennis"));
    CHECK(tv.server.count("/LiveTv/Programs/Recommended") == 1);
}

namespace {
jellyfin::Item channel(const std::string& name) { return fixtureItem("jellyfin/ts/livetv_channels.json", name); }

const char* const kLiveStreamId =
    "e2329f4997b378e64ccf8fa396deb76e_af999c25a00715699361240d4c6c7a53_5d3aff9566faa64d7ae0e237d2d7499c";
}  // namespace

TEST_CASE("a channel plays live through the server, cannot be sought and closes its live stream once") {
    Playing p;
    p.server.showMediaViews();
    p.shell.session.fixedNow = jellyfin::parseUtc("2026-10-16T19:45:00Z");
    p.shell.session.fixedUtcOffset = 120;
    p.shell.application->push(app::makePlayer(p.shell.session, channel("Playfin Info"), 0));
    p.frames(10);
    CHECK(p.server.playbackInfoBody().find(R"("AutoOpenLiveStream":true)") != std::string::npos);
    REQUIRE(p.shell.application->videoSource() != nullptr);
    CHECK(shows(*p.shell.application, "Playfin Info \xC2\xB7 Le point du jour"));
    CHECK(shows(*p.shell.application, "21:30 \xE2\x80\x93 22:30"));
    p.shell.press(Action::Right);  // no seeking in a live channel: the bar keeps the programme's times
    p.frames(5);
    CHECK(shows(*p.shell.application, "21:30 \xE2\x80\x93 22:30"));
    p.shell.press(Action::Back);
    p.frames(5);
    CHECK(p.shell.application->depth() == 1);
    CHECK(requestsTo(p.server, "POST", "/LiveStreams/Close") == 1);
    CHECK(p.server.last().target.find(kLiveStreamId) != std::string::npos);
}

TEST_CASE("R1 and L1 change channel in the live player") {
    Playing p;
    p.server.showMediaViews();
    p.shell.application->push(app::makePlayer(p.shell.session, channel("Playfin Info"), 0));
    p.frames(10);
    p.shell.press(Action::R1);
    p.frames(10);
    CHECK(p.shell.application->depth() == 2);
    CHECK(shows(*p.shell.application, "Playfin Sport \xC2\xB7 Tennis"));
    CHECK(requestsTo(p.server, "POST", "/LiveStreams/Close") == 1);  // the first channel's stream
    CHECK(p.server.count("/Items/" + channel("Playfin Sport").id + "/PlaybackInfo") == 1);
    p.shell.press(Action::L1);
    p.frames(10);
    CHECK(shows(*p.shell.application, "Playfin Info \xC2\xB7 Le point du jour"));
    p.shell.press(Action::L1);  // wraps to the last channel
    p.frames(10);
    CHECK(shows(*p.shell.application, "Playfin Cin\xC3\xA9ma \xC2\xB7 Court m\xC3\xA9trage"));
    p.shell.press(Action::Back);
}

TEST_CASE("a programme on now opens its channel with the channel's name") {
    Playing p;
    p.server.showMediaViews();
    const jellyfin::Item tennis = fixtureItem("jellyfin/ts/livetv_airing.json", "Tennis");
    app::openItem(p.shell.session, tennis);
    p.frames(10);
    CHECK(shows(*p.shell.application, "Playfin Sport \xC2\xB7 Tennis"));
    p.shell.press(Action::Back);
}

TEST_CASE("profiles also offer the users saved on this console that the server does not list") {
    test::FakeJellyfin server;
    Shell shell;
    shell.remember(server.baseUrl(), "tok-demo");
    auto& stored = jellyfin::upsertServer(shell.session.data(), "f1e2d3c4b5a6");
    auto& hidden = jellyfin::upsertUser(stored, "u-hidden");
    hidden.name = "hidden user";
    hidden.token = "tok-demo";
    REQUIRE(shell.session.save());
    shell.session.useServer("f1e2d3c4b5a6", server.baseUrl());
    shell.application->push(app::makeUsers(shell.session));
    shell.frames(10);
    CHECK(shows(*shell.application, "hidden user"));
    CHECK(shows(*shell.application, "demo"));
    int listed = 0;
    std::function<void(ui::View&)> count = [&](ui::View& view) {
        if (dynamic_cast<app::ProfileCard*>(&view) != nullptr) {
            std::vector<std::string> labels;
            texts(view, labels);
            listed += !labels.empty() && labels.back() == "demo" ? 1 : 0;
        }
        for (const auto& child : view.children()) {
            count(*child);
        }
    };
    count(*shell.application->top());
    CHECK(listed == 1);  // a saved user the server lists is shown once
    ui::View* card = nullptr;
    std::function<void(ui::View&)> find = [&](ui::View& view) {
        if (dynamic_cast<app::ProfileCard*>(&view) != nullptr) {
            std::vector<std::string> labels;
            texts(view, labels);
            if (!labels.empty() && labels.back() == "hidden user") {
                card = &view;
            }
        }
        for (const auto& child : view.children()) {
            find(*child);
        }
    };
    find(*shell.application->top());
    REQUIRE(card != nullptr);
    REQUIRE(shell.application->focus().focus(card));
    shell.press(Action::Confirm);
    shell.frames(10);
    CHECK(shell.session.data().lastUserId == "u-hidden");
    CHECK(shell.application->depth() == 1);  // signed in with the saved token: home
}
