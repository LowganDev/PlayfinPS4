#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "core/task.hpp"
#include "jellyfin/models.hpp"
#include "player/engine.hpp"
#include "player/queue.hpp"
#include "player/session.hpp"

namespace playfin::app {

class AppSession;

// The music that keeps playing while the user browses: a queue of tracks, an engine without video, the reports
// to the server (one playback session per track). Owned by the AppSession; main thread; update() every frame.
class MusicPlayer {
public:
    explicit MusicPlayer(AppSession& session);
    ~MusicPlayer();
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    void play(std::vector<jellyfin::Item> tracks, int start, bool shuffle);
    void togglePause();
    void next();
    // Restarts the track after its first 3 s, else the previous track.
    void previous();
    void seekTo(double seconds);
    void jump(int offset);  // the offset-th upcoming track
    void setShuffle(bool on);
    void setRepeat(player::Repeat repeat);
    // Reports the track stopped, forgets the queue and closes the engine.
    void stop();
    // Engine events, reports, the next track when one ends.
    void update();

    bool active() const { return !queue_.empty(); }
    struct Status {
        const jellyfin::Item* track = nullptr;
        double position = 0;  // seconds
        double duration = 0;
        bool paused = false;
        bool loading = false;
    };
    Status status() const;
    const player::Queue& queue() const { return queue_; }
    // Grows whenever what Status shows changes (a track, pause, each second of the position): screens redraw then.
    unsigned changes() const { return changes_; }
    // `changed` runs after update() when something changed (once per frame at most); views listening are owners.
    void listen(const void* owner, std::function<void()> changed);
    void unlisten(const void* owner);

private:
    void startCurrent();
    void updateEngine();
    void advance(bool userAction);
    void failed(const char* why);
    jellyfin::PlaybackState playbackState() const;
    void send(player::ReportKind kind, const jellyfin::PlaybackState& state);
    std::int64_t now() const;

    AppSession& session_;
    std::unique_ptr<player::Engine> engine_;
    player::Queue queue_;
    std::unique_ptr<player::Reporter> reporter_;
    player::StreamChoice stream_;
    std::string itemId_;
    core::CancelToken token_;  // the track being loaded: cancelled when another one starts
    int failures_ = 0;         // tracks in a row that could not play
    bool loading_ = false;
    unsigned changes_ = 0;
    unsigned notified_ = 0;
    std::vector<std::pair<const void*, std::function<void()>>> listeners_;
    long lastSecond_ = -1;
    bool lastPaused_ = false;
};

}  // namespace playfin::app
