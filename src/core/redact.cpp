#include "core/redact.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "core/strings.hpp"

namespace playfin::core {

namespace {

enum class Until { QueryValue, LineEnd, JsonString, QuotedValue };

struct Rule {
    std::string_view key;  // lower case
    Until until;
};

constexpr Rule kRules[] = {
    {"api_key=", Until::QueryValue},
    {"apikey=", Until::QueryValue},
    {"password=", Until::QueryValue},
    {"secret=", Until::QueryValue},
    {"x-emby-token:", Until::LineEnd},
    {"x-mediabrowser-token:", Until::LineEnd},
    {"authorization:", Until::LineEnd},
    {"token=\"", Until::QuotedValue},
    {"\"pw\"", Until::JsonString},
    {"\"password\"", Until::JsonString},
    {"\"accesstoken\"", Until::JsonString},
    {"\"currentpw\"", Until::JsonString},
    {"\"newpw\"", Until::JsonString},
    {"\"secret\"", Until::JsonString},
};

using Range = std::pair<std::size_t, std::size_t>;

bool endsQueryValue(char c) {
    return c == '&' || c == ' ' || c == '"' || c == '\'' || c == '\n' || c == '\r' || c == '\t';
}

std::size_t skipSpaces(std::string_view text, std::size_t pos) {
    while (pos < text.size() && text[pos] == ' ') {
        ++pos;
    }
    return pos;
}

// [begin, end) of the value that starts right after a key ending at `pos`; begin == end when there is none
Range valueRange(std::string_view text, std::size_t pos, Until until) {
    switch (until) {
    case Until::QueryValue: {
        std::size_t end = pos;
        while (end < text.size() && !endsQueryValue(text[end])) {
            ++end;
        }
        return {pos, end};
    }
    case Until::LineEnd: {
        const std::size_t begin = skipSpaces(text, pos);
        std::size_t end = begin;
        while (end < text.size() && text[end] != '\n' && text[end] != '\r') {
            ++end;
        }
        return {begin, end};
    }
    case Until::QuotedValue: {
        std::size_t end = pos;
        while (end < text.size() && text[end] != '"') {
            ++end;
        }
        return {pos, end};
    }
    case Until::JsonString: {
        std::size_t p = skipSpaces(text, pos);
        if (p >= text.size() || text[p] != ':') {
            return {pos, pos};
        }
        p = skipSpaces(text, p + 1);
        if (p >= text.size() || text[p] != '"') {
            return {pos, pos};
        }
        const std::size_t begin = p + 1;
        std::size_t end = begin;
        while (end < text.size() && text[end] != '"') {
            if (text[end] == '\\' && end + 1 < text.size()) {
                ++end;
            }
            ++end;
        }
        return {begin, std::min(end, text.size())};
    }
    }
    return {pos, pos};
}

}  // namespace

std::string redact(std::string_view text) {
    const std::string lower = asciiLower(text);
    std::vector<Range> ranges;
    for (const Rule& rule : kRules) {
        std::size_t from = 0;
        while (from < lower.size()) {
            const std::size_t at = lower.find(rule.key, from);
            if (at == std::string::npos) {
                break;
            }
            const std::size_t keyEnd = at + rule.key.size();
            const Range range = valueRange(text, keyEnd, rule.until);
            if (range.second > range.first) {
                ranges.push_back(range);
                from = range.second;
            } else {
                from = keyEnd;
            }
        }
    }
    if (ranges.empty()) {
        return std::string(text);
    }
    std::sort(ranges.begin(), ranges.end());
    std::string out;
    out.reserve(text.size());
    std::size_t cursor = 0;
    for (const Range& range : ranges) {
        if (range.first < cursor) {
            // inside (or overlapping) the previous mask: extend it
            cursor = std::max(cursor, range.second);
            continue;
        }
        out.append(text.substr(cursor, range.first - cursor));
        out += "***";
        cursor = range.second;
    }
    out.append(text.substr(cursor));
    return out;
}

}  // namespace playfin::core
