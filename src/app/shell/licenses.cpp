#include "app/shell/licenses.hpp"

namespace playfin::app {

namespace {

std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(separator, start);
        const std::string_view part = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        parts.emplace_back(part);
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

std::vector<std::string> list(const std::string& field) {
    std::vector<std::string> items;
    for (auto& item : split(field, ',')) {
        if (!item.empty()) {
            items.push_back(std::move(item));
        }
    }
    return items;
}

}  // namespace

std::vector<LicenseEntry> parseLicenseManifest(std::string_view text) {
    std::vector<LicenseEntry> entries;
    for (std::string line : split(text, '\n')) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto fields = split(line, '|');
        if (fields.size() != 5 || fields[0].empty()) {
            continue;
        }
        entries.push_back({fields[0], fields[1], fields[2], list(fields[3]), list(fields[4])});
    }
    return entries;
}

}  // namespace playfin::app
