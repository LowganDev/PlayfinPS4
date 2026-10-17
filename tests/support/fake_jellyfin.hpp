#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "support/fake_server.hpp"

namespace playfin::test {

// A small Jellyfin 10.10 server: user "demo" / "secret" (token "tok-demo"), libraries "lib-movies" (250 generated
// movies, paged) and "lib-shows" (series "s-1" with seasons "se-1", "se-2"), Quick Connect code 123456.
class FakeJellyfin {
public:
    static constexpr int kMovieCount = 250;

    explicit FakeJellyfin(TlsMode mode = TlsMode::None, std::string version = "10.10.7", int port = 0);

    std::string baseUrl() const { return server_.baseUrl(); }
    void approveQuickConnect() { approved_ = true; }
    // The user views of the test server (Live TV, music, playlists, recordings) instead of movies and shows.
    void showMediaViews() { mediaViews_ = true; }

    struct Seen {
        std::string method;
        std::string path;
        std::string target;  // path + raw query
        std::string authorization;
        std::string body;
    };
    Seen last() const;
    // POSTs to /Sessions/Playing, /Sessions/Playing/Progress and /Sessions/Playing/Stopped, in order
    std::vector<Seen> reports() const;
    // every request, in order (bodies only for PlaybackInfo and the reports)
    std::vector<Seen> requests() const;
    // how many requests had this path
    int count(const std::string& path) const;
    std::string playbackInfoBody() const;
    std::string timerBody() const;  // the last POST /LiveTv/Timers

private:
    FakeServer server_;
    std::string version_;
    std::atomic<bool> approved_{false};
    std::atomic<bool> mediaViews_{false};
    mutable std::mutex mutex_;
    Seen last_;
    std::vector<Seen> reports_;
    std::vector<Seen> requests_;
    std::string playbackInfoBody_;
    std::string timerBody_;
};

}  // namespace playfin::test
