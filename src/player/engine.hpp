#pragma once

#include <cstddef>
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

// A file Playfin reads itself for the player (a file on a USB drive, through the console's FTP server): blocking
// reads from mpv's threads, a seek, its size (-1 unknown); cancel() (any thread) makes a waiting read give up.
class StreamReader {
public:
    virtual ~StreamReader() = default;
    virtual long long size() = 0;
    virtual long long read(char* buffer, std::size_t size) = 0;  // 0 at the end, -1 on a failure
    virtual bool seek(long long offset) = 0;
    virtual void cancel() = 0;
};
// "pfusb://<path>" opened: the reader of <path>, else null.
using StreamOpener = std::function<std::unique_ptr<StreamReader>(const std::string& path)>;
constexpr const char* kUsbScheme = "pfusb://";

struct EngineOptions {
    std::string audioOutput;    // mpv "ao": "sdl" on the PS4, "null" in tests, empty for mpv's default
    std::string fontsDir;       // subtitles: Noto Sans and Noto Sans CJK
    std::string caFile;         // TLS verification of https streams
    bool verifyTls = true;      // false only for a server trusted by its pinned certificate (FFmpeg cannot pin)
    std::string authorization;  // the Jellyfin "Authorization" header sent with every stream request
    int decoderThreads = 0;     // 0: FFmpeg's count of the CPUs (it finds only 1 on the PS4)
    bool audioOnly = false;     // music: no video track (album art included), nothing to render
    bool reconnect = false;     // live: FFmpeg reconnects an http stream that drops
    StreamOpener usb;           // "pfusb://" files (a USB drive), none without it
    double subtitleScale = 1.0;      // text subtitles' size (0.75 small to 1.5 very large)
    bool subtitleBackground = false;  // a dark box behind text subtitles (read better on a bright picture)
};

// Options every player gets before anything else: no configuration files, scripts or key bindings of mpv, and a
// media file can open neither other files (references, playlists) nor its own fonts (they would reach FreeType).
std::vector<std::pair<const char*, const char*>> hardeningOptions();

// An HLS playlist over http(s) (a live channel, a transcode): its segments are references the demuxer has to open, so
// the player follows references for it alone (FFmpeg's HLS demuxer opens only segments of the playlist's protocol
// family). Every other file keeps "access-references" off.
bool followsReferences(const std::string& url);

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
    void setVolume(int percent);       // 0 to 100
    // 0.5 to 2 (clamped); the voice keeps its pitch (mpv's default). Above what the device decodes, mpv drops
    // pictures and the sound stays in step.
    void setSpeed(double speed);
    double speed() const;

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

    // The file's audio and subtitle tracks as mpv sees them (those of a file added with addSubtitle() too).
    struct Track {
        int id = 0;            // mpv's id within its type
        bool audio = false;    // else a subtitle
        std::string title;     // the track's title, else its file's name for an added file
        std::string language;  // "fre", "en"…
        bool selected = false;
    };
    std::vector<Track> tracks() const;
    // A tag of the file playing ("title", "artist", "album"), empty without it.
    std::string tag(const char* name) const;

private:
    struct Impl;
    explicit Engine(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace playfin::player
