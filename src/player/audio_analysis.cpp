#include "player/audio_analysis.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <cstdint>
#include <utility>

#include "core/log.hpp"

namespace playfin::player {

namespace {

// everything FFmpeg allocates, freed whatever way the decoding ends
struct Decoder {
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* resampler = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    ~Decoder() {
        av_frame_free(&frame);
        av_packet_free(&packet);
        swr_free(&resampler);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
};

int interrupted(void* flag) { return static_cast<std::atomic<bool>*>(flag)->load() ? 1 : 0; }

}  // namespace

AudioAnalysis::AudioAnalysis(Source source) {
    thread_ = std::thread([this, source = std::move(source)] { run(source); });
}

AudioAnalysis::~AudioAnalysis() {
    stop_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::optional<SpectrumFrame> AudioAnalysis::at(double seconds) const {
    if (seconds < 0) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(seconds * kFramesPerSecond);
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= frames_.size()) {
        return std::nullopt;
    }
    return frames_[index];
}

void AudioAnalysis::run(const Source& source) {
    Decoder d;
    d.format = avformat_alloc_context();
    if (d.format == nullptr) {
        failed_ = true;
        return;
    }
    d.format->interrupt_callback.callback = interrupted;
    d.format->interrupt_callback.opaque = &stop_;
    AVDictionary* options = nullptr;
    if (!source.authorization.empty()) {
        av_dict_set(&options, "headers", ("Authorization: " + source.authorization + "\r\n").c_str(), 0);
    }
    av_dict_set(&options, "tls_verify", source.verifyTls ? "1" : "0", 0);
    if (!source.caFile.empty()) {
        av_dict_set(&options, "ca_file", source.caFile.c_str(), 0);
    }
    av_dict_set(&options, "rw_timeout", "15000000", 0);  // µs
    const int opened = avformat_open_input(&d.format, source.url.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (opened < 0) {  // avformat_open_input freed the context
        d.format = nullptr;
        if (!stop_) {
            PF_LOG_WARN("visualizer: the track cannot be opened (%d)", opened);
        }
        failed_ = true;
        return;
    }
    if (avformat_find_stream_info(d.format, nullptr) < 0) {
        failed_ = true;
        return;
    }
    const AVCodec* codec = nullptr;
    const int stream = av_find_best_stream(d.format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (stream < 0 || codec == nullptr) {
        failed_ = true;
        return;
    }
    d.codec = avcodec_alloc_context3(codec);
    if (d.codec == nullptr ||
        avcodec_parameters_to_context(d.codec, d.format->streams[stream]->codecpar) < 0 ||
        avcodec_open2(d.codec, codec, nullptr) < 0 || d.codec->ch_layout.nb_channels <= 0) {
        failed_ = true;
        return;
    }
    AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
    if (swr_alloc_set_opts2(&d.resampler, &mono, AV_SAMPLE_FMT_FLT, kRate, &d.codec->ch_layout,
                            d.codec->sample_fmt, d.codec->sample_rate, 0, nullptr) < 0 ||
        swr_init(d.resampler) < 0) {
        failed_ = true;
        return;
    }
    d.packet = av_packet_alloc();
    d.frame = av_frame_alloc();
    if (d.packet == nullptr || d.frame == nullptr) {
        failed_ = true;
        return;
    }
    SpectrumBuilder builder(kRate);
    std::vector<float> samples;
    const auto publish = [this, &builder] {
        std::vector<SpectrumFrame> made = builder.take();
        std::lock_guard<std::mutex> lock(mutex_);
        frames_.insert(frames_.end(), made.begin(), made.end());
        return frames_.size() < kMaxFrames;
    };
    const auto drain = [&]() {
        while (avcodec_receive_frame(d.codec, d.frame) == 0) {
            const int room = swr_get_out_samples(d.resampler, d.frame->nb_samples);
            if (room > 0) {
                samples.resize(static_cast<std::size_t>(room));
                auto* out = reinterpret_cast<std::uint8_t*>(samples.data());
                const int made = swr_convert(d.resampler, &out, room,
                                             const_cast<const std::uint8_t**>(d.frame->extended_data),
                                             d.frame->nb_samples);
                if (made > 0) {
                    builder.add(samples.data(), static_cast<std::size_t>(made));
                }
            }
            av_frame_unref(d.frame);
        }
    };
    while (!stop_ && av_read_frame(d.format, d.packet) >= 0) {
        if (d.packet->stream_index == stream && avcodec_send_packet(d.codec, d.packet) == 0) {
            drain();
        }
        av_packet_unref(d.packet);
        if (!publish()) {
            break;
        }
    }
    avcodec_send_packet(d.codec, nullptr);  // the decoder's last frames
    drain();
    publish();
    finished_ = true;
}

}  // namespace playfin::player
