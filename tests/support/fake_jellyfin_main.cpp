#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

#include <memory>

#include "support/fake_ftp.hpp"
#include "support/fake_jellyfin.hpp"

// Standalone fake server for the end-to-end screenshots:
// fake_jellyfin [--https] [--port N] [--url-file PATH] [--media (the views of the test server: music, Live TV)]
//               [--ftp-root DIR --ftp-url-file PATH (a USB drive: DIR served over FTP, for PLAYFIN_USB_FTP)]
int main(int argc, char** argv) {
    bool https = false;
    bool media = false;
    std::string showcase;  // the README screenshots' artwork
    int port = 0;
    std::string urlFile;
    std::string ftpRoot;
    std::string ftpUrlFile;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--https") == 0) {
            https = true;
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--media") == 0) {
            media = true;
        } else if (std::strcmp(argv[i], "--url-file") == 0 && i + 1 < argc) {
            urlFile = argv[++i];
        } else if (std::strcmp(argv[i], "--showcase") == 0 && i + 1 < argc) {
            showcase = argv[++i];
        } else if (std::strcmp(argv[i], "--ftp-root") == 0 && i + 1 < argc) {
            ftpRoot = argv[++i];
        } else if (std::strcmp(argv[i], "--ftp-url-file") == 0 && i + 1 < argc) {
            ftpUrlFile = argv[++i];
        }
    }
    playfin::test::FakeJellyfin server(https ? playfin::test::TlsMode::SelfSigned : playfin::test::TlsMode::None,
                                       "10.10.7", port);
    if (media) {
        server.showMediaViews();
    }
    if (!showcase.empty()) {
        server.showcase(showcase);
    }
    std::unique_ptr<playfin::test::FakeFtp> ftp;
    if (!ftpRoot.empty()) {
        ftp = std::make_unique<playfin::test::FakeFtp>(ftpRoot);
        if (!ftpUrlFile.empty()) {
            std::ofstream(ftpUrlFile) << ftp->base();
        }
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
