#include "jellyfin/discovery.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>

#include "core/log.hpp"
#include "jellyfin/json.hpp"
#include "net/url.hpp"

#ifdef PLAYFIN_PS4
// The PS4 kernel is FreeBSD. The OpenOrbis sys/socket.h has the FreeBSD values for these (unlike the TCP-level
// options, see the TCP keepalive lesson): keep it that way.
static_assert(SOL_SOCKET == 0xffff && SO_BROADCAST == 0x0020, "socket option numbers must be FreeBSD's on PS4");
#endif

namespace playfin::jellyfin {

namespace {

constexpr std::size_t kMaxReply = 4096;
constexpr std::size_t kMaxIdOrName = 128;
constexpr std::size_t kMaxAddress = 512;
constexpr char kMessage[] = "who is JellyfinServer?";

}  // namespace

std::optional<DiscoveredServer> parseDiscoveryReply(std::string_view reply) {
    if (reply.empty() || reply.size() > kMaxReply) {
        return std::nullopt;
    }
    const auto doc = json::Document::parse(reply);
    if (!doc || !yyjson_is_obj(doc->root())) {
        return std::nullopt;
    }
    DiscoveredServer server;
    server.id = json::string(doc->root(), "Id");
    server.name = json::string(doc->root(), "Name");
    server.address = json::string(doc->root(), "Address");
    if (server.id.empty() || server.id.size() > kMaxIdOrName || server.name.size() > kMaxIdOrName ||
        server.address.size() > kMaxAddress || !net::parseUrl(server.address)) {
        return std::nullopt;
    }
    return server;
}

std::vector<DiscoveredServer> discover(const DiscoveryConfig& config, const core::CancelToken& token) {
    std::vector<DiscoveredServer> found;
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(config.port);
    if (::inet_pton(AF_INET, config.broadcastAddress.c_str(), &target.sin_addr) != 1) {
        PF_LOG_WARN("discovery: invalid broadcast address %s", config.broadcastAddress.c_str());
        return found;
    }
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        PF_LOG_WARN("discovery: no socket (%d)", errno);
        return found;
    }
    const int on = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) != 0) {
        PF_LOG_WARN("discovery: SO_BROADCAST refused (%d)", errno);
    }
    if (::sendto(fd, kMessage, sizeof(kMessage) - 1, 0, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) <
        0) {
        PF_LOG_WARN("discovery: send failed (%d)", errno);
        ::close(fd);
        return found;
    }
    timeval wait{0, 100 * 1000};
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait)) != 0) {
        PF_LOG_WARN("discovery: SO_RCVTIMEO refused (%d)", errno);
        ::close(fd);
        return found;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(config.timeoutMs);
    while (!token.cancelled() && found.size() < kMaxDiscoveredServers) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            break;
        }
        // the socket's receive timeout (100 ms) bounds each wait: poll()'s timeout was not honoured on the PS4
        char buffer[kMaxReply + 1];
        const ssize_t n = ::recvfrom(fd, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            PF_LOG_WARN("discovery: receive failed (%d)", errno);
            break;
        }
        if (n <= 0) {
            continue;
        }
        const auto server = parseDiscoveryReply(std::string_view(buffer, static_cast<std::size_t>(n)));
        if (server && std::none_of(found.begin(), found.end(),
                                   [&](const DiscoveredServer& known) { return known.id == server->id; })) {
            PF_LOG_INFO("discovery: %s at %s", server->name.c_str(), server->address.c_str());
            found.push_back(*server);
        }
    }
    ::close(fd);
    PF_LOG_INFO("discovery: done, %d server(s)", static_cast<int>(found.size()));
    return found;
}

}  // namespace playfin::jellyfin
