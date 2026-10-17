#include "support/fake_server.hpp"

#include <stdexcept>

namespace playfin::test {

FakeServer::FakeServer(TlsMode mode) : mode_(mode) {
    if (mode == TlsMode::None) {
        server_ = std::make_unique<httplib::Server>();
    } else {
        const std::string name = mode == TlsMode::CaSigned ? "server" : "self";
        server_ = std::make_unique<httplib::SSLServer>(fixturePath("tls/" + name + ".crt").c_str(),
                                                       fixturePath("tls/" + name + ".key").c_str());
    }
}

FakeServer::~FakeServer() {
    server_->stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void FakeServer::start(int port) {
    if (port > 0) {
        if (!server_->bind_to_port("127.0.0.1", port)) {
            throw std::runtime_error("port in use");
        }
        port_ = port;
    } else {
        port_ = server_->bind_to_any_port("127.0.0.1");
    }
    thread_ = std::thread([this] { server_->listen_after_bind(); });
    server_->wait_until_ready();
}

std::string FakeServer::baseUrl() const {
    return std::string(mode_ == TlsMode::None ? "http" : "https") + "://127.0.0.1:" + std::to_string(port_);
}

}  // namespace playfin::test
