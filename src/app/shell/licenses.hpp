#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace playfin::app {

// A line of resources/licenses/manifest.txt: name|version|licence|files|libraries (lists comma separated; the
// libraries are the PS4 link's -l names the component stands for, empty for files such as fonts).
struct LicenseEntry {
    std::string name;
    std::string version;
    std::string license;
    std::vector<std::string> files;
    std::vector<std::string> libraries;
};

// Lines starting with '#' and blank lines are skipped, and so are lines without five fields.
std::vector<LicenseEntry> parseLicenseManifest(std::string_view text);

}  // namespace playfin::app
