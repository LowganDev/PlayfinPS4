#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace playfin::jellyfin {

// An XMLTV guide (the TV listings format of IPTV providers), read on the console when the server has no guide.
struct GuideChannel {
    std::string id;
    std::vector<std::string> names;  // its display-names (a name, a number…)
};

struct GuideProgram {
    std::string channel;  // a GuideChannel id
    std::int64_t startUtc = 0;
    std::int64_t endUtc = 0;
    std::string title;
    std::string description;
};

struct Guide {
    std::vector<GuideChannel> channels;
    std::vector<GuideProgram> programs;  // by channel, then start
};

// Reads an XMLTV file as it downloads (plain or gzip, told by its first bytes) and keeps the programmes that overlap
// [fromUtc, toUtc]. Untrusted input: sizes are bounded (texts, tags, counts, the whole file once decompressed); a
// file past a bound, a broken gzip or something that is not XML stops the reading.
class XmltvReader {
public:
    static constexpr std::size_t kMaxChannels = 5000;
    static constexpr std::size_t kMaxPrograms = 200000;
    static constexpr std::size_t kMaxText = 2000;                     // bytes kept of a title or a description
    static constexpr std::size_t kMaxPending = 1024 * 1024;           // a tag or a text longer than this: not XMLTV
    static constexpr std::size_t kMaxInput = 1024ULL * 1024 * 1024;  // bytes of XML (decompressed)

    XmltvReader(std::int64_t fromUtc, std::int64_t toUtc);
    ~XmltvReader();
    XmltvReader(const XmltvReader&) = delete;
    XmltvReader& operator=(const XmltvReader&) = delete;

    // False once the reading stopped (then the rest is ignored).
    bool feed(const char* data, std::size_t size);
    bool failed() const;
    // The guide read so far (programmes sorted); call once, after the last feed.
    Guide finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// "20261108203000 +0100" (seconds and offset optional) as seconds since 1970 UTC; 0 when it is not one.
std::int64_t parseXmltvTime(std::string_view text);

// A channel name to compare with: lower-case ASCII letters and digits (other bytes kept), without a trailing
// "hd", "fhd", "uhd", "sd" or "4k" when something is left ("TF1 HD" and "tf1" compare equal).
std::string channelKey(std::string_view name);

}  // namespace playfin::jellyfin
