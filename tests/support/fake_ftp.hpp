#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace playfin::test {

// A small FTP server on 127.0.0.1 serving a folder as "/", with the commands GoldHEN's server answers and Playfin
// uses (PASV only, LIST in the Unix style with "." and "..", SIZE, REST, RETR, STOR, APPE, MKD, DELE, RNFR/RNTO).
class FakeFtp {
public:
    explicit FakeFtp(std::string root);
    ~FakeFtp();
    FakeFtp(const FakeFtp&) = delete;
    FakeFtp& operator=(const FakeFtp&) = delete;

    std::string base() const;  // "ftp://127.0.0.1:<port>"
    const std::string& root() const { return root_; }

private:
    void acceptLoop();
    void serve(int control);
    std::string root_;
    int listener_ = -1;
    int port_ = 0;
    std::atomic<bool> stopping_{false};
    std::thread acceptor_;
    std::mutex mutex_;
    std::vector<std::thread> sessions_;
    std::vector<int> sockets_;
};

}  // namespace playfin::test
