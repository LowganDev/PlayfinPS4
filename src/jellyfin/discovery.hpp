#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/task.hpp"

namespace playfin::jellyfin {

// A flood of replies (hostile or broken network) never grows the list past this.
constexpr std::size_t kMaxDiscoveredServers = 32;

struct DiscoveredServer {
    std::string id;
    std::string name;
    std::string address;
};

struct DiscoveryConfig {
    std::string broadcastAddress = "255.255.255.255";
    std::uint16_t port = 7359;
    int timeoutMs = 2000;
};

// {"Address": "...", "Id": "...", "Name": "..."}; nullopt for anything else, over 4 KiB, or with an id or
// name over 128 bytes or an address over 512 bytes
std::optional<DiscoveredServer> parseDiscoveryReply(std::string_view reply);

// Broadcasts "who is JellyfinServer?" and collects distinct replies until the timeout. Blocking.
std::vector<DiscoveredServer> discover(const DiscoveryConfig& config, const core::CancelToken& token = {});

}  // namespace playfin::jellyfin
