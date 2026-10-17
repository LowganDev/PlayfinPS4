#include "app/shell/engine_options.hpp"

#include "platform/platform.hpp"

namespace playfin::app {

player::EngineOptions engineOptionsFor(const jellyfin::Client& client, bool synchronous) {
    const std::string resources = platform::resourceDir();
    player::EngineOptions options;
#ifdef PLAYFIN_PS4
    options.audioOutput = "sdl";
    options.decoderThreads = 4;  // of the 6 cores an app gets (the UI uses the others)
#endif
    if (synchronous) {
        options.audioOutput = "null";  // tests
    }
    options.fontsDir = resources + "/fonts";
    options.caFile = resources + "/certs/cacert.pem";
    options.verifyTls = client.session().pin.empty();  // FFmpeg cannot check a pinned certificate
    options.authorization = jellyfin::authorizationHeader(client.session());
    return options;
}

}  // namespace playfin::app
