#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "core/result.hpp"
#include "core/task.hpp"
#include "net/http.hpp"

namespace playfin::app {

// Where Playfin's releases are published (GitHub's API for the latest one).
constexpr const char* kLatestReleaseUrl = "https://api.github.com/repos/LowganDev/PlayfinPS4/releases/latest";

// The newest published version and its PS4 package.
struct Release {
    std::string version;     // "2.0.0" (a leading v removed)
    std::string packageUrl;  // https only
    long long size = 0;
    std::string sha256;      // 64 lower-case hex digits, as GitHub publishes it
};

// GitHub's latest release: its tag and its .pkg asset (with an https address, a size and a sha256 digest), else
// BadResponse.
core::Result<Release> parseLatestRelease(std::string_view json);
// "2.0.0" newer than "1.0.1" (dotted numbers, a v prefix accepted; anything else is never newer)
bool newerVersion(const std::string& candidate, const std::string& current);
// "Playfin-2.0.0.pkg": the version's digits, letters, dots and dashes only (empty without any)
std::string packageFileName(const std::string& version);

// Removes the update packages "Playfin-<version>.pkg" in `folder` whose version is `current` or older (installed:
// they only fill GoldHEN's list), and nothing else. Returns how many went.
int removeOldPackages(const std::string& folder, const std::string& current);

core::Result<Release> fetchLatestRelease(const net::HttpClient& http, const std::string& url,
                                         const core::CancelToken& token = {});
// The package to `path` (through `path`.part), checked against the release's size and digest as it arrives:
// nothing stays at `path` unless both match.
core::Result<core::Unit> downloadPackage(const net::HttpClient& http, const Release& release, const std::string& path,
                                         std::function<void(long long received, long long total)> progress,
                                         const core::CancelToken& token = {});

}  // namespace playfin::app
