#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/task.hpp"
#include "jellyfin/models.hpp"
#include "jellyfin/xmltv.hpp"

namespace playfin::app {

class AppSession;

// The TV guide read on the console from the XMLTV address of the settings (for servers without a guide): downloaded
// at start and when the address changes, kept in memory. The server's own guide comes first; this one fills the
// channels it has nothing for. Owned by the AppSession; main thread.
class TvGuide {
public:
    enum class State { Off, Loading, Ready, Failed };
    static constexpr std::int64_t kPastSeconds = 6 * 3600;     // kept before now
    static constexpr std::int64_t kAheadSeconds = 4 * 86400;  // kept after now

    explicit TvGuide(AppSession& session);
    ~TvGuide();
    TvGuide(const TvGuide&) = delete;
    TvGuide& operator=(const TvGuide&) = delete;

    State state() const { return state_; }
    std::size_t channelCount() const { return guide_.channels.size(); }

    // Downloads the address of the settings on the transfer thread (Off without one).
    void refresh();
    // At exit: the download stops.
    void stop() { token_.cancel(); }

    // The guide in use (tests give theirs).
    void use(jellyfin::Guide guide);

    // The programmes of a Jellyfin channel that end after `fromUtc` and start before `toUtc`, as programme items of
    // that channel; empty when no guide channel matches it (by name, then by number).
    std::vector<jellyfin::Item> programs(const jellyfin::Item& channel, std::int64_t fromUtc, std::int64_t toUtc) const;
    // Its programme on now, else null.
    std::shared_ptr<const jellyfin::Item> current(const jellyfin::Item& channel, std::int64_t nowUtc) const;
    // Channels without a programme on now get this guide's.
    void fill(std::vector<jellyfin::Item>& channels, std::int64_t nowUtc) const;

    // `changed` runs when the state changes; listeners are owners.
    void listen(const void* owner, std::function<void()> changed);
    void unlisten(const void* owner);

private:
    const std::string* channelIdFor(const jellyfin::Item& channel) const;
    void notify();

    AppSession& session_;
    State state_ = State::Off;
    jellyfin::Guide guide_;
    std::map<std::string, std::string> byKey_;  // channel key (or "#number") -> guide channel id
    std::map<std::string, std::pair<std::size_t, std::size_t>> ranges_;  // guide channel id -> its programmes
    core::CancelToken token_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::vector<std::pair<const void*, std::function<void()>>> listeners_;
};

}  // namespace playfin::app
