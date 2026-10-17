#include <doctest/doctest.h>

#include <string>

#include "jellyfin/json.hpp"
#include "player/profile.hpp"

using namespace playfin;
namespace json = jellyfin::json;

TEST_CASE("the device profile describes direct play, HLS transcoding and subtitles") {
    const std::string profile = player::deviceProfileJson(player::DirectPlayLimits{});
    const auto doc = json::Document::parse(profile);
    REQUIRE(doc.has_value());
    yyjson_val* root = doc->root();
    CHECK(json::integer(root, "MaxStreamingBitrate") == 40000000);
    yyjson_val* direct = json::array(root, "DirectPlayProfiles");
    REQUIRE(direct != nullptr);
    CHECK(profile.find(R"("Container":"mkv,mp4,m4v,mov,webm,ts,m2ts,avi")") != std::string::npos);
    CHECK(profile.find(R"("VideoCodec":"h264,hevc,vp9,av1,mpeg2video,mpeg4")") != std::string::npos);
    CHECK(profile.find(R"("Protocol":"hls")") != std::string::npos);
    CHECK(profile.find(R"("BreakOnNonKeyFrames":true)") != std::string::npos);
    CHECK(profile.find(R"({"Format":"pgssub","Method":"Embed"})") != std::string::npos);
    CHECK(profile.find(R"({"Format":"vtt","Method":"External"})") != std::string::npos);
    CHECK(profile.find(R"("Value":"1080")") != std::string::npos);  // height limit condition
}

TEST_CASE("the profile's height limit follows the measured limits") {
    player::DirectPlayLimits limits;
    limits.maxHeight = 720;
    CHECK(player::deviceProfileJson(limits).find(R"("Value":"720")") != std::string::npos);
}

TEST_CASE("the profile's bitrate follows the quality setting") {
    player::DirectPlayLimits limits;
    limits.maxBitrate = 4000000;
    const auto doc = json::Document::parse(player::deviceProfileJson(limits));
    REQUIRE(doc.has_value());
    CHECK(json::integer(doc->root(), "MaxStreamingBitrate") == 4000000);
}

TEST_CASE("the profile plays common audio formats directly and gets the rest as AAC") {
    const std::string profile = player::deviceProfileJson(player::DirectPlayLimits{});
    CHECK(profile.find(R"({"Type":"Audio","Container":"mp3,flac,m4a,mp4,aac,ogg,oga,opus,webm,wav")") != std::string::npos);
    CHECK(profile.find(R"("AudioCodec":"mp3,flac,aac,alac,opus,vorbis,pcm_s16le,pcm_s24le")") != std::string::npos);
    CHECK(profile.find(R"({"Type":"Audio","Container":"aac","AudioCodec":"aac","Protocol":"http","Context":"Streaming")") != std::string::npos);
}
