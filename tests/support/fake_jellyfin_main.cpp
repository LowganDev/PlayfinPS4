#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

#include "support/fake_jellyfin.hpp"

// Standalone fake server for the end-to-end screenshots:
// fake_jellyfin [--https] [--port N] [--url-file PATH] [--media (the views of the test server: music, Live TV)]
int main(int argc, char** argv) {
    bool https = false;
    bool media = false;
    int port = 0;
    std::string urlFile;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--https") == 0) {
            https = true;
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--media") == 0) {
            media = true;
        } else if (std::strcmp(argv[i], "--url-file") == 0 && i + 1 < argc) {
            urlFile = argv[++i];
        }
    }
    playfin::test::FakeJellyfin server(https ? playfin::test::TlsMode::SelfSigned : playfin::test::TlsMode::None,
                                       "10.10.7", port);
    if (media) {
        server.showMediaViews();
    }
    if (!urlFile.empty()) {
        std::ofstream(urlFile) << server.baseUrl();
    }
    std::printf("%s\n", server.baseUrl().c_str());
    std::fflush(stdout);
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
