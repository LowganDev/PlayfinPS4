#include "player/engine.hpp"

#include <mpv/client.h>
#include <mpv/render.h>

#include <chrono>
#include <cstring>

#include "core/log.hpp"

namespace playfin::player {

struct Engine::Impl {
    mpv_handle* mpv = nullptr;
    mpv_render_context* render = nullptr;
    std::function<void()> wake;
    std::vector<EngineEvent> pending;
    bool loaded = false;      // a file is loaded and no picture was shown yet
    bool shownFirst = false;  // FirstFrame sent for the current file
    std::string error;
    double renderMs = 0;

    ~Impl() {
        if (render != nullptr) {
            mpv_render_context_free(render);  // before the core goes away
        }
        if (mpv != nullptr) {
            mpv_terminate_destroy(mpv);
        }
    }

    double number(const char* name) const {
        double value = 0;
        return mpv_get_property(mpv, name, MPV_FORMAT_DOUBLE, &value) >= 0 ? value : 0;
    }
    std::int64_t integer(const char* name) const {
        std::int64_t value = 0;
        return mpv_get_property(mpv, name, MPV_FORMAT_INT64, &value) >= 0 ? value : 0;
    }
    bool flag(const char* name) const {
        int value = 0;
        return mpv_get_property(mpv, name, MPV_FORMAT_FLAG, &value) >= 0 && value != 0;
    }
    std::string text(const char* name) const {
        char* value = mpv_get_property_string(mpv, name);
        std::string out = value != nullptr ? value : "";
        mpv_free(value);
        return out;
    }
    void command(std::initializer_list<const char*> words) {
        std::vector<const char*> args(words);
        args.push_back(nullptr);
        const int rc = mpv_command_async(mpv, 0, args.data());
        if (rc < 0) {
            PF_LOG_WARN("mpv: %s failed: %s", args[0], mpv_error_string(rc));
        }
    }
};

namespace {

void setOption(mpv_handle* mpv, const char* name, const std::string& value) {
    const int rc = mpv_set_option_string(mpv, name, value.c_str());
    if (rc == MPV_ERROR_OPTION_NOT_FOUND && optionMayBeMissing(name)) {
        PF_LOG_DEBUG("mpv: option %s not in this build", name);
    } else if (rc < 0) {
        PF_LOG_WARN("mpv: option %s refused: %s", name, mpv_error_string(rc));
    }
}

}  // namespace

bool optionMayBeMissing(const char* name) { return std::strcmp(name, "ytdl") == 0; }

std::vector<std::pair<const char*, const char*>> hardeningOptions() {
    return {{"config", "no"},
            {"load-scripts", "no"},
            {"ytdl", "no"},
            {"input-default-bindings", "no"},
            {"embeddedfonts", "no"},
            {"access-references", "no"},
            {"load-unsafe-playlists", "no"}};
}

std::unique_ptr<Engine> Engine::create(const EngineOptions& options, std::function<void()> wake) {
    auto impl = std::make_unique<Impl>();
    impl->wake = std::move(wake);
    impl->mpv = mpv_create();
    if (impl->mpv == nullptr) {
        PF_LOG_ERROR("mpv: cannot create the player");
        return nullptr;
    }
    mpv_handle* mpv = impl->mpv;
    for (const auto& option : hardeningOptions()) {
        setOption(mpv, option.first, option.second);
    }
    setOption(mpv, "terminal", "no");
    setOption(mpv, "idle", "yes");
    setOption(mpv, "vo", "libmpv");
    if (options.audioOnly) {
        setOption(mpv, "vid", "no");
        setOption(mpv, "audio-display", "no");
    }
    setOption(mpv, "hwdec", "no");
    setOption(mpv, "osd-level", "0");
    setOption(mpv, "cache", "yes");
    setOption(mpv, "demuxer-max-bytes", "64MiB");
    setOption(mpv, "demuxer-readahead-secs", "20");
    // the picture is converted at its own size: swscale's fast paths (SIMD) instead of its accurate C path,
    // which cost 110 ms per 1080p picture on the PS4 (3 ms with these)
    setOption(mpv, "sws-fast", "yes");
    setOption(mpv, "sws-scaler", "fast-bilinear");
    if (options.decoderThreads > 0) {
        setOption(mpv, "vd-lavc-threads", std::to_string(options.decoderThreads));
    }
    if (!options.audioOutput.empty()) {
        setOption(mpv, "ao", options.audioOutput);
    }
    if (!options.fontsDir.empty()) {
        setOption(mpv, "sub-fonts-dir", options.fontsDir);
        setOption(mpv, "sub-font", "Noto Sans");
        setOption(mpv, "sub-font-provider", "none");
    }
    setOption(mpv, "tls-verify", options.verifyTls ? "yes" : "no");
    if (!options.caFile.empty()) {
        setOption(mpv, "tls-ca-file", options.caFile);
    }
    if (!options.authorization.empty()) {
        // a node list: the header value contains commas, which the string form would split
        std::string header = "Authorization: " + options.authorization;
        mpv_node item;
        item.format = MPV_FORMAT_STRING;
        item.u.string = header.data();
        mpv_node_list list;
        list.num = 1;
        list.values = &item;
        list.keys = nullptr;
        mpv_node node;
        node.format = MPV_FORMAT_NODE_ARRAY;
        node.u.list = &list;
        if (mpv_set_option(mpv, "http-header-fields", MPV_FORMAT_NODE, &node) < 0) {
            PF_LOG_WARN("mpv: the authorization header was refused");
        }
    }
    mpv_request_log_messages(mpv, "warn");
    const int rc = mpv_initialize(mpv);
    if (rc < 0) {
        PF_LOG_ERROR("mpv: initialisation failed: %s", mpv_error_string(rc));
        return nullptr;
    }
    mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_SW)},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (mpv_render_context_create(&impl->render, mpv, params) < 0) {
        PF_LOG_ERROR("mpv: cannot create the software renderer");
        return nullptr;
    }
    Impl* raw = impl.get();
    mpv_set_wakeup_callback(
        mpv, [](void* data) { static_cast<Impl*>(data)->wake(); }, raw);
    mpv_render_context_set_update_callback(
        impl->render, [](void* data) { static_cast<Impl*>(data)->wake(); }, raw);
    return std::unique_ptr<Engine>(new Engine(std::move(impl)));
}

Engine::Engine(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Engine::~Engine() = default;

void Engine::load(const std::string& url, double startSeconds) {
    // "start" before loadfile: the loadfile argument list changed between mpv versions
    mpv_set_property_string(impl_->mpv, "start", startSeconds > 0 ? std::to_string(startSeconds).c_str() : "none");
    impl_->loaded = false;
    impl_->shownFirst = false;
    impl_->error.clear();
    impl_->command({"loadfile", url.c_str(), "replace"});
}

void Engine::addSubtitle(const std::string& url, bool select) {
    impl_->command({"sub-add", url.c_str(), select ? "select" : "auto"});
}

void Engine::setPaused(bool paused) {
    int value = paused ? 1 : 0;
    mpv_set_property(impl_->mpv, "pause", MPV_FORMAT_FLAG, &value);
}

void Engine::seekTo(double seconds) {
    const std::string target = std::to_string(seconds);
    impl_->command({"seek", target.c_str(), "absolute"});
}

void Engine::setAudioTrack(int mpvId) {
    mpv_set_property_string(impl_->mpv, "aid", mpvId > 0 ? std::to_string(mpvId).c_str() : "no");
}

void Engine::setSubtitleTrack(int mpvId) {
    mpv_set_property_string(impl_->mpv, "sid", mpvId > 0 ? std::to_string(mpvId).c_str() : "no");
}

Engine::State Engine::state() const {
    State s;
    s.position = impl_->number("time-pos");
    s.duration = impl_->number("duration");
    s.paused = impl_->flag("pause");
    s.buffering = impl_->flag("paused-for-cache");
    s.subtitleTrack = static_cast<int>(impl_->integer("sid"));
    s.error = impl_->error;
    return s;
}

std::vector<EngineEvent> Engine::drainEvents() {
    for (;;) {
        mpv_event* event = mpv_wait_event(impl_->mpv, 0);
        if (event == nullptr || event->event_id == MPV_EVENT_NONE) {
            break;
        }
        switch (event->event_id) {
        case MPV_EVENT_LOG_MESSAGE: {
            const auto* message = static_cast<mpv_event_log_message*>(event->data);
            std::string line = message->text != nullptr ? message->text : "";
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            PF_LOG_WARN("mpv [%s]: %s", message->prefix != nullptr ? message->prefix : "", line.c_str());
            break;
        }
        case MPV_EVENT_FILE_LOADED:
            impl_->loaded = true;
            impl_->pending.push_back(EngineEvent::FileLoaded);
            break;
        case MPV_EVENT_PLAYBACK_RESTART:
            impl_->pending.push_back(EngineEvent::Seeked);
            break;
        case MPV_EVENT_END_FILE: {
            const auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (end->reason == MPV_END_FILE_REASON_EOF) {
                impl_->pending.push_back(EngineEvent::EndOfFile);
            } else if (end->reason == MPV_END_FILE_REASON_ERROR) {
                impl_->error = mpv_error_string(end->error);
                PF_LOG_WARN("player: the file cannot be played: %s", impl_->error.c_str());
                impl_->pending.push_back(EngineEvent::Error);
            }
            break;
        }
        default:
            break;
        }
    }
    std::vector<EngineEvent> out;
    out.swap(impl_->pending);
    return out;
}

bool Engine::renderIfDue(VideoFrame& frame) {
    if ((mpv_render_context_update(impl_->render) & MPV_RENDER_UPDATE_FRAME) == 0) {
        return false;
    }
    const auto width = static_cast<int>(impl_->integer("dwidth"));
    const auto height = static_cast<int>(impl_->integer("dheight"));
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        return false;
    }
    const std::size_t stride = static_cast<std::size_t>(width) * 4;
    frame.pixels.resize(stride * static_cast<std::size_t>(height));
    int size[2] = {width, height};
    char format[] = "rgb0";
    std::size_t strideParam = stride;
    int block = 0;  // never wait for the picture's display time: the frame loop must keep drawing the UI
    mpv_render_param params[] = {{MPV_RENDER_PARAM_SW_SIZE, size},
                                 {MPV_RENDER_PARAM_SW_FORMAT, format},
                                 {MPV_RENDER_PARAM_SW_STRIDE, &strideParam},
                                 {MPV_RENDER_PARAM_SW_POINTER, frame.pixels.data()},
                                 {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};
    const auto start = std::chrono::steady_clock::now();
    const int rc = mpv_render_context_render(impl_->render, params);
    impl_->renderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (rc < 0) {
        PF_LOG_WARN("mpv: render failed: %s", mpv_error_string(rc));
        return false;
    }
    frame.width = width;
    frame.height = height;
    ++frame.serial;
    if (impl_->loaded && !impl_->shownFirst) {
        impl_->shownFirst = true;
        impl_->pending.push_back(EngineEvent::FirstFrame);
    }
    return true;
}

Engine::Stats Engine::stats() const {
    Stats s;
    s.decoderDrops = static_cast<long>(impl_->integer("decoder-frame-drop-count"));
    s.outputDrops = static_cast<long>(impl_->integer("frame-drop-count"));
    s.renderMs = impl_->renderMs;
    return s;
}

std::string Engine::describe() const {
    return impl_->text("video-codec") + " " + impl_->text("width") + "x" + impl_->text("height") + " " +
           impl_->text("file-format");
}

}  // namespace playfin::player
