#include <doctest/doctest.h>

#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <chrono>
#include <string>
#include <thread>

#include "core/log.hpp"
#include "net/crypto.hpp"
#include "net/http.hpp"
#include "support/fake_server.hpp"

using namespace std::chrono_literals;
using playfin::core::ErrorKind;
using playfin::net::HttpClient;
using playfin::net::HttpConfig;
using playfin::net::HttpRequest;
using playfin::test::FakeServer;
using playfin::test::TlsMode;

namespace {

HttpConfig testConfig() {
    HttpConfig config;
    config.caFile = playfin::test::fixturePath("tls/ca.crt");
    return config;
}

HttpRequest get(const std::string& url) {
    HttpRequest request;
    request.url = url;
    return request;
}

// sleeps up to `limit` in small steps, stopping early when `stop` is set (keeps server shutdown fast)
void sleepUnless(const std::atomic<bool>& stop, std::chrono::milliseconds limit) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (!stop && std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(10ms);
    }
}

}  // namespace

TEST_CASE("GET returns the status and the body") {
    FakeServer server;
    server.http().Get("/hello", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("world", "text/plain");
    });
    server.start();
    const auto result = HttpClient(testConfig()).send(get(server.baseUrl() + "/hello"));
    REQUIRE(result.ok());
    CHECK(result.value().status == 200);
    CHECK(result.value().body == "world");
    CHECK(result.value().contentType.find("text/plain") == 0);
}

TEST_CASE("POST and DELETE send their method, body and headers") {
    FakeServer server;
    server.http().Post("/echo", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content(req.body + "|" + req.get_header_value("X-Test"), "text/plain");
    });
    server.http().Delete("/thing", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("deleted", "text/plain");
    });
    server.start();
    HttpClient client(testConfig());
    HttpRequest post = get(server.baseUrl() + "/echo");
    post.method = "POST";
    post.body = R"({"a":1})";
    post.headers = {{"X-Test", "yes"}, {"Content-Type", "application/json"}};
    const auto echoed = client.send(post);
    REQUIRE(echoed.ok());
    CHECK(echoed.value().body == R"({"a":1}|yes)");
    HttpRequest del = get(server.baseUrl() + "/thing");
    del.method = "DELETE";
    const auto deleted = client.send(del);
    REQUIRE(deleted.ok());
    CHECK(deleted.value().body == "deleted");
}

TEST_CASE("HTTP error statuses are returned for the caller to map") {
    FakeServer server;
    server.start();
    const auto result = HttpClient(testConfig()).send(get(server.baseUrl() + "/missing"));
    REQUIRE(result.ok());
    CHECK(result.value().status == 404);
}

TEST_CASE("a refused connection is a network error") {
    const auto result = HttpClient(testConfig()).send(get("http://127.0.0.1:1/"));
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == ErrorKind::Network);
}

TEST_CASE("a slow server times out") {
    std::atomic<bool> stop{false};
    FakeServer server;
    server.http().Get("/slow", [&](const httplib::Request&, httplib::Response& res) {
        sleepUnless(stop, 2000ms);
        res.set_content("late", "text/plain");
    });
    server.start();
    HttpRequest request = get(server.baseUrl() + "/slow");
    request.timeoutMs = 300;
    const auto result = HttpClient(testConfig()).send(request);
    stop = true;
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == ErrorKind::Timeout);
}

TEST_CASE("bodies over the limit are refused") {
    FakeServer server;
    server.http().Get("/big", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(std::string(4096, 'x'), "text/plain");
    });
    server.start();
    HttpConfig config = testConfig();
    config.maxBodyBytes = 1024;
    const auto result = HttpClient(config).send(get(server.baseUrl() + "/big"));
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == ErrorKind::BadResponse);
}

TEST_CASE("a request can set a lower limit than the client") {
    FakeServer server;
    server.http().Get("/big", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(std::string(4096, 'x'), "text/plain");
    });
    server.start();
    HttpRequest request = get(server.baseUrl() + "/big");
    request.maxBodyBytes = 1024;
    const auto refused = HttpClient(testConfig()).send(request);
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().kind == ErrorKind::BadResponse);
    request.maxBodyBytes = 0;  // 0 = the client's limit
    CHECK(HttpClient(testConfig()).send(request).ok());
}

TEST_CASE("cancelling stops a transfer") {
    std::atomic<bool> stop{false};
    FakeServer server;
    server.http().Get("/slow", [&](const httplib::Request&, httplib::Response& res) {
        sleepUnless(stop, 5000ms);
        res.set_content("late", "text/plain");
    });
    server.start();
    playfin::core::CancelToken token;
    std::thread canceller([&] {
        std::this_thread::sleep_for(100ms);
        token.cancel();
    });
    const auto start = std::chrono::steady_clock::now();
    const auto result = HttpClient(testConfig()).send(get(server.baseUrl() + "/slow"), {}, token);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    canceller.join();
    stop = true;
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == ErrorKind::Cancelled);
    CHECK(elapsed < 3s);
}

TEST_CASE("a cancelled request is not logged as a failure") {
    FakeServer server;
    server.http().Get("/any", [](const httplib::Request&, httplib::Response& res) { res.set_content("x", "text/plain"); });
    server.start();
    char tmpl[] = "/tmp/playfin-httplog-XXXXXX";
    REQUIRE(mkdtemp(tmpl) != nullptr);
    const std::string logPath = std::string(tmpl) + "/log.txt";
    REQUIRE(playfin::log::openFile(logPath));
    playfin::core::CancelToken token;
    token.cancel();
    const auto result = HttpClient(testConfig()).send(get(server.baseUrl() + "/any"), {}, token);
    playfin::log::closeFile();
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == ErrorKind::Cancelled);
    std::ifstream in(logPath);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find(" W ") == std::string::npos);
    std::remove(logPath.c_str());
    ::rmdir(tmpl);
}

TEST_CASE("redirects never go from https to http") {
    FakeServer plain;
    plain.http().Get("/hello", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("plain", "text/plain");
    });
    plain.start();
    FakeServer secure(TlsMode::CaSigned);
    const std::string target = plain.baseUrl() + "/hello";
    secure.http().Get("/go", [&](const httplib::Request&, httplib::Response& res) { res.set_redirect(target); });
    secure.start();
    const auto result = HttpClient(testConfig()).send(get(secure.baseUrl() + "/go"));
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == ErrorKind::Network);
    FakeServer other;
    other.http().Get("/go", [&](const httplib::Request&, httplib::Response& res) { res.set_redirect(target); });
    other.start();
    const auto followed = HttpClient(testConfig()).send(get(other.baseUrl() + "/go"));
    REQUIRE(followed.ok());
    CHECK(followed.value().body == "plain");
}

TEST_CASE("a server with a trusted chain is verified without a pin") {
    FakeServer server(TlsMode::CaSigned);
    server.http().Get("/hello", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("secure", "text/plain");
    });
    server.start();
    const auto result = HttpClient(testConfig()).send(get(server.baseUrl() + "/hello"));
    REQUIRE(result.ok());
    CHECK(result.value().body == "secure");
}

TEST_CASE("an untrusted certificate is reported with its fingerprint") {
    FakeServer server(TlsMode::SelfSigned);
    server.start();
    const auto result = HttpClient(testConfig()).send(get(server.baseUrl() + "/System/Info/Public"));
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().kind == ErrorKind::CertificateUntrusted);
    CHECK(playfin::net::hexFingerprint(result.error().certificate.spkiSha256) ==
          "EF:FD:3E:D9:00:FC:AD:2C:D4:7D:0E:BA:21:2D:D7:D5:EB:C1:4D:09:0E:01:F2:59:EC:04:B1:23:C7:D2:C9:39");
    CHECK(result.error().certificate.subject.find("Playfin Self Signed") != std::string::npos);
    CHECK_FALSE(result.error().certificate.notAfter.empty());
}

TEST_CASE("a pinned key is accepted and any other key is refused") {
    FakeServer server(TlsMode::SelfSigned);
    server.http().Get("/hello", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("pinned", "text/plain");
    });
    server.start();
    HttpClient client(testConfig());
    const auto pinned =
        client.send(get(server.baseUrl() + "/hello"), "sha256//7/0+2QD8rSzUfQ66IS3X1evBTQkOAfJZ7ASxI8fSyTk=");
    REQUIRE(pinned.ok());
    CHECK(pinned.value().body == "pinned");
    const auto other =
        client.send(get(server.baseUrl() + "/hello"), "sha256//e/lJHyILdq+f84w+ilCF7/374MlV1SHk2yfT9CzB3nM=");
    REQUIRE_FALSE(other.ok());
    CHECK(other.error().kind == ErrorKind::Tls);
}

TEST_CASE("a wrong pin is refused even for a server with a trusted chain") {
    FakeServer server(TlsMode::CaSigned);
    server.start();
    const auto result = HttpClient(testConfig()).send(get(server.baseUrl() + "/"),
                                                     "sha256//7/0+2QD8rSzUfQ66IS3X1evBTQkOAfJZ7ASxI8fSyTk=");
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == ErrorKind::Tls);
}
