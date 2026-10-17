#include <doctest/doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "jellyfin/discovery.hpp"

using namespace std::chrono_literals;
using playfin::jellyfin::DiscoveryConfig;
using playfin::jellyfin::parseDiscoveryReply;

namespace {

// UDP responder on 127.0.0.1 answering the discovery message with `replies`
class Responder {
public:
    explicit Responder(std::vector<std::string> replies) : replies_(std::move(replies)) {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        socklen_t length = sizeof(address);
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] { run(); });
    }
    ~Responder() {
        stop_ = true;
        thread_.join();
        ::close(fd_);
    }
    std::uint16_t port() const { return port_; }

private:
    void run() {
        while (!stop_) {
            pollfd waiting{fd_, POLLIN, 0};
            if (::poll(&waiting, 1, 20) <= 0) {
                continue;
            }
            char buffer[256];
            sockaddr_in from{};
            socklen_t length = sizeof(from);
            const ssize_t n = ::recvfrom(fd_, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &length);
            if (n <= 0 || std::string(buffer, static_cast<std::size_t>(n)) != "who is JellyfinServer?") {
                continue;
            }
            for (const std::string& reply : replies_) {
                ::sendto(fd_, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&from), length);
            }
        }
    }

    std::vector<std::string> replies_;
    int fd_ = -1;
    std::uint16_t port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

const char* kServerA = R"({"Address":"http://192.168.1.20:8096","Id":"a1","Name":"Living room","EndpointAddress":null})";
const char* kServerB = R"({"Address":"https://nas.local:8920","Id":"b2","Name":"NAS"})";

}  // namespace

TEST_CASE("discovery replies are parsed safely") {
    const auto server = parseDiscoveryReply(kServerA);
    REQUIRE(server.has_value());
    CHECK(server->id == "a1");
    CHECK(server->name == "Living room");
    CHECK(server->address == "http://192.168.1.20:8096");
    const std::vector<std::string> bad = {
        R"({"Address":"http://x"})",
        R"({"Id":"x"})",
        R"x({"Id":"x","Address":"javascript:alert(1)"})x",
        "garbage",
        std::string(R"({"Id":"x","Address":"http://h","Name":")") + std::string(5000, 'a') + "\"}",
        std::string(R"({"Id":"x","Address":"http://h","Name":")") + std::string(300, 'a') + "\"}",
        std::string(R"({"Address":"http://h","Id":")") + std::string(300, 'a') + "\"}",
        std::string(R"({"Id":"x","Address":"http://h/)") + std::string(600, 'a') + "\"}",
    };
    for (const std::string& reply : bad) {
        INFO(reply.substr(0, 60));
        CHECK_FALSE(parseDiscoveryReply(reply).has_value());
    }
}

TEST_CASE("discover collects distinct servers until the timeout") {
    Responder responder({"nonsense", kServerA, kServerA, kServerB});
    DiscoveryConfig config;
    config.broadcastAddress = "127.0.0.1";
    config.port = responder.port();
    config.timeoutMs = 500;
    const auto start = std::chrono::steady_clock::now();
    const auto servers = playfin::jellyfin::discover(config);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(servers.size() == 2);
    CHECK(servers[0].id == "a1");
    CHECK(servers[1].id == "b2");
    CHECK(elapsed >= 450ms);
    CHECK(elapsed < 2s);
}

TEST_CASE("discover keeps at most 32 servers") {
    std::vector<std::string> replies;
    for (int i = 0; i < 100; ++i) {
        replies.push_back(R"({"Address":"http://10.0.0.1:8096","Id":"s)" + std::to_string(i) + R"(","Name":"S"})");
    }
    Responder responder(replies);
    DiscoveryConfig config;
    config.broadcastAddress = "127.0.0.1";
    config.port = responder.port();
    config.timeoutMs = 400;
    const auto servers = playfin::jellyfin::discover(config);
    CHECK(servers.size() == playfin::jellyfin::kMaxDiscoveredServers);
    CHECK(playfin::jellyfin::kMaxDiscoveredServers == 32);
}

TEST_CASE("discover returns nothing when nobody answers, and stops when cancelled") {
    Responder silent({});
    DiscoveryConfig config;
    config.broadcastAddress = "127.0.0.1";
    config.port = silent.port();
    config.timeoutMs = 300;
    CHECK(playfin::jellyfin::discover(config).empty());
    playfin::core::CancelToken token;
    token.cancel();
    config.timeoutMs = 5000;
    const auto start = std::chrono::steady_clock::now();
    CHECK(playfin::jellyfin::discover(config, token).empty());
    CHECK(std::chrono::steady_clock::now() - start < 1s);
}

TEST_CASE("an invalid broadcast address gives no servers") {
    DiscoveryConfig config;
    config.broadcastAddress = "not-an-ip";
    CHECK(playfin::jellyfin::discover(config).empty());
}
