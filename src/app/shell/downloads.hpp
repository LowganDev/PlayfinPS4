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
#include "jellyfin/models.hpp"

namespace playfin::app {

class AppSession;

// Films and episodes kept on the console (in the data folder's downloads/, never elsewhere), played without the
// server. One file at a time, in the order asked; the list is kept in downloads.json. Owned by the AppSession;
// main thread.
class Downloads {
public:
    enum class State { Queued, Running, Done, Failed };
    struct Entry {
        jellyfin::Item item;       // what the cards and the player need (name, series, numbers, length)
        std::string file;          // name in downloads/ ("<id>.mkv")
        std::string image;         // its picture in downloads/ ("<id>.jpg"), empty without one
        std::int64_t size = 0;     // bytes expected (0: unknown until done)
        std::int64_t received = 0;
        State state = State::Queued;
        std::string error;         // why it failed, for the user
        std::int64_t positionTicks = 0;  // where it was left, played offline
    };

    explicit Downloads(AppSession& session);
    ~Downloads();
    Downloads(const Downloads&) = delete;
    Downloads& operator=(const Downloads&) = delete;

    // downloads.json in the data folder (nothing is kept without one)
    void load();

    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(const std::string& id) const;
    // the file of a finished download (absolute), else nullopt
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

    // `changed` runs when an entry changes (progress at most once per percent); listeners are owners.
    void listen(const void* owner, std::function<void()> changed);
    void unlisten(const void* owner);

    // At exit: the running download is cancelled (it stays queued for the next start).
    void stop();

    // Bytes free where downloads go (0 when unknown).
    static std::int64_t freeBytes();
    // What the queue asks before and during a transfer (freeBytes; tests replace it). Called on the transfer thread.
    std::function<std::int64_t()> freeSpace = &Downloads::freeBytes;

private:
    void startNext();
    void finished(const std::string& id, core::Result<core::Unit> result, std::int64_t received);
    void progress(const std::string& id, std::int64_t received, std::int64_t total);
    void save() const;
    void notify();
    Entry* entry(const std::string& id);

    AppSession& session_;
    std::vector<Entry> entries_;
    std::string running_;     // id of the download on its way (until its job ends, even when removed meanwhile)
    core::CancelToken token_;  // cancels it
    std::vector<std::pair<const void*, std::function<void()>>> listeners_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace playfin::app
