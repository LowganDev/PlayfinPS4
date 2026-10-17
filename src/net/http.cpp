#include "net/http.hpp"

#include <curl/curl.h>

#include <chrono>
#include <mutex>
#include <vector>

#include "core/log.hpp"
#include "core/strings.hpp"
#include "net/crypto.hpp"
#include "net/url.hpp"

namespace playfin::net {

namespace {

std::once_flag gInitOnce;
std::mutex gShareMutex;
CURLSH* gShare = nullptr;

void lockShare(CURL*, curl_lock_data, curl_lock_access, void*) { gShareMutex.lock(); }
void unlockShare(CURL*, curl_lock_data, void*) { gShareMutex.unlock(); }

// Reused curl handles, borrowed per request. Not thread_local: a thread_local object with a destructor needs
// __cxa_thread_atexit_impl, which no PS4 system module provides (the eboot does not load).
std::mutex gPoolMutex;

struct HandlePool {
    std::vector<CURL*> handles;
    ~HandlePool() {
        for (CURL* curl : handles) {
            curl_easy_cleanup(curl);
        }
    }
};
HandlePool gPool;

class Lease {
public:
    Lease() {
        {
            std::lock_guard<std::mutex> lock(gPoolMutex);
            if (!gPool.handles.empty()) {
                curl_ = gPool.handles.back();
                gPool.handles.pop_back();
            }
        }
        if (curl_ == nullptr) {
            curl_ = curl_easy_init();
        } else {
            curl_easy_reset(curl_);
        }
    }
    ~Lease() {
        if (curl_ != nullptr) {
            std::lock_guard<std::mutex> lock(gPoolMutex);
            gPool.handles.push_back(curl_);
        }
    }
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    CURL* get() const { return curl_; }

private:
    CURL* curl_ = nullptr;
};

struct Transfer {
    std::string* body;
    std::size_t limit;
    bool tooBig;
    const core::CancelToken* token;
    std::string trace = {};  // curl's informational lines, logged when the request fails
};

// CURLOPT_DEBUGFUNCTION: keep curl's own text (resolve, connect, TLS steps), never the data or the headers
int onDebug(CURL*, curl_infotype type, char* data, std::size_t size, void* user) {
    auto* transfer = static_cast<Transfer*>(user);
    if (type == CURLINFO_TEXT && transfer->trace.size() < 4096) {
        transfer->trace.append(data, std::min(size, 4096 - transfer->trace.size()));
    }
    return 0;
}

std::size_t onWrite(char* data, std::size_t size, std::size_t count, void* user) {
    auto* transfer = static_cast<Transfer*>(user);
    const std::size_t bytes = size * count;
    if (transfer->body->size() + bytes > transfer->limit) {
        transfer->tooBig = true;
        return 0;
    }
    transfer->body->append(data, bytes);
    return bytes;
}

int onProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<Transfer*>(user)->token->cancelled() ? 1 : 0;
}

void applyCommon(CURL* curl, const HttpConfig& config, const std::string& url, long timeoutMs,
                 long connectTimeoutMs, Transfer& transfer) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connectTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, config.lowSpeedSeconds);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_SHARE, gShare);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    if (!config.caFile.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, config.caFile.c_str());
    }
#ifdef PLAYFIN_PS4
    // lessons of the old app: IPv6 attempts stalled, and TCP keepalive options are Linux numbers on a FreeBSD kernel
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, static_cast<long>(CURL_IPRESOLVE_V4));
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 0L);
#endif
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, onDebug);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &transfer);
}

// scheme://host[:port] of a URL that may carry a path and a query
std::string originOf(const std::string& url) {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string::npos) {
        return url;
    }
    const std::size_t end = url.find_first_of("/?#", scheme + 3);
    return end == std::string::npos ? url : url.substr(0, end);
}

long elapsedMs(std::chrono::steady_clock::time_point start) {
    return static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

}  // namespace

void globalInit() {
    std::call_once(gInitOnce, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        // DNS only: shared TLS sessions and connections stalled handshakes on PS4 (old app)
        gShare = curl_share_init();
        curl_share_setopt(gShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(gShare, CURLSHOPT_LOCKFUNC, lockShare);
        curl_share_setopt(gShare, CURLSHOPT_UNLOCKFUNC, unlockShare);
    });
}

HttpClient::HttpClient() : HttpClient(HttpConfig{}) {}

HttpClient::HttpClient(HttpConfig config) : config_(std::move(config)) { globalInit(); }

core::Result<HttpResponse> HttpClient::send(const HttpRequest& request, const std::string& pin,
                                            const core::CancelToken& token) const {
    Lease lease;
    CURL* curl = lease.get();
    if (curl == nullptr) {
        return core::makeError(core::ErrorKind::Network, "curl is not available");
    }
    const auto started = std::chrono::steady_clock::now();
    HttpResponse response;
    const std::size_t limit = request.maxBodyBytes != 0 ? request.maxBodyBytes : config_.maxBodyBytes;
    Transfer transfer{&response.body, limit, false, &token};
    applyCommon(curl, config_, request.url, request.timeoutMs, request.connectTimeoutMs, transfer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    const bool secure = request.url.rfind("https://", 0) == 0;
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, secure ? "https" : "http,https");
    if (request.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    } else if (request.method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
        if (!request.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
        }
    }
    curl_slist* headers = nullptr;
    for (const auto& header : request.headers) {
        const std::string line = header.first + ": " + header.second;
        headers = curl_slist_append(headers, line.c_str());
    }
    headers = curl_slist_append(headers, "Expect:");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (!pin.empty()) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, pin.c_str());
    }

    const CURLcode code = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    char* contentType = nullptr;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &contentType);
    if (contentType != nullptr) {
        response.contentType = contentType;
    }
    if (code == CURLE_OK) {
        PF_LOG_DEBUG("%s %s -> %ld (%ld ms)", request.method.c_str(), request.url.c_str(), response.status,
                     elapsedMs(started));
        return response;
    }
    if (code == CURLE_ABORTED_BY_CALLBACK) {  // cancelled: the screen that asked went away
        PF_LOG_DEBUG("%s %s cancelled after %ld ms", request.method.c_str(), request.url.c_str(), elapsedMs(started));
        return core::makeError(core::ErrorKind::Cancelled, "cancelled");
    }
    PF_LOG_WARN("%s %s failed after %ld ms: %s", request.method.c_str(), request.url.c_str(), elapsedMs(started),
                curl_easy_strerror(code));
    if (!transfer.trace.empty()) {
        PF_LOG_WARN("curl: %s", transfer.trace.c_str());
    }
    switch (code) {
    case CURLE_WRITE_ERROR:
        if (transfer.tooBig) {
            return core::makeError(core::ErrorKind::BadResponse, "the response is too large");
        }
        return core::makeError(core::ErrorKind::Network, curl_easy_strerror(code));
    case CURLE_OPERATION_TIMEDOUT:
        return core::makeError(core::ErrorKind::Timeout, "the server did not answer in time");
    case CURLE_PEER_FAILED_VERIFICATION: {
        if (!pin.empty()) {
            return core::makeError(core::ErrorKind::Tls, curl_easy_strerror(code));
        }
        auto certificate = fetchCertificate(originOf(request.url) + "/", token);
        if (!certificate.ok()) {
            return certificate.error();
        }
        core::Error error = core::makeError(core::ErrorKind::CertificateUntrusted, "the certificate is not trusted");
        error.certificate = certificate.value();
        return error;
    }
    case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
        return core::makeError(core::ErrorKind::Tls, "the server certificate changed");
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_SSL_CACERT_BADFILE:
        return core::makeError(core::ErrorKind::Tls, curl_easy_strerror(code));
    default:
        return core::makeError(core::ErrorKind::Network, curl_easy_strerror(code));
    }
}

core::Result<core::CertificateInfo> HttpClient::fetchCertificate(const std::string& url,
                                                                 const core::CancelToken& token) const {
    Lease lease;
    CURL* curl = lease.get();
    if (curl == nullptr) {
        return core::makeError(core::ErrorKind::Network, "curl is not available");
    }
    std::string body;
    Transfer transfer{&body, 64 * 1024, false, &token};
    applyCommon(curl, config_, url, 10000, 5000, transfer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L);
    const CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_ABORTED_BY_CALLBACK) {
        return core::makeError(core::ErrorKind::Cancelled, "cancelled");
    }
    curl_certinfo* info = nullptr;
    curl_easy_getinfo(curl, CURLINFO_CERTINFO, &info);
    if (info == nullptr || info->num_of_certs < 1) {
        return core::makeError(core::ErrorKind::Tls,
                               code == CURLE_OK ? "no certificate received" : curl_easy_strerror(code));
    }
    core::CertificateInfo certificate;
    std::string pem;
    for (curl_slist* entry = info->certinfo[0]; entry != nullptr; entry = entry->next) {
        const std::string_view line(entry->data);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        const std::string_view name = line.substr(0, colon);
        const std::string_view value = line.substr(colon + 1);
        if (name == "Cert") {
            pem = std::string(value);
        } else if (name == "Subject") {
            certificate.subject = std::string(core::trimmed(value));
        } else if (name == "Issuer") {
            certificate.issuer = std::string(core::trimmed(value));
        } else if (name == "Expire date") {
            certificate.notAfter = std::string(core::trimmed(value));
        }
    }
    const auto digest = spkiSha256FromPem(pem);
    if (!digest) {
        return core::makeError(core::ErrorKind::Tls, "cannot read the server certificate");
    }
    certificate.spkiSha256 = *digest;
    return certificate;
}

}  // namespace playfin::net
