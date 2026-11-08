#include <doctest/doctest.h>

#include <zlib.h>

#include <string>

#include "jellyfin/xmltv.hpp"

using namespace playfin::jellyfin;

namespace {

const char* kGuide = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE tv SYSTEM "xmltv.dtd">
<tv generator-info-name="test">
  <!-- a comment with <tags> inside -->
  <channel id="tf1.fr">
    <display-name lang="fr">TF1 HD</display-name>
    <display-name>1</display-name>
    <icon src="http://example/tf1.png"/>
  </channel>
  <channel id='arte.fr'><display-name>Arte</display-name></channel>
  <programme start="20261108200000 +0100" stop="20261108210000 +0100" channel="tf1.fr">
    <title lang="fr">Le Journal &amp; la m&#233;t&#xE9;o</title>
    <title lang="en">News</title>
    <desc><![CDATA[Les <titres> du jour.]]></desc>
    <category>News</category>
  </programme>
  <programme start="20261108190000 +0100" stop="20261108200000 +0100" channel="tf1.fr">
    <title>Avant</title>
  </programme>
  <programme start="20261101000000 +0000" stop="20261101010000 +0000" channel="arte.fr">
    <title>Old, outside the window</title>
  </programme>
  <programme start="20261108200000" stop="20261108223000" channel="arte.fr">
    <title>Film</title>
  </programme>
</tv>
)";

const std::int64_t kFrom = parseXmltvTime("20261108000000 +0000");
const std::int64_t kTo = parseXmltvTime("20261110000000 +0000");

std::string gzip(const std::string& text) {
    z_stream z{};
    deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    std::string out(compressBound(static_cast<uLong>(text.size())) + 64, '\0');
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(text.data()));
    z.avail_in = static_cast<uInt>(text.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = static_cast<uInt>(out.size());
    deflate(&z, Z_FINISH);
    out.resize(z.total_out);
    deflateEnd(&z);
    return out;
}

}  // namespace

TEST_CASE("XMLTV times: local with an offset, UTC without one") {
    CHECK(parseXmltvTime("20261108200000 +0100") == parseXmltvTime("20261108190000 +0000"));
    CHECK(parseXmltvTime("20261108190000") == parseXmltvTime("20261108190000 +0000"));
    CHECK(parseXmltvTime("202611081900 -0230") == parseXmltvTime("20261108213000"));
    CHECK(parseXmltvTime("19700101000100 +0000") == 60);
    CHECK(parseXmltvTime("") == 0);
    CHECK(parseXmltvTime("2026") == 0);
    CHECK(parseXmltvTime("20261340190000") == 0);
}

TEST_CASE("channel keys: case, spaces, punctuation and the HD suffix do not count") {
    CHECK(channelKey("TF1 HD") == "tf1");
    CHECK(channelKey("tf1") == "tf1");
    CHECK(channelKey("France 2 (FHD)") == "france2");
    CHECK(channelKey("HD") == "hd");  // nothing would be left
    CHECK(channelKey("Canal+ 4K") == "canal");
}

TEST_CASE("an XMLTV guide: channels, programmes in the window, entities and CDATA, by channel then start") {
    XmltvReader reader(kFrom, kTo);
    const std::string text = kGuide;
    for (std::size_t i = 0; i < text.size(); i += 7) {  // in small pieces, as it downloads
        REQUIRE(reader.feed(text.data() + i, std::min<std::size_t>(7, text.size() - i)));
    }
    const Guide guide = reader.finish();
    REQUIRE(guide.channels.size() == 2);
    CHECK(guide.channels[0].id == "tf1.fr");
    REQUIRE(guide.channels[0].names.size() == 2);
    CHECK(guide.channels[0].names[0] == "TF1 HD");
    CHECK(guide.channels[0].names[1] == "1");
    CHECK(guide.channels[1].id == "arte.fr");
    REQUIRE(guide.programs.size() == 3);  // the old one left out
    CHECK(guide.programs[0].channel == "arte.fr");
    CHECK(guide.programs[0].title == "Film");
    CHECK(guide.programs[1].title == "Avant");
    CHECK(guide.programs[2].title == "Le Journal & la m\xC3\xA9t\xC3\xA9o");  // the first title only
    CHECK(guide.programs[2].description == "Les <titres> du jour.");
    CHECK(guide.programs[2].startUtc == parseXmltvTime("20261108190000"));
    CHECK(guide.programs[2].endUtc == parseXmltvTime("20261108200000"));
}

TEST_CASE("an XMLTV guide compressed with gzip reads the same") {
    XmltvReader reader(kFrom, kTo);
    const std::string packed = gzip(kGuide);
    REQUIRE(reader.feed(packed.data(), 10));
    REQUIRE(reader.feed(packed.data() + 10, packed.size() - 10));
    const Guide guide = reader.finish();
    CHECK(guide.channels.size() == 2);
    CHECK(guide.programs.size() == 3);
}

TEST_CASE("what is not an XMLTV guide stops the reading") {
    XmltvReader html(kFrom, kTo);
    html.feed("<html><body>Not found</body></html>", 35);
    CHECK(html.finish().channels.empty());
    XmltvReader endless(kFrom, kTo);
    const std::string text(XmltvReader::kMaxPending + 10, 'a');
    CHECK_FALSE(endless.feed(text.data(), text.size()));
    CHECK(endless.failed());
    XmltvReader broken(kFrom, kTo);
    const char bad[] = "\x1f\x8b\x08\x00garbage-garbage-garbage";
    CHECK_FALSE(broken.feed(bad, sizeof(bad) - 1));
}

TEST_CASE("long titles are cut on a character boundary") {
    XmltvReader reader(kFrom, kTo);
    std::string title;
    for (int i = 0; i < 1500; ++i) {
        title += "\xC3\xA9";  // é, 2 bytes
    }
    const std::string text = R"(<tv><programme start="20261108200000" stop="20261108210000" channel="c"><title>)" +
                             title + "</title></programme></tv>";
    reader.feed(text.data(), text.size());
    const Guide guide = reader.finish();
    REQUIRE(guide.programs.size() == 1);
    CHECK(guide.programs[0].title.size() <= XmltvReader::kMaxText);
    CHECK(guide.programs[0].title.size() % 2 == 0);
}
