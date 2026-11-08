#include "jellyfin/xmltv.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace playfin::jellyfin {

namespace {

constexpr std::size_t kMaxNames = 20;  // display-names kept per channel

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// `text` would still need more bytes to tell whether it starts with `prefix`
bool couldStartWith(std::string_view text, std::string_view prefix) {
    return text.size() < prefix.size() && prefix.compare(0, text.size(), text) == 0;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

void appendUtf8(std::string& out, unsigned long code) {
    if (code == 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
        return;
    }
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

// XML text with its entities decoded, appended to `out` up to `limit` bytes
void appendDecoded(std::string& out, std::string_view text, std::size_t limit) {
    for (std::size_t i = 0; i < text.size() && out.size() < limit; ++i) {
        if (text[i] != '&') {
            out += text[i];
            continue;
        }
        const std::size_t semicolon = text.find(';', i);
        if (semicolon == std::string_view::npos || semicolon - i > 12) {
            out += '&';
            continue;
        }
        const std::string_view entity = text.substr(i + 1, semicolon - i - 1);
        if (entity == "amp") {
            out += '&';
        } else if (entity == "lt") {
            out += '<';
        } else if (entity == "gt") {
            out += '>';
        } else if (entity == "quot") {
            out += '"';
        } else if (entity == "apos") {
            out += '\'';
        } else if (entity.size() > 1 && entity[0] == '#') {
            const bool hex = entity[1] == 'x' || entity[1] == 'X';
            unsigned long code = 0;
            bool digits = entity.size() > (hex ? 2u : 1u);
            for (std::size_t k = hex ? 2 : 1; k < entity.size() && digits; ++k) {
                const char c = entity[k];
                int value = -1;
                if (c >= '0' && c <= '9') {
                    value = c - '0';
                } else if (hex && c >= 'a' && c <= 'f') {
                    value = c - 'a' + 10;
                } else if (hex && c >= 'A' && c <= 'F') {
                    value = c - 'A' + 10;
                }
                digits = value >= 0 && code <= 0x10FFFF;
                code = code * (hex ? 16 : 10) + static_cast<unsigned long>(value < 0 ? 0 : value);
            }
            if (digits) {
                appendUtf8(out, code);
            }
        } else {
            out.append(text.substr(i, semicolon - i + 1));  // unknown: kept as it is
        }
        i = semicolon;
    }
}

// cut to `limit` bytes without splitting a UTF-8 character, spaces around trimmed
std::string tidy(std::string text, std::size_t limit) {
    if (text.size() > limit) {
        std::size_t end = limit;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
            --end;
        }
        text.resize(end);
    }
    std::size_t first = 0;
    while (first < text.size() && isSpace(text[first])) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first && isSpace(text[last - 1])) {
        --last;
    }
    return text.substr(first, last - first);
}

struct Tag {
    std::string_view name;
    bool closing = false;
    bool selfClosing = false;
    std::string_view attributes;
};

Tag parseTag(std::string_view inside) {
    Tag tag;
    if (!inside.empty() && inside.front() == '/') {
        tag.closing = true;
        inside.remove_prefix(1);
    }
    if (!inside.empty() && inside.back() == '/') {
        tag.selfClosing = true;
        inside.remove_suffix(1);
    }
    std::size_t end = 0;
    while (end < inside.size() && !isSpace(inside[end])) {
        ++end;
    }
    tag.name = inside.substr(0, end);
    tag.attributes = inside.substr(end);
    return tag;
}

// the value of attribute `name` (entities decoded), empty without one
std::string attribute(std::string_view attributes, std::string_view name) {
    std::size_t i = 0;
    while (i < attributes.size()) {
        while (i < attributes.size() && isSpace(attributes[i])) {
            ++i;
        }
        const std::size_t keyStart = i;
        while (i < attributes.size() && attributes[i] != '=' && !isSpace(attributes[i])) {
            ++i;
        }
        const std::string_view key = attributes.substr(keyStart, i - keyStart);
        while (i < attributes.size() && isSpace(attributes[i])) {
            ++i;
        }
        if (i >= attributes.size() || attributes[i] != '=') {
            if (i == keyStart) {
                ++i;
            }
            continue;
        }
        ++i;
        while (i < attributes.size() && isSpace(attributes[i])) {
            ++i;
        }
        if (i >= attributes.size() || (attributes[i] != '"' && attributes[i] != '\'')) {
            return {};
        }
        const char quote = attributes[i++];
        const std::size_t close = attributes.find(quote, i);
        if (close == std::string_view::npos) {
            return {};
        }
        if (key == name) {
            std::string value;
            appendDecoded(value, attributes.substr(i, close - i), XmltvReader::kMaxText);
            return value;
        }
        i = close + 1;
    }
    return {};
}

// days since 1970-01-01 of a civil date (proleptic Gregorian)
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned monthFromMarch = m > 2 ? m - 3 : m + 9;
    const unsigned doy = (153 * monthFromMarch + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

}  // namespace

std::int64_t parseXmltvTime(std::string_view text) {
    std::size_t digits = 0;
    while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9') {
        ++digits;
    }
    if (digits != 12 && digits != 14) {
        return 0;
    }
    const auto number = [&text](std::size_t at, std::size_t count) {
        int value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            value = value * 10 + (text[at + i] - '0');
        }
        return value;
    };
    const int year = number(0, 4);
    const int month = number(4, 2);
    const int day = number(6, 2);
    const int hour = number(8, 2);
    const int minute = number(10, 2);
    const int second = digits == 14 ? number(12, 2) : 0;
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
        return 0;
    }
    std::int64_t seconds = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 +
                           hour * 3600 + minute * 60 + second;
    std::string_view rest = text.substr(digits);
    while (!rest.empty() && isSpace(rest.front())) {
        rest.remove_prefix(1);
    }
    if (rest.size() >= 5 && (rest[0] == '+' || rest[0] == '-')) {
        bool valid = true;
        for (std::size_t i = 1; i < 5; ++i) {
            valid = valid && rest[i] >= '0' && rest[i] <= '9';
        }
        if (valid) {
            const int offset = ((rest[1] - '0') * 10 + (rest[2] - '0')) * 3600 + ((rest[3] - '0') * 10 + (rest[4] - '0')) * 60;
            seconds -= rest[0] == '+' ? offset : -offset;
        }
    }
    return seconds;
}

std::string channelKey(std::string_view name) {
    std::string key;
    for (char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')) {
            key += c;
        } else if (c >= 'A' && c <= 'Z') {
            key += static_cast<char>(c - 'A' + 'a');
        }
    }
    for (const char* suffix : {"fhd", "uhd", "hd", "sd", "4k"}) {
        const std::size_t length = std::strlen(suffix);
        if (key.size() > length && key.compare(key.size() - length, length, suffix) == 0) {
            key.resize(key.size() - length);
            break;
        }
    }
    return key;
}

struct XmltvReader::Impl {
    std::int64_t from;
    std::int64_t to;
    bool failed = false;
    bool decided = false;  // plain or gzip
    bool gzip = false;
    bool zOpen = false;
    z_stream z{};
    std::string head;  // the first bytes, until gzip or not is known
    std::size_t input = 0;
    std::string pending;
    bool rootSeen = false;
    // what is being read
    bool inChannel = false;
    bool inProgramme = false;
    bool keep = false;
    bool haveTitle = false;
    bool haveDescription = false;
    enum class Capture { None, Name, Title, Description } capture = Capture::None;
    std::string text;
    GuideChannel channel;
    GuideProgram program;
    Guide guide;

    ~Impl() {
        if (zOpen) {
            inflateEnd(&z);
        }
    }

    void fail() {
        failed = true;
        pending.clear();
    }

    void openTag(const Tag& tag) {
        if (!rootSeen) {
            rootSeen = true;
            if (tag.name != "tv") {  // an error page, not a guide
                fail();
            }
            return;
        }
        if (tag.name == "channel" && !inProgramme) {
            inChannel = true;
            channel = GuideChannel{};
            channel.id = attribute(tag.attributes, "id");
        } else if (tag.name == "display-name" && inChannel) {
            startCapture(Capture::Name);
        } else if (tag.name == "programme" && !inChannel) {
            inProgramme = true;
            haveTitle = haveDescription = false;
            program = GuideProgram{};
            program.channel = attribute(tag.attributes, "channel");
            program.startUtc = parseXmltvTime(attribute(tag.attributes, "start"));
            program.endUtc = parseXmltvTime(attribute(tag.attributes, "stop"));
            keep = !program.channel.empty() && program.startUtc > 0 && program.endUtc > program.startUtc &&
                   program.endUtc > from && program.startUtc < to && guide.programs.size() < kMaxPrograms;
        } else if (tag.name == "title" && inProgramme && keep && !haveTitle) {
            startCapture(Capture::Title);
        } else if (tag.name == "desc" && inProgramme && keep && !haveDescription) {
            startCapture(Capture::Description);
        }
    }

    void closeTag(std::string_view name) {
        if (name == "display-name" && capture == Capture::Name) {
            std::string value = tidy(std::move(text), kMaxText);
            if (!value.empty() && channel.names.size() < kMaxNames) {
                channel.names.push_back(std::move(value));
            }
            capture = Capture::None;
        } else if (name == "title" && capture == Capture::Title) {
            program.title = tidy(std::move(text), kMaxText);
            haveTitle = true;
            capture = Capture::None;
        } else if (name == "desc" && capture == Capture::Description) {
            program.description = tidy(std::move(text), kMaxText);
            haveDescription = true;
            capture = Capture::None;
        } else if (name == "channel" && inChannel) {
            inChannel = false;
            capture = Capture::None;
            if (!channel.id.empty() && guide.channels.size() < kMaxChannels) {
                guide.channels.push_back(std::move(channel));
            }
        } else if (name == "programme" && inProgramme) {
            inProgramme = false;
            capture = Capture::None;
            if (keep && guide.programs.size() < kMaxPrograms) {
                guide.programs.push_back(std::move(program));
            }
        }
    }

    void startCapture(Capture what) {
        capture = what;
        text.clear();
    }

    void characters(std::string_view raw, bool decode) {
        if (capture == Capture::None) {
            return;
        }
        if (decode) {
            appendDecoded(text, raw, kMaxText + 4);
        } else if (text.size() < kMaxText + 4) {
            text.append(raw.substr(0, kMaxText + 4 - text.size()));
        }
    }

    // the complete pieces of `pending`: texts, tags, comments, CDATA; the rest waits for more bytes
    void parse() {
        std::size_t pos = 0;
        while (!failed && pos < pending.size()) {
            const std::string_view rest(pending.data() + pos, pending.size() - pos);
            if (rest.front() != '<') {
                const std::size_t lt = rest.find('<');
                if (lt == std::string_view::npos) {
                    break;  // a text ends at the next tag
                }
                characters(rest.substr(0, lt), true);
                pos += lt;
                continue;
            }
            if (couldStartWith(rest, "<!--") || couldStartWith(rest, "<![CDATA[")) {
                break;
            }
            std::size_t consumed = 0;
            if (startsWith(rest, "<!--")) {
                const std::size_t end = rest.find("-->", 4);
                if (end == std::string_view::npos) {
                    break;
                }
                consumed = end + 3;
            } else if (startsWith(rest, "<![CDATA[")) {
                const std::size_t end = rest.find("]]>", 9);
                if (end == std::string_view::npos) {
                    break;
                }
                characters(rest.substr(9, end - 9), false);
                consumed = end + 3;
            } else if (startsWith(rest, "<?") || startsWith(rest, "<!")) {
                const std::size_t end = rest.find('>');
                if (end == std::string_view::npos) {
                    break;
                }
                consumed = end + 1;
            } else {
                char quote = 0;
                std::size_t end = 1;
                for (; end < rest.size(); ++end) {
                    const char c = rest[end];
                    if (quote != 0) {
                        quote = c == quote ? 0 : quote;
                    } else if (c == '"' || c == '\'') {
                        quote = c;
                    } else if (c == '>') {
                        break;
                    }
                }
                if (end >= rest.size()) {
                    break;
                }
                const Tag tag = parseTag(rest.substr(1, end - 1));
                if (tag.name.empty()) {
                    fail();
                    return;
                }
                if (tag.closing) {
                    closeTag(tag.name);
                } else {
                    openTag(tag);
                    if (tag.selfClosing && !failed) {
                        closeTag(tag.name);
                    }
                }
                consumed = end + 1;
            }
            pos += consumed;
        }
        if (failed) {
            return;
        }
        pending.erase(0, pos);
        if (pending.size() > kMaxPending) {
            fail();
        }
    }

    void xml(const char* data, std::size_t size) {
        input += size;
        if (input > kMaxInput) {
            fail();
            return;
        }
        pending.append(data, size);
        parse();
    }

    void inflate(const char* data, std::size_t size) {
        z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
        z.avail_in = static_cast<uInt>(size);
        char out[64 * 1024];
        while (!failed && z.avail_in > 0) {
            z.next_out = reinterpret_cast<Bytef*>(out);
            z.avail_out = sizeof(out);
            const int rc = ::inflate(&z, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
                fail();
                return;
            }
            xml(out, sizeof(out) - z.avail_out);
            if (rc == Z_STREAM_END) {
                if (z.avail_in == 0) {
                    return;
                }
                inflateReset(&z);  // another member follows
            } else if (rc == Z_BUF_ERROR && z.avail_out != 0) {
                return;  // needs more input
            }
        }
    }

    void feed(const char* data, std::size_t size) {
        if (!decided) {
            head.append(data, size);
            if (head.size() < 2) {
                return;
            }
            decided = true;
            gzip = static_cast<unsigned char>(head[0]) == 0x1f && static_cast<unsigned char>(head[1]) == 0x8b;
            if (gzip) {
                zOpen = inflateInit2(&z, 16 + MAX_WBITS) == Z_OK;
                if (!zOpen) {
                    fail();
                    return;
                }
            }
            const std::string first = std::move(head);
            head.clear();
            feed(first.data(), first.size());
            return;
        }
        if (gzip) {
            inflate(data, size);
        } else {
            xml(data, size);
        }
    }
};

XmltvReader::XmltvReader(std::int64_t fromUtc, std::int64_t toUtc) : impl_(std::make_unique<Impl>()) {
    impl_->from = fromUtc;
    impl_->to = toUtc;
}

XmltvReader::~XmltvReader() = default;

bool XmltvReader::feed(const char* data, std::size_t size) {
    if (!impl_->failed && size > 0) {
        impl_->feed(data, size);
    }
    return !impl_->failed;
}

bool XmltvReader::failed() const { return impl_->failed; }

Guide XmltvReader::finish() {
    Guide guide = std::move(impl_->guide);
    impl_->guide = Guide{};
    std::stable_sort(guide.programs.begin(), guide.programs.end(), [](const GuideProgram& a, const GuideProgram& b) {
        return a.channel != b.channel ? a.channel < b.channel : a.startUtc < b.startUtc;
    });
    return guide;
}

}  // namespace playfin::jellyfin
