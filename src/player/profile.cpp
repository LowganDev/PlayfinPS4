#include "player/profile.hpp"

#include "jellyfin/json.hpp"

namespace playfin::player {

namespace {


void condition(jellyfin::json::Writer& w, const char* property, const char* comparison, const std::string& value) {
    w.object();
    w.key("Condition").value(comparison);
    w.key("Property").value(property);
    w.key("Value").value(value);
    w.key("IsRequired").value(false);
    w.end();
}

}  // namespace

std::string deviceProfileJson(const DirectPlayLimits& limits) {
    jellyfin::json::Writer w;
    w.object();
    w.key("Name").value("Playfin");
    w.key("MaxStreamingBitrate").value(limits.maxBitrate);
    w.key("MaxStaticBitrate").value(limits.maxBitrate);
    w.key("MusicStreamingTranscodingBitrate").value(static_cast<std::int64_t>(320000));

    w.key("DirectPlayProfiles").array();
    w.object();
    w.key("Type").value("Video");
    w.key("Container").value("mkv,mp4,m4v,mov,webm,ts,m2ts,avi");
    w.key("VideoCodec").value("h264,hevc,vp9,av1,mpeg2video,mpeg4");
    w.key("AudioCodec").value("aac,mp3,ac3,eac3,dts,truehd,flac,alac,opus,vorbis,pcm_s16le,pcm_s24le,mp2");
    w.end();
    w.object();
    w.key("Type").value("Audio");
    w.key("Container").value("mp3,flac,m4a,mp4,aac,ogg,oga,opus,webm,wav");
    w.key("AudioCodec").value("mp3,flac,aac,alac,opus,vorbis,pcm_s16le,pcm_s24le");
    w.end();
    w.end();

    w.key("TranscodingProfiles").array();
    w.object();
    w.key("Type").value("Video");
    w.key("Container").value("ts");
    w.key("Protocol").value("hls");
    w.key("VideoCodec").value("h264");
    w.key("AudioCodec").value("aac,ac3");
    w.key("Context").value("Streaming");
    w.key("MaxAudioChannels").value("6");
    w.key("MinSegments").value(static_cast<std::int64_t>(1));
    w.key("BreakOnNonKeyFrames").value(true);
    w.end();
    w.object();  // music in another format: AAC over plain HTTP
    w.key("Type").value("Audio");
    w.key("Container").value("aac");
    w.key("AudioCodec").value("aac");
    w.key("Protocol").value("http");
    w.key("Context").value("Streaming");
    w.key("MaxAudioChannels").value("2");
    w.end();
    w.end();

    w.key("CodecProfiles").array();
    w.object();
    w.key("Type").value("Video");
    w.key("Conditions").array();
    condition(w, "Width", "LessThanEqual", std::to_string(limits.maxWidth));
    condition(w, "Height", "LessThanEqual", std::to_string(limits.maxHeight));
    w.end();
    w.end();
    w.end();

    w.key("SubtitleProfiles").array();
    for (const char* format : {"ass", "ssa", "srt", "subrip", "vtt", "webvtt", "mov_text", "pgssub", "dvdsub", "dvbsub"}) {
        w.object().key("Format").value(format).key("Method").value("Embed").end();
    }
    for (const char* format : {"srt", "ass", "ssa", "vtt"}) {
        w.object().key("Format").value(format).key("Method").value("External").end();
    }
    w.end();

    w.key("ContainerProfiles").array().end();
    w.key("ResponseProfiles").array().end();
    w.end();
    return w.str();
}

}  // namespace playfin::player
