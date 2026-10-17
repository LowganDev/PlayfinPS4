#pragma once

#include <httplib.h>

#include <memory>
#include <string>
#include <thread>

#include "support/files.hpp"

namespace playfin::test {

enum class TlsMode { None, CaSigned, SelfSigned };

// In-process HTTP(S) server on 127.0.0.1 and a free port. Set routes on http(), then start().
class FakeServer {
public:
    explicit FakeServer(TlsMode mode = TlsMode::None);
    ~FakeServer();
    FakeServer(const FakeServer&) = delete;
    FakeServer& operator=(const FakeServer&) = delete;

    httplib::Server& http() { return *server_; }
    void start(int port = 0);  // 0: any free port
    std::string baseUrl() const;
    int port() const { return port_; }

private:
    TlsMode mode_;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    int port_ = 0;
};

}  // namespace playfin::test
