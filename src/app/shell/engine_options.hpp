#pragma once

#include "jellyfin/client.hpp"
#include "player/engine.hpp"

namespace playfin::app {

// The player's engine options for streams of `client`'s server: SDL audio and 4 decoder threads on the PS4, no
// audio output in tests (`synchronous`), the fonts and CA list of the resources, the Authorization header.
player::EngineOptions engineOptionsFor(const jellyfin::Client& client, bool synchronous);

}  // namespace playfin::app
