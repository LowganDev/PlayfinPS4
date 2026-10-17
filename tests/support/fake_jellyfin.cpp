#include "support/fake_jellyfin.hpp"

#include <stb/stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <set>
#include <vector>

#include "jellyfin/json.hpp"
#include "support/files.hpp"

namespace playfin::test {

namespace {

using Handler = std::function<void(const httplib::Request&, httplib::Response&)>;

bool authorized(const httplib::Request& req) {
    return req.get_header_value("Authorization").find("Token=\"tok-demo\"") != std::string::npos;
}

Handler guarded(Handler handler) {
    return [handler](const httplib::Request& req, httplib::Response& res) {
        if (!authorized(req)) {
            res.status = 401;
            return;
        }
        handler(req, res);
    };
}

Handler fixture(const std::string& name) {
    return [name](const httplib::Request&, httplib::Response& res) {
        res.set_content(readFixture("jellyfin/" + name), "application/json");
    };
}

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string movieName(int index) {
    char name[32];
    std::snprintf(name, sizeof(name), "Movie %03d", index);
    return name;
}

std::set<std::string> idsIn(std::initializer_list<const char*> fixtures) {
    std::set<std::string> out;
    for (const char* name : fixtures) {
        const auto doc = jellyfin::json::Document::parse(readFixture(name));
        yyjson_val* items = doc ? jellyfin::json::array(doc->root(), "Items") : nullptr;
        yyjson_arr_iter iter = yyjson_arr_iter_with(items);
        while (yyjson_val* item = items != nullptr ? yyjson_arr_iter_next(&iter) : nullptr) {
            out.insert(jellyfin::json::string(item, "Id"));
        }
    }
    return out;
}

// the ids of the recorded albums (their tracks are the recorded album's)
const std::set<std::string>& albumIds() {
    static const std::set<std::string> ids = idsIn({"jellyfin/ts/music_albums.json", "jellyfin/ts/artist_albums.json"});
    return ids;
}

// the ids of the recorded music tracks (their PlaybackInfo is the recorded audio one)
const std::set<std::string>& audioIds() {
    static const std::set<std::string> ids = idsIn({"jellyfin/ts/album_tracks.json", "jellyfin/ts/playlist_items.json"});
    return ids;
}

// GET /Items: generated movies, paged, filtered by searchTerm and filters, reversed when descending
void items(const httplib::Request& req, httplib::Response& res) {
    const int start = req.has_param("startIndex") ? std::stoi(req.get_param_value("startIndex")) : 0;
    const int limit = req.has_param("limit") ? std::stoi(req.get_param_value("limit")) : 100;
    const std::string term = lower(req.get_param_value("searchTerm"));
    const std::string types = req.get_param_value("includeItemTypes");
    // music (answers recorded on the test server): albums, an artist's albums, playlists, an album's tracks
    const std::string parentId = req.get_param_value("parentId");
    if (types.find("MusicAlbum") != std::string::npos) {
        const char* name = req.has_param("albumArtistIds") ? "ts/artist_albums.json" : "ts/music_albums.json";
        res.set_content(readFixture(std::string("jellyfin/") + name), "application/json");
        return;
    }
    if (types.find("Playlist") != std::string::npos) {
        res.set_content(readFixture("jellyfin/ts/music_playlists.json"), "application/json");
        return;
    }
    if (types == "Audio" || albumIds().count(parentId) != 0) {
        res.set_content(readFixture("jellyfin/ts/album_tracks.json"), "application/json");
        return;
    }
    if (!types.empty() && types.find("Movie") == std::string::npos) {
        // series s-1 "Test Show" and its episode e-1 "Pilot", for searches
        const bool series = types.find("Series") != std::string::npos;
        const std::string name = series ? "test show" : "pilot";
        const bool match = term.empty() || name.find(term) != std::string::npos;
        const std::string item =
            series ? R"({"Name":"Test Show","Id":"s-1","Type":"Series"})"
                   : R"({"Name":"Pilot","Id":"e-1","Type":"Episode","SeriesName":"Test Show","SeriesId":"s-1","SeasonId":"se-1","IndexNumber":1,"ParentIndexNumber":1})";
        res.set_content(std::string(R"({"Items":[)") + (match ? item : "") + R"(],"TotalRecordCount":)" +
                            (match ? "1" : "0") + R"(,"StartIndex":0})",
                        "application/json");
        return;
    }
    const std::string filters = req.get_param_value("filters");
    std::vector<int> matches;
    for (int i = 0; i < FakeJellyfin::kMovieCount; ++i) {
        // every 10th movie is a favourite; the odd ones are unwatched
        if ((filters.find("IsFavorite") != std::string::npos && i % 10 != 0) ||
            (filters.find("IsUnplayed") != std::string::npos && i % 2 == 0)) {
            continue;
        }
        if (term.empty() || lower(movieName(i)).find(term) != std::string::npos) {
            matches.push_back(i);
        }
    }
    if (req.get_param_value("sortOrder") == "Descending") {
        std::reverse(matches.begin(), matches.end());
    }
    std::string body = R"({"Items":[)";
    for (int i = start; i < static_cast<int>(matches.size()) && i < start + limit; ++i) {
        char entry[160];
        std::snprintf(entry, sizeof(entry), R"(%s{"Name":"%s","Id":"mv-%03d","Type":"Movie","ImageTags":{"Primary":"t-%03d"}})",
                      i == start ? "" : ",", movieName(matches[static_cast<std::size_t>(i)]).c_str(),
                      matches[static_cast<std::size_t>(i)], matches[static_cast<std::size_t>(i)]);
        body += entry;
    }
    body += R"(],"TotalRecordCount":)" + std::to_string(matches.size()) + R"(,"StartIndex":)" + std::to_string(start) +
            "}";
    res.set_content(body, "application/json");
}

void writeToString(void* context, void* data, int size) {
    static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<std::size_t>(size));
}

// a 16x24 JPEG poster
std::string posterJpeg() {
    std::vector<unsigned char> pixels(16 * 24 * 3);
    for (std::size_t i = 0; i < pixels.size(); i += 3) {
        pixels[i] = 0x40;
        pixels[i + 1] = 0x80;
        pixels[i + 2] = 0xC0;
    }
    std::string out;
    stbi_write_jpg_to_func(writeToString, &out, 16, 24, 3, pixels.data(), 90);
    return out;
}

}  // namespace

FakeJellyfin::FakeJellyfin(TlsMode mode, std::string version, int port)
    : server_(mode), version_(std::move(version)) {
    httplib::Server& http = server_.http();
    http.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response&) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_ = Seen{req.method, req.path, req.target, req.get_header_value("Authorization"), req.body};
        requests_.push_back(last_);
        return httplib::Server::HandlerResponse::Unhandled;
    });
    http.Get("/System/Info/Public", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"LocalAddress":")" + server_.baseUrl() + R"(","ServerName":"Test Server","Version":")" +
                            version_ + R"(","ProductName":"Jellyfin Server","Id":"f1e2d3c4b5a6","StartupWizardCompleted":true})",
                        "application/json");
    });
    http.Get("/Users/Public", fixture("users_public.json"));
    http.Post("/Users/AuthenticateByName", [this](const httplib::Request& req, httplib::Response& res) {
        {
            // the pre-routing handler runs before httplib reads the body
            std::lock_guard<std::mutex> lock(mutex_);
            last_.body = req.body;
        }
        const auto doc = jellyfin::json::Document::parse(req.body);
        if (doc && jellyfin::json::string(doc->root(), "Username") == "demo" &&
            jellyfin::json::string(doc->root(), "Pw") == "secret") {
            res.set_content(readFixture("jellyfin/auth.json"), "application/json");
        } else {
            res.status = 401;
        }
    });
    http.Get("/QuickConnect/Enabled", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("true", "application/json");
    });
    http.Post("/QuickConnect/Initiate", fixture("quickconnect.json"));
    http.Get("/QuickConnect/Connect", [this](const httplib::Request& req, httplib::Response& res) {
        if (req.get_param_value("secret") != "qc-secret") {
            res.status = 404;
            return;
        }
        res.set_content(std::string(R"({"Authenticated":)") + (approved_ ? "true" : "false") +
                            R"(,"Secret":"qc-secret","Code":"123456"})",
                        "application/json");
    });
    http.Post("/Users/AuthenticateWithQuickConnect", [this](const httplib::Request& req, httplib::Response& res) {
        const auto doc = jellyfin::json::Document::parse(req.body);
        if (approved_ && doc && jellyfin::json::string(doc->root(), "Secret") == "qc-secret") {
            res.set_content(readFixture("jellyfin/auth.json"), "application/json");
        } else {
            res.status = 401;
        }
    });
    http.Get("/Users/Me", guarded(fixture("user_me.json")));
    http.Post("/Sessions/Logout", guarded([](const httplib::Request&, httplib::Response& res) { res.status = 204; }));
    http.Get("/UserViews", guarded([this](const httplib::Request&, httplib::Response& res) {
        res.set_content(readFixture(mediaViews_ ? "jellyfin/ts/views_media.json" : "jellyfin/views.json"),
                        "application/json");
    }));
    http.Get("/Items/Latest", guarded([this](const httplib::Request&, httplib::Response& res) {
        if (!mediaViews_) {
            res.set_content(readFixture("jellyfin/latest.json"), "application/json");
            return;
        }
        // Latest answers an array: the recorded albums
        const auto doc = jellyfin::json::Document::parse(readFixture("jellyfin/ts/music_albums.json"));
        yyjson_val* items = jellyfin::json::array(doc->root(), "Items");
        char* text = yyjson_val_write(items, 0, nullptr);
        res.set_content(text != nullptr ? std::string(text) : std::string("[]"), "application/json");
        std::free(text);
    }));
    // with the test server's views, nothing was watched yet (as on the test server)
    const auto resume = [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(mediaViews_ ? std::string(R"({"Items":[],"TotalRecordCount":0,"StartIndex":0})")
                                    : readFixture("jellyfin/resume.json"),
                        "application/json");
    };
    http.Get("/UserItems/Resume", guarded(resume));
    http.Get("/Shows/NextUp", guarded(resume));
    http.Get("/Items", guarded(items));
    // playback: item m-transcode needs a transcode, the others play directly; e-1 has an intro and an outro
    http.Post(R"(/Items/([^/]+)/PlaybackInfo)", guarded([this](const httplib::Request& req, httplib::Response& res) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_.body = req.body;  // the pre-routing hook runs before the body is read
            playbackInfoBody_ = req.body;
        }
        if (std::string(req.matches[1]).rfind("gone-", 0) == 0) {  // items that no longer exist
            res.status = 404;
            return;
        }
        std::string name =
            req.matches[1] == "m-transcode" ? "playbackinfo_transcode.json" : "playbackinfo_direct.json";
        if (req.body.find(R"("AutoOpenLiveStream":true)") != std::string::npos) {
            name = "ts/playbackinfo_live.json";
        } else if (audioIds().count(req.matches[1]) != 0) {
            name = "ts/playbackinfo_audio.json";
        }
        res.set_content(readFixture("jellyfin/" + name), "application/json");
    }));
    http.Get(R"(/MediaSegments/([^/]+))", guarded([](const httplib::Request& req, httplib::Response& res) {
        if (req.matches[1] != "e-1") {
            res.status = 404;
            return;
        }
        res.set_content(readFixture("jellyfin/segments.json"), "application/json");
    }));
    const auto report = [this](const httplib::Request& req, httplib::Response& res) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            reports_.push_back(Seen{req.method, req.path, req.target, req.get_header_value("Authorization"), req.body});
        }
        res.status = 204;
    };
    http.Post("/Sessions/Playing", guarded(report));
    http.Post("/Sessions/Playing/Progress", guarded(report));
    http.Post("/Sessions/Playing/Stopped", guarded(report));
    http.Get(R"(/Videos/([^/]+)/subtitle.vtt)", guarded([](const httplib::Request&, httplib::Response& res) {
        res.set_content(readFixture("media/one.vtt"), "text/vtt");
    }));
    // every music track streams the test tone
    http.Get(R"(/Audio/([^/]+)/stream)", guarded([](const httplib::Request&, httplib::Response& res) {
        static const std::string tone = readFixture("media/tone.mp3");
        res.set_content(tone, "audio/mpeg");
    }));
    // the relayed live channel (TranscodingUrl of the recorded live PlaybackInfo): the test clip
    http.Get(R"(/videos/([^/]+)/master.m3u8)", guarded([](const httplib::Request&, httplib::Response& res) {
        static const std::string clip = readFixture("media/clip.mp4");
        res.set_content(clip, "video/mp4");
    }));
    // every item streams the test clip (httplib answers Range requests from the content)
    http.Get(R"(/Videos/([^/]+)/stream)", guarded([](const httplib::Request&, httplib::Response& res) {
        static const std::string clip = readFixture("media/clip.mp4");
        res.set_content(clip, "video/mp4");
    }));
    http.Get(R"(/Users/([^/]+)/Images/Primary)", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(posterJpeg(), "image/jpeg");
    });
    http.Get(R"(/Items/([^/]+)/Images/([A-Za-z]+))", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(posterJpeg(), "image/jpeg");
    });
    http.Get(R"(/Items/([^/]+))", guarded([](const httplib::Request& req, httplib::Response& res) {
        const std::string id = req.matches[1];
        if (id == "m-1") {
            res.set_content(readFixture("jellyfin/item_movie.json"), "application/json");
        } else if (id == "e-1") {
            res.set_content(readFixture("jellyfin/item_episode.json"), "application/json");
        } else if (id == "s-1") {
            res.set_content(readFixture("jellyfin/item_series.json"), "application/json");
        } else {
            res.status = 404;
        }
    }));
    http.Get(R"(/Shows/s-1/Seasons)", guarded(fixture("seasons.json")));
    // music and Live TV: answers recorded on the test server (scripts/test-server)
    http.Get("/Artists/AlbumArtists", guarded(fixture("ts/music_artists.json")));
    http.Get(R"(/Playlists/([^/]+)/Items)", guarded(fixture("ts/playlist_items.json")));
    http.Get("/LiveTv/Channels", guarded(fixture("ts/livetv_channels.json")));
    http.Get("/LiveTv/Programs", guarded(fixture("ts/livetv_programs.json")));
    http.Get("/LiveTv/Programs/Recommended", guarded(fixture("ts/livetv_airing.json")));
    http.Get("/LiveTv/Recordings", guarded(fixture("ts/livetv_recordings.json")));
    http.Get("/LiveTv/Timers/Defaults", guarded(fixture("ts/timer_defaults.json")));
    http.Get("/LiveTv/Timers", guarded(fixture("ts/livetv_timers.json")));
    http.Post("/LiveTv/Timers", guarded([this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex_);
        timerBody_ = req.body;
        res.status = 204;
    }));
    http.Delete(R"(/LiveTv/Timers/([^/]+))", guarded([](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    }));
    http.Post("/LiveStreams/Close", guarded([](const httplib::Request&, httplib::Response& res) { res.status = 204; }));
    // season se-2 has its own episode; any other season (or none) lists e-1 and e-2
    http.Get(R"(/Shows/s-1/Episodes)", guarded([](const httplib::Request& req, httplib::Response& res) {
        const bool second = req.get_param_value("seasonId") == "se-2";
        res.set_content(readFixture(second ? "jellyfin/episodes_se2.json" : "jellyfin/episodes.json"), "application/json");
    }));
    const Handler userData = [](const httplib::Request& req, httplib::Response& res) {
        res.set_content(std::string(R"({"IsFavorite":)") + (req.method == "POST" ? "true" : "false") + "}",
                        "application/json");
    };
    http.Post(R"(/UserFavoriteItems/([^/]+))", guarded(userData));
    http.Delete(R"(/UserFavoriteItems/([^/]+))", guarded(userData));
    http.Post(R"(/UserPlayedItems/([^/]+))", guarded(userData));
    http.Delete(R"(/UserPlayedItems/([^/]+))", guarded(userData));
    server_.start(port);
}

std::vector<FakeJellyfin::Seen> FakeJellyfin::reports() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reports_;
}

std::vector<FakeJellyfin::Seen> FakeJellyfin::requests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_;
}

int FakeJellyfin::count(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    int n = 0;
    for (const Seen& seen : requests_) {
        n += seen.path == path ? 1 : 0;
    }
    return n;
}

std::string FakeJellyfin::timerBody() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return timerBody_;
}

std::string FakeJellyfin::playbackInfoBody() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playbackInfoBody_;
}

FakeJellyfin::Seen FakeJellyfin::last() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_;
}

}  // namespace playfin::test
