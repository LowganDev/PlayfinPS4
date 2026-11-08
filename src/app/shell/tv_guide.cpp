#include "app/shell/tv_guide.hpp"

#include <algorithm>

#include "app/shell/session.hpp"
#include "core/log.hpp"
#include "ui/application.hpp"

namespace playfin::app {

namespace {

bool digitsOnly(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// "007" and "7" are the same channel number
std::string numberKey(const std::string& number) {
    const std::size_t first = number.find_first_not_of('0');
    return "#" + (first == std::string::npos ? std::string("0") : number.substr(first));
}

}  // namespace

TvGuide::TvGuide(AppSession& session) : session_(session) {}

TvGuide::~TvGuide() {
    *alive_ = false;
    token_.cancel();
}

void TvGuide::use(jellyfin::Guide guide) {
    guide_ = std::move(guide);
    byKey_.clear();
    ranges_.clear();
    // names first (a channel's own name wins over another's id), then ids and numbers
    for (const auto& channel : guide_.channels) {
        for (const std::string& name : channel.names) {
            if (digitsOnly(name)) {
                byKey_.emplace(numberKey(name), channel.id);
            } else if (const std::string key = jellyfin::channelKey(name); !key.empty()) {
                byKey_.emplace(key, channel.id);
            }
        }
    }
    for (const auto& channel : guide_.channels) {
        byKey_.emplace(jellyfin::channelKey(channel.id), channel.id);
        byKey_.emplace(jellyfin::channelKey(channel.id.substr(0, channel.id.find('.'))), channel.id);
    }
    byKey_.erase("");
    for (std::size_t i = 0; i < guide_.programs.size();) {  // sorted by channel
        std::size_t end = i;
        while (end < guide_.programs.size() && guide_.programs[end].channel == guide_.programs[i].channel) {
            ++end;
        }
        ranges_[guide_.programs[i].channel] = {i, end};
        i = end;
    }
    state_ = State::Ready;
    notify();
}

const std::string* TvGuide::channelIdFor(const jellyfin::Item& channel) const {
    auto found = byKey_.find(jellyfin::channelKey(channel.name));
    if (found == byKey_.end() && digitsOnly(channel.channelNumber)) {
        found = byKey_.find(numberKey(channel.channelNumber));
    }
    return found == byKey_.end() ? nullptr : &found->second;
}

std::vector<jellyfin::Item> TvGuide::programs(const jellyfin::Item& channel, std::int64_t fromUtc,
                                              std::int64_t toUtc) const {
    std::vector<jellyfin::Item> out;
    const std::string* id = channelIdFor(channel);
    const auto range = id == nullptr ? ranges_.end() : ranges_.find(*id);
    if (range == ranges_.end()) {
        return out;
    }
    for (std::size_t i = range->second.first; i < range->second.second; ++i) {
        const jellyfin::GuideProgram& program = guide_.programs[i];
        if (program.endUtc <= fromUtc || program.startUtc >= toUtc) {
            continue;
        }
        jellyfin::Item item;
        item.type = jellyfin::ItemType::Program;
        item.name = program.title;
        item.overview = program.description;
        item.startUtc = program.startUtc;
        item.endUtc = program.endUtc;
        item.channelId = channel.id;
        out.push_back(std::move(item));
    }
    return out;
}

std::shared_ptr<const jellyfin::Item> TvGuide::current(const jellyfin::Item& channel, std::int64_t nowUtc) const {
    auto list = programs(channel, nowUtc, nowUtc + 1);
    if (list.empty()) {
        return nullptr;
    }
    return std::make_shared<const jellyfin::Item>(std::move(list.front()));
}

void TvGuide::fill(std::vector<jellyfin::Item>& channels, std::int64_t nowUtc) const {
    if (state_ != State::Ready) {
        return;
    }
    for (auto& channel : channels) {
        if (!channel.currentProgram) {
            channel.currentProgram = current(channel, nowUtc);
        }
    }
}

void TvGuide::refresh() {
    token_.cancel();
    token_ = core::CancelToken{};
    const std::string url = session_.settings().xmltvUrl;
    if (url.empty()) {
        guide_ = jellyfin::Guide{};
        byKey_.clear();
        ranges_.clear();
        state_ = State::Off;
        notify();
        return;
    }
    state_ = State::Loading;
    notify();
    const std::int64_t now = session_.now();
    const net::HttpClient* http = &session_.http();
    const core::CancelToken token = token_;
    auto job = [http, url, now, token] {
        jellyfin::XmltvReader reader(now - kPastSeconds, now + kAheadSeconds);
        net::HttpRequest request;  // another host than the server: no Jellyfin header
        request.url = url;
        request.timeoutMs = 0;  // a big guide takes long; the low-speed limit stops a stalled one
        auto response = http->download(
            request, [&reader](const char* data, std::size_t size) { return reader.feed(data, size); }, {}, {},
            token);
        core::Result<jellyfin::Guide> result = jellyfin::Guide{};
        if (!response.ok()) {
            result = response.error();
        } else if (response.value().status < 200 || response.value().status >= 300) {
            result = core::makeError(core::ErrorKind::Server, "HTTP " + std::to_string(response.value().status));
        } else {
            jellyfin::Guide guide = reader.finish();
            if (guide.channels.empty() && guide.programs.empty()) {
                result = core::makeError(core::ErrorKind::BadResponse, "not an XMLTV guide");
            } else {
                result = std::move(guide);
            }
        }
        return result;
    };
    std::weak_ptr<bool> alive = alive_;
    auto done = [this, alive, token](core::Result<jellyfin::Guide> result) {
        const auto flag = alive.lock();
        if (!flag || !*flag || token.cancelled()) {
            return;
        }
        if (!result.ok()) {
            PF_LOG_WARN("tv guide: not read: %s", result.error().message.c_str());
            state_ = State::Failed;
            notify();
            return;
        }
        PF_LOG_INFO("tv guide: %zu channels, %zu programmes", result.value().channels.size(),
                    result.value().programs.size());
        use(std::move(result.value()));
    };
    ui::Application* app = ui::Application::current();
    if (session_.synchronous || app == nullptr) {
        done(job());
        return;
    }
    core::runAsync<core::Result<jellyfin::Guide>>(session_.transfers(), app->mainQueue(), job, done,
                                                  core::CancelToken{});
}

void TvGuide::listen(const void* owner, std::function<void()> changed) {
    listeners_.emplace_back(owner, std::move(changed));
}

void TvGuide::unlisten(const void* owner) {
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                    [owner](const auto& listener) { return listener.first == owner; }),
                     listeners_.end());
}

void TvGuide::notify() {
    const auto listeners = listeners_;
    for (const auto& listener : listeners) {
        if (listener.second) {
            listener.second();
        }
    }
}

}  // namespace playfin::app
