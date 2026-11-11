#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/task.hpp"
#include "jellyfin/client.hpp"
#include "jellyfin/models.hpp"
#include "net/ftp.hpp"

namespace playfin::app {

class AppSession;

// A download's speed over its last seconds (kWindowMs), from samples of the bytes received and the time; known once
// the samples span kMinSpanMs, so the first second's burst does not count alone.
class TransferRate {
public:
    static constexpr std::int64_t kWindowMs = 10000;
    static constexpr std::int64_t kMinSpanMs = 2000;
    void sample(std::int64_t bytes, std::int64_t ms);
    void reset() { samples_.clear(); }
    double bytesPerSecond() const;  // 0 while unknown
    // seconds until `size` bytes at this speed (rounded up), -1 when the size or the speed is not known
    std::int64_t secondsLeft(std::int64_t received, std::int64_t size) const;

private:
    std::vector<std::pair<std::int64_t, std::int64_t>> samples_;  // (ms, bytes), oldest first
};

// "< 1 min", "19 min", "1 h 05" (rounded up to the minute)
std::string durationText(std::int64_t seconds);
// "1,2 Go", "340 Mo" (decimal units, as the console shows its storage)
std::string sizeText(std::int64_t bytes);
// "4,5 Mo/s", "12 Mo/s" (decimal megabytes, one decimal under 10)
std::string speedText(double bytesPerSecond);

// Films and episodes kept on the console (in the data folder's downloads/) or on a USB drive (in its Playfin/ folder,
// through the console's FTP server; their pictures stay on the console), played without the server. One file at a
// time, in the order asked; the list is kept in downloads.json. Where a download goes is the settings'
// downloadTarget when it starts. Owned by the AppSession; main thread.
class Downloads {
public:
    enum class State { Queued, Running, Paused, Done, Failed };
    struct Entry {
        jellyfin::Item item;       // what the cards and the player need (name, series, numbers, length)
        std::string file;          // name in downloads/ ("<id>.mkv")
        std::string image;         // its picture in downloads/ ("<id>.jpg"), empty without one
        std::int64_t size = 0;     // bytes expected (0: unknown until done)
        std::int64_t received = 0;
        State state = State::Queued;
        std::string error;         // why it failed, for the user
        std::int64_t positionTicks = 0;  // where it was left, played offline
        std::string drive;               // "" on the console, else the USB drive it is on ("/mnt/usb0")
        // while it runs (not saved): its speed (bytes a second, 0 unknown) and the time left (-1 unknown)
        double bytesPerSecond = 0;
        std::int64_t secondsLeft = -1;
    };
    // "Téléchargement… 9 % · reste 19 min" (with `speed`: "… 9 % · 4,5 Mo/s · reste 19 min"); what it knows only
    static std::string runningText(const Entry& entry, bool speed);
    // "En pause · 12 %"
    static std::string pausedText(const Entry& entry);
    // what the Downloads tab and the finished message call it: a film's name, an episode's series, season and number
    static std::string title(const jellyfin::Item& item);

    explicit Downloads(AppSession& session);
    ~Downloads();
    Downloads(const Downloads&) = delete;
    Downloads& operator=(const Downloads&) = delete;

    // downloads.json in the data folder (nothing is kept without one)
    void load();

    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(const std::string& id) const;
    // the file of a finished download (absolute, or "pfusb://…" on a USB drive), else nullopt
    std::optional<std::string> localFile(const std::string& id) const;
    // "file://…" of its picture, empty without one (AppSession::fetchImage reads it)
    std::string localImageUrl(const std::string& id) const;

    // Queued (once); the next one starts when nothing runs. A film or an episode with an id.
    void add(const jellyfin::Item& item);
    // Stops it if it runs, removes its files and its entry.
    void remove(const std::string& id);
    void setPosition(const std::string& id, std::int64_t ticks);
    // A failed one queued again.
    void retry(const std::string& id);
    // Stops a running or waiting one and keeps what it got (its part file); nothing starts it until resume().
    void pause(const std::string& id);
    // A paused one waits again, then goes on from where it was (the server sends the rest).
    void resume(const std::string& id);
    // Moves one by `delta` (-1 up, 1 down) among those not finished, never above the one running.
    void move(const std::string& id, int delta);
    // The id of the download on its way, empty when none.
    const std::string& runningId() const { return running_; }

    // `changed` runs when an entry changes (progress at most once per percent); listeners are owners.
    void listen(const void* owner, std::function<void()> changed);
    void unlisten(const void* owner);

    // At exit: the running download is stopped, keeping its part file (it goes on from there at the next start).
    void stop();

    // Bytes free where downloads go (0 when unknown).
    static std::int64_t freeBytes();
    // What the queue asks before and during a transfer (freeBytes; tests replace it). Called on the transfer thread.
    std::function<std::int64_t()> freeSpace = &Downloads::freeBytes;

private:
    struct Outcome {
        core::Result<core::Unit> result = core::Unit{};
        std::int64_t received = 0;
        std::string drive;  // the USB drive it went to, empty on the console
    };
    void startNext();
    void finished(const std::string& id, Outcome outcome);
    // the item's file to `fileName` in the first USB drive's Playfin/ folder (through `.part`, renamed when complete)
    static core::Result<core::Unit> toDrive(const jellyfin::Client& client, const net::FtpClient& ftp,
                                            const std::string& id, const std::string& fileName,
                                            const std::function<void(long long, long long)>& progress,
                                            const core::CancelToken& token, std::string& drive,
                                            std::int64_t& received, const std::atomic<bool>& keepPart);
    void progress(const std::string& id, std::int64_t received, std::int64_t total);
    TransferRate rate_;  // the running download's
    void save() const;
    void notify();
    Entry* entry(const std::string& id);

    AppSession& session_;
    std::vector<Entry> entries_;
    std::string running_;     // id of the download on its way (until its job ends, even when removed meanwhile)
    core::CancelToken token_;  // cancels it
    std::shared_ptr<std::atomic<bool>> keepPart_;
    bool stopping_ = false;  // the app closes: nothing starts any more  // when it is cancelled: its part file stays (paused, closing)
    std::vector<std::pair<const void*, std::function<void()>>> listeners_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace playfin::app
