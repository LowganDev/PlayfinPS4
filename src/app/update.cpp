#include "app/update.hpp"

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <vector>

#include "core/data_dir.hpp"
#include "core/file_io.hpp"
#include "core/log.hpp"
#include "core/version.hpp"
#include "jellyfin/json.hpp"
#include "net/crypto.hpp"

namespace playfin::app {

namespace json = jellyfin::json;

namespace {

core::Error bad(const char* what) { return core::makeError(core::ErrorKind::BadResponse, what); }

std::vector<long> parts(std::string text) {
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V')) {
        text.erase(0, 1);
    }
    std::vector<long> out;
    long value = 0;
    bool digit = false;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            digit = true;
            if (value > 1000000) {
                return {};
            }
        } else if (c == '.' && digit) {
            out.push_back(value);
            value = 0;
            digit = false;
        } else {
            return {};
        }
    }
    if (!digit) {
        return {};
    }
    out.push_back(value);
    return out;
}

bool hex64(const std::string& text) {
    if (text.size() != 64) {
        return false;
    }
    for (char c : text) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

}  // namespace

core::Result<Release> parseLatestRelease(std::string_view text) {
    const auto doc = json::Document::parse(text);
    if (!doc || !yyjson_is_obj(doc->root())) {
        return bad("not a release");
    }
    Release release;
    release.version = json::string(doc->root(), "tag_name");
    if (!release.version.empty() && (release.version[0] == 'v' || release.version[0] == 'V')) {
        release.version.erase(0, 1);
    }
    if (parts(release.version).empty()) {
        return bad("a release without a version");
    }
    yyjson_val* assets = json::array(doc->root(), "assets");
    yyjson_arr_iter iter = yyjson_arr_iter_with(assets);
    while (yyjson_val* asset = assets != nullptr ? yyjson_arr_iter_next(&iter) : nullptr) {
        const std::string name = json::string(asset, "name");
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".pkg") != 0) {
            continue;
        }
        const std::string url = json::string(asset, "browser_download_url");
        const std::string digest = json::string(asset, "digest");
        const long long size = json::integer(asset, "size");
        if (url.rfind("https://", 0) != 0 || digest.rfind("sha256:", 0) != 0 || !hex64(digest.substr(7)) ||
            size <= 0) {
            continue;
        }
        release.packageUrl = url;
        release.size = size;
        release.sha256 = digest.substr(7);
        for (char& c : release.sha256) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return release;
    }
    return bad("the release has no package");
}

bool newerVersion(const std::string& candidate, const std::string& current) {
    const std::vector<long> a = parts(candidate);
    const std::vector<long> b = parts(current);
    if (a.empty() || b.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        const long x = i < a.size() ? a[i] : 0;
        const long y = i < b.size() ? b[i] : 0;
        if (x != y) {
            return x > y;
        }
    }
    return false;
}

std::string packageFileName(const std::string& version) {
    std::string safe;
    for (char c : version) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
            (c == '.' && !safe.empty() && safe.back() != '.')) {
            safe += c;
        }
    }
    while (!safe.empty() && safe.front() == '.') {
        safe.erase(0, 1);
    }
    return safe.empty() ? std::string() : "Playfin-" + safe + ".pkg";
}

core::Result<Release> fetchLatestRelease(const net::HttpClient& http, const std::string& url,
                                         const core::CancelToken& token) {
    net::HttpRequest request;
    request.url = url;
    request.maxBodyBytes = 1024 * 1024;
    // GitHub's API wants a User-Agent; a generic one (no personal detail)
    request.headers = {{"User-Agent", std::string("Playfin/") + core::version()}, {"Accept", "application/vnd.github+json"}};
    const auto response = http.send(request, {}, token);
    if (!response.ok()) {
        return response.error();
    }
    if (response.value().status != 200) {
        return core::makeError(response.value().status == 404 ? core::ErrorKind::NotFound : core::ErrorKind::Server,
                               "HTTP " + std::to_string(response.value().status));
    }
    return parseLatestRelease(response.value().body);
}

core::Result<core::Unit> downloadPackage(const net::HttpClient& http, const Release& release, const std::string& path,
                                         std::function<void(long long, long long)> progress,
                                         const core::CancelToken& token) {
    core::FileSink sink(path);
    if (!sink.ok()) {
        return core::makeError(core::ErrorKind::Storage, "the package cannot be written");
    }
    net::Sha256Stream digest;
    net::HttpRequest request;
    request.url = release.packageUrl;
    request.timeoutMs = 0;  // a large file on a slow network: only the low-speed limit stops it
    request.headers = {{"User-Agent", std::string("Playfin/") + core::version()}};
    const auto response = http.download(
        request,
        [&](const char* data, std::size_t size) {
            if (sink.written() + static_cast<long long>(size) > release.size) {
                return false;  // more than published: not the file we want
            }
            digest.update(data, size);
            return sink.write(data, size);
        },
        [&](long long received, long long total) {
            if (progress) {
                progress(received, total > 0 ? total : release.size);
            }
        },
        {}, token);
    if (!response.ok()) {
        return response.error();
    }
    if (response.value().status != 200) {
        return core::makeError(core::ErrorKind::Server, "HTTP " + std::to_string(response.value().status));
    }
    if (sink.written() != release.size || net::hexLower(digest.finish()) != release.sha256) {
        PF_LOG_WARN("update: the package does not match its published size and digest");
        return core::makeError(core::ErrorKind::BadResponse, "the package does not match the published one");
    }
    if (!sink.commit()) {
        return core::makeError(core::ErrorKind::Storage, "the package cannot be written");
    }
    return core::Unit{};
}

int removeOldPackages(const std::string& folder, const std::string& current) {
    DIR* listing = ::opendir(folder.c_str());
    if (listing == nullptr) {
        return 0;
    }
    std::vector<std::string> old;
    const std::string prefix = "Playfin-";
    const std::string suffix = ".pkg";
    while (const dirent* file = ::readdir(listing)) {
        const std::string name = file->d_name;
        if (name.size() <= prefix.size() + suffix.size() || name.compare(0, prefix.size(), prefix) != 0 ||
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        const std::string version = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
        // ours exactly (the name the update gives it), and a version: not newer than the one running
        const bool numbered = !version.empty() && std::isdigit(static_cast<unsigned char>(version.front())) != 0;
        if (numbered && packageFileName(version) == name && !newerVersion(version, current)) {
            old.push_back(name);
        }
    }
    ::closedir(listing);
    int removed = 0;
    for (const std::string& name : old) {
        if (const auto path = core::resolveInside(folder, name); path && ::unlink(path->c_str()) == 0) {
            PF_LOG_INFO("update: %s removed (installed)", name.c_str());
            ++removed;
        }
    }
    return removed;
}

}  // namespace playfin::app
