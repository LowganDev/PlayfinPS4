#include <doctest/doctest.h>

#include "app/shell/session.hpp"
#include "app/shell/tv_guide.hpp"
#include "net/http.hpp"

using namespace playfin;

namespace {

jellyfin::Guide sample() {
    jellyfin::Guide guide;
    guide.channels = {{"tf1.fr", {"TF1 HD", "1"}}, {"arte.fr", {"Arte"}}, {"m6.fr", {"6"}}};
    guide.programs = {
        {"arte.fr", 1000, 2000, "Before", ""},
        {"arte.fr", 2000, 3000, "Now on Arte", "A documentary."},
        {"m6.fr", 1500, 2500, "On M6", ""},
        {"tf1.fr", 2000, 4000, "On TF1", ""},
    };
    return guide;
}

jellyfin::Item channel(const char* id, const char* name, const char* number) {
    jellyfin::Item item;
    item.id = id;
    item.name = name;
    item.channelNumber = number;
    item.type = jellyfin::ItemType::TvChannel;
    return item;
}

}  // namespace

TEST_CASE("TV guide: a server channel finds its guide channel by name, else by number") {
    app::AppSession session(net::HttpConfig{}, "");
    app::TvGuide guide(session);
    guide.use(sample());
    CHECK(guide.state() == app::TvGuide::State::Ready);
    auto now = guide.current(channel("c1", "arte", "7"), 2500);
    REQUIRE(static_cast<bool>(now));
    CHECK(now->name == "Now on Arte");
    CHECK(now->overview == "A documentary.");
    CHECK(now->channelId == "c1");
    CHECK(now->type == jellyfin::ItemType::Program);
    CHECK(now->id.empty());  // not a server programme: nothing to record
    now = guide.current(channel("c2", "TF1", "1"), 2500);  // "TF1 HD" in the guide
    REQUIRE(static_cast<bool>(now));
    CHECK(now->name == "On TF1");
    now = guide.current(channel("c3", "M6 Boutique", "6"), 2000);  // the name differs, the number matches
    REQUIRE(static_cast<bool>(now));
    CHECK(now->name == "On M6");
    CHECK_FALSE(static_cast<bool>(guide.current(channel("c4", "Unknown", "99"), 2500)));
    const auto list = guide.programs(channel("c1", "Arte", ""), 1500, 10000);
    REQUIRE(list.size() == 2);
    CHECK(list[0].name == "Before");
    CHECK(list[1].startUtc == 2000);
}

TEST_CASE("TV guide: it fills only the channels without a programme from the server") {
    app::AppSession session(net::HttpConfig{}, "");
    app::TvGuide guide(session);
    guide.use(sample());
    std::vector<jellyfin::Item> channels = {channel("c1", "Arte", ""), channel("c2", "TF1", "")};
    jellyfin::Item fromServer;
    fromServer.name = "The server's";
    channels[1].currentProgram = std::make_shared<const jellyfin::Item>(fromServer);
    guide.fill(channels, 2500);
    REQUIRE(static_cast<bool>(channels[0].currentProgram));
    CHECK(channels[0].currentProgram->name == "Now on Arte");
    CHECK(channels[1].currentProgram->name == "The server's");
}

TEST_CASE("TV guide: without an address it is off and asks nothing") {
    app::AppSession session(net::HttpConfig{}, "");
    app::TvGuide guide(session);
    guide.refresh();
    CHECK(guide.state() == app::TvGuide::State::Off);
    CHECK_FALSE(static_cast<bool>(guide.current(channel("c1", "Arte", ""), 2500)));
}
