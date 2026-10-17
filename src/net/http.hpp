#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/result.hpp"
#include "core/task.hpp"

namespace playfin::net {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    long timeoutMs = 30000;
    long connectTimeoutMs = 5000;
    std::size_t maxBodyBytes = 0;  // 0 = HttpConfig::maxBodyBytes
};

struct HttpResponse {
    long status = 0;
    std::string body;
    std::string contentType;
};

struct HttpConfig {
    std::string caFile;  // trusted CA list (resources/certs/cacert.pem)
    std::size_t maxBodyBytes = 64u * 1024u * 1024u;
    long lowSpeedSeconds = 15;  // abort below 1 byte/s for this long
};

// curl_global_init and the shared DNS cache, once per process (thread-safe).
void globalInit();

// Blocking HTTP(S) over curl, from a shared pool of reused handles. Call from worker threads.
class HttpClient {
public:
    HttpClient();
    explicit HttpClient(HttpConfig config);

    // Any HTTP status is a success here (the caller maps 4xx/5xx). With `pin` ("sha256//<base64>"), certificate
    // verification is replaced by that exact public key; without, the chain and host name are verified.
    core::Result<HttpResponse> send(const HttpRequest& request, const std::string& pin = {},
                                    const core::CancelToken& token = {}) const;

    // The certificate presented by the server of `url`, read without trusting it (trust on first use): a GET of
    // the server root without any header, response discarded.
    core::Result<core::CertificateInfo> fetchCertificate(const std::string& url,
                                                         const core::CancelToken& token = {}) const;


private:
    HttpConfig config_;
};

}  // namespace playfin::net
