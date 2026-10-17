#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace playfin::player {

// A decoded picture: rgb0 bytes (R, G, B, unused), top-down rows of 4 × width bytes, at the video's display size.
struct VideoFrame {
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    std::uint64_t serial = 0;  // grows with every new picture
};

struct EngineOptions {
    std::string audioOutput;    // mpv "ao": "sdl" on the PS4, "null" in tests, empty for mpv's default
    std::string fontsDir;       // subtitles: Noto Sans and Noto Sans CJK
    std::string caFile;         // TLS verification of https streams
    bool verifyTls = true;      // false only for a server trusted by its pinned certificate (FFmpeg cannot pin)
    std::string authorization;  // the Jellyfin "Authorization" header sent with every stream request
    int decoderThreads = 0;     // 0: FFmpeg's count of the CPUs (it finds only 1 on the PS4)
    bool audioOnly = false;     // music: no video track (album art included), nothing to render
};

// Options every player gets before anything else: no configuration files, scripts or key bindings of mpv, and a
// media file can open neither other files (references, playlists) nor its own fonts (they would reach FreeType).
std::vector<std::pair<const char*, const char*>> hardeningOptions();

// True for an option an mpv build may lack with nothing lost (ytdl: mpv built without Lua, as on the PS4). Any other
// option mpv does not know is logged as a warning, so a protection renamed by a newer mpv does not go unnoticed.
bool optionMayBeMissing(const char* name);

enum class EngineEvent { FileLoaded, FirstFrame, Seeked, EndOfFile, Error };

// libmpv with its software renderer: mpv's threads demux, decode and play the sound; the main thread renders the
// pictures into a VideoFrame. Every method is for the main thread; `wake` (given at creation) may be called from
// mpv's threads and must only post work to the main thread.
class Engine {
public:
    static std::unique_ptr<Engine> create(const EngineOptions& options, std::function<void()> wake);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void load(const std::string& url, double startSeconds);
    void addSubtitle(const std::string& url, bool select);
    void setPaused(bool paused);
    void seekTo(double seconds);
    void setAudioTrack(int mpvId);     // mpv track ids start at 1
    void setSubtitleTrack(int mpvId);  // 0: no subtitles

    struct State {
        double position = 0;
        double duration = 0;
        bool paused = false;
        bool buffering = false;
        int subtitleTrack = 0;
        std::string error;  // the last load failure, empty otherwise
    };
    State state() const;

    // Events since the last call, in order (mpv's event queue is read here).
    std::vector<EngineEvent> drainEvents();

    // Renders the next picture into `frame` when mpv has one; true if `frame` changed.
    bool renderIfDue(VideoFrame& frame);

    struct Stats {
        long decoderDrops = 0;
        long outputDrops = 0;
        double renderMs = 0;  // last picture's conversion time
    };
    Stats stats() const;

    // mpv's description of the stream ("h264 1920x1080 mkv"), for the log
    std::string describe() const;

private:
    struct Impl;
    explicit Engine(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace playfin::player
