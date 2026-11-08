#include "app/shell/downloads.hpp"

#include <dirent.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>

#include "app/shell/format.hpp"
#include "app/shell/session.hpp"
#include "core/data_dir.hpp"
#include "core/file_io.hpp"
#include "core/log.hpp"
#include "jellyfin/json.hpp"
#include "ui/application.hpp"
#include "ui/i18n.hpp"

namespace playfin::app {

namespace json = jellyfin::json;

namespace {

constexpr std::size_t kMaxListBytes = 2 * 1024 * 1024;
constexpr std::size_t kMaxEntries = 500;
constexpr std::int64_t kSpareBytes = 512LL * 1024 * 1024;  // what is left free for the console and the app
constexpr std::int64_t kCheckEvery = 64LL * 1024 * 1024;    // free space asked again after this much
const char* const kNoSpace = "no space left";

std::optional<std::string> folder() {
    const auto path = core::dataPath("downloads");
    if (!path || !core::makeDirs(*path)) {
        return std::nullopt;
    }
    return path;
}

// letters, digits and dashes of an id (a file name that cannot leave the folder)
std::string safe(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') {
            out += c;
        }
    }
    return out.substr(0, 64);
}

// what a download stopped by the app's closing left behind ("<file>.part"); nothing runs yet
void sweepParts(const std::string& dir) {
    DIR* listing = ::opendir(dir.c_str());
    if (listing == nullptr) {
        return;
    }
    std::vector<std::string> parts;
    while (const dirent* file = ::readdir(listing)) {
        const std::string name = file->d_name;
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".part") == 0 && name.find('/') == std::string::npos) {
            parts.push_back(name);
        }
    }
    ::closedir(listing);
    for (const std::string& name : parts) {
        if (const auto path = core::resolveInside(dir, name)) {
            ::unlink(path->c_str());
        }
    }
}

// a failure of the connection: the next ones would fail the same way, so the queue waits
bool connectionLost(const core::Error& error) {
    return error.kind == core::ErrorKind::Network || error.kind == core::ErrorKind::Timeout ||
           error.kind == core::ErrorKind::Tls;
}

const char* stateName(Downloads::State state) {
    switch (state) {
    case Downloads::State::Done:
        return "done";
    case Downloads::State::Failed:
        return "failed";
    default:
        return "queued";
    }
}

}  // namespace

Downloads::Downloads(AppSession& session) : session_(session) {}

Downloads::~Downloads() {
    *alive_ = false;
    token_.cancel();
}

void Downloads::load() {
    if (const auto dir = folder()) {
        sweepParts(*dir);
    }
    const auto path = core::dataPath("downloads.json");
    std::string text;
    if (!path || core::readSmallFile(*path, kMaxListBytes, text) != core::ReadStatus::Ok) {
        return;
    }
    const auto doc = json::Document::parse(text);
    yyjson_val* items = doc ? json::array(doc->root(), "items") : nullptr;
    yyjson_arr_iter iter = yyjson_arr_iter_with(items);
    while (yyjson_val* value = items != nullptr ? yyjson_arr_iter_next(&iter) : nullptr) {
        if (entries_.size() >= kMaxEntries) {
            break;
        }
        Entry entry;
        jellyfin::Item& item = entry.item;
        item.id = safe(json::string(value, "id"));
        item.name = json::string(value, "name");
        item.type = jellyfin::itemTypeFromName(json::string(value, "type"));
        item.seriesId = json::string(value, "seriesId");
        item.seriesName = json::string(value, "seriesName");
        item.seasonName = json::string(value, "seasonName");
        item.overview = json::string(value, "overview");
        item.indexNumber = static_cast<int>(json::integer(value, "index"));
        item.parentIndexNumber = static_cast<int>(json::integer(value, "parentIndex"));
        item.productionYear = static_cast<int>(json::integer(value, "year"));
        item.runTimeTicks = json::integer(value, "runTimeTicks");
        entry.file = json::string(value, "file");
        entry.image = json::string(value, "image");
        entry.size = json::integer(value, "size");
        entry.positionTicks = json::integer(value, "position");
        const std::string state = json::string(value, "state");
        entry.state = state == "done" ? State::Done : state == "failed" ? State::Failed : State::Queued;
        entry.error = json::string(value, "error");
        if (item.id.empty() || entry.file.empty() || entry.file.find('/') != std::string::npos ||
            entry.image.find('/') != std::string::npos) {
            continue;
        }
        entry.received = entry.state == State::Done ? entry.size : 0;
        entries_.push_back(std::move(entry));
    }
    startNext();  // what was queued when the app closed
}

void Downloads::save() const {
    const auto path = core::dataPath("downloads.json");
    if (!path) {
        return;
    }
    json::Writer writer;
    writer.object();
    writer.key("version").value(1);
    writer.key("items").array();
    for (const Entry& entry : entries_) {
        const jellyfin::Item& item = entry.item;
        writer.object();
        writer.key("id").value(item.id);
        writer.key("name").value(item.name);
        writer.key("type").value(item.type == jellyfin::ItemType::Episode ? "Episode"
                                 : item.type == jellyfin::ItemType::Video ? "Video"
                                                                          : "Movie");
        writer.key("seriesId").value(item.seriesId);
        writer.key("seriesName").value(item.seriesName);
        writer.key("seasonName").value(item.seasonName);
        writer.key("overview").value(item.overview);
        writer.key("index").value(item.indexNumber);
        writer.key("parentIndex").value(item.parentIndexNumber);
        writer.key("year").value(item.productionYear);
        writer.key("runTimeTicks").value(item.runTimeTicks);
        writer.key("file").value(entry.file);
        writer.key("image").value(entry.image);
        writer.key("size").value(entry.size);
        writer.key("position").value(entry.positionTicks);
        writer.key("state").value(stateName(entry.state));
        writer.key("error").value(entry.error);
        writer.end();
    }
    writer.end();
    writer.end();
    core::writeFileAtomic(*path, writer.str());
}

Downloads::Entry* Downloads::entry(const std::string& id) {
    for (Entry& candidate : entries_) {
        if (candidate.item.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

const Downloads::Entry* Downloads::find(const std::string& id) const {
    for (const Entry& candidate : entries_) {
        if (candidate.item.id == id) {
            return &candidate;
        }
    }
    return nullptr;
}

std::optional<std::string> Downloads::localFile(const std::string& id) const {
    const Entry* found = find(id);
    if (found == nullptr || found->state != State::Done) {
        return std::nullopt;
    }
    const auto dir = folder();
    if (!dir) {
        return std::nullopt;
    }
    return core::resolveInside(*dir, found->file);
}

std::string Downloads::localImageUrl(const std::string& id) const {
    const Entry* found = find(id);
    const auto dir = folder();
    if (found == nullptr || found->image.empty() || !dir) {
        return {};
    }
    const auto path = core::resolveInside(*dir, found->image);
    return path ? "file://" + *path : std::string();
}

void Downloads::add(const jellyfin::Item& item) {
    const std::string id = safe(item.id);
    if (id.empty() || find(id) != nullptr || entries_.size() >= kMaxEntries) {
        return;
    }
    Entry entry;
    entry.item = item;
    entry.item.id = id;
    entry.item.people.clear();
    std::string container = "mkv";
    if (!item.mediaSources.empty()) {
        entry.size = item.mediaSources.front().size;
        const std::string named = safe(item.mediaSources.front().container.substr(0, item.mediaSources.front().container.find(',')));
        if (!named.empty() && named.size() <= 5) {
            container = named;
        }
    }
    entry.item.mediaSources.clear();
    entry.file = id + "." + container;
    entries_.push_back(std::move(entry));
    save();
    notify();
    startNext();
}

void Downloads::retry(const std::string& id) {
    if (Entry* found = entry(id); found != nullptr && found->state == State::Failed) {
        found->state = State::Queued;
        found->error.clear();
        save();
        notify();
        startNext();
    }
}

void Downloads::stop() { token_.cancel(); }

void Downloads::remove(const std::string& id) {
    if (running_ == id) {
        // its job ends with "cancelled" and its part file goes with its sink; the next one waits for that (the
        // same film added again would write the same part file)
        token_.cancel();
    }
    const auto dir = folder();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->item.id != id) {
            continue;
        }
        if (dir) {
            for (const std::string& name : {it->file, it->image}) {
                if (const auto path = name.empty() ? std::nullopt : core::resolveInside(*dir, name)) {
                    ::unlink(path->c_str());
                }
            }
        }
        entries_.erase(it);
        break;
    }
    save();
    notify();
    startNext();
}

void Downloads::setPosition(const std::string& id, std::int64_t ticks) {
    if (Entry* found = entry(id)) {
        found->positionTicks = ticks;
        save();
    }
}

void Downloads::listen(const void* owner, std::function<void()> changed) {
    listeners_.emplace_back(owner, std::move(changed));
}

void Downloads::unlisten(const void* owner) {
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                    [owner](const auto& listener) { return listener.first == owner; }),
                     listeners_.end());
}

void Downloads::notify() {
    const auto listeners = listeners_;  // a listener may unlisten
    for (const auto& listener : listeners) {
        if (listener.second) {
            listener.second();
        }
    }
}

std::int64_t Downloads::freeBytes() {
    const auto dir = folder();
    struct statvfs info {};
    if (!dir || ::statvfs(dir->c_str(), &info) != 0) {
        return 0;
    }
    return static_cast<std::int64_t>(info.f_bavail) * static_cast<std::int64_t>(info.f_frsize);
}

void Downloads::startNext() {
    if (!running_.empty()) {
        return;
    }
    Entry* next = nullptr;
    for (Entry& candidate : entries_) {
        if (candidate.state == State::Queued) {
            next = &candidate;
            break;
        }
    }
    const auto client = session_.client();
    const auto dir = folder();
    if (next == nullptr || !client || !dir) {
        return;
    }
    if (next->size > 0) {
        const std::int64_t free = freeSpace ? freeSpace() : 0;
        if (free > 0 && free < next->size + kSpareBytes) {
            next->state = State::Failed;
            next->error = ui::tr("downloads.no_space");
            save();
            notify();
            startNext();
            return;
        }
    }
    next->state = State::Running;
    next->received = 0;
    next->image = next->item.id + ".jpg";
    running_ = next->item.id;
    token_ = core::CancelToken{};
    const std::string id = next->item.id;
    const auto filePath = core::resolveInside(*dir, next->file);
    const std::string imageName = id + ".jpg";
    const auto imagePath = core::resolveInside(*dir, imageName);
    const std::string imageUrl = imageUrlFor(*client, next->item, false, 400, 600);
    notify();
    const core::CancelToken token = token_;
    std::weak_ptr<bool> alive = alive_;
    ui::Application* app = ui::Application::current();
    const std::function<std::int64_t()> freeNow = freeSpace;
    auto job = [client, id, filePath, imagePath, imageUrl, token, alive, app, freeNow, this]() {
        std::int64_t received = 0;
        if (!filePath) {
            return std::make_pair(core::Result<core::Unit>(core::makeError(core::ErrorKind::Storage, "bad name")),
                                  received);
        }
        if (imagePath && !imageUrl.empty()) {
            const auto picture = client->fetch(imageUrl, token);
            if (picture.ok()) {
                core::writeFileAtomic(*imagePath, picture.value());
            }
        }
        core::FileSink sink(*filePath);
        if (!sink.ok()) {
            return std::make_pair(core::Result<core::Unit>(core::makeError(core::ErrorKind::Storage, "cannot write")),
                                  received);
        }
        // the free space is asked at the start, once the length is known, then every 64 MB: an episode's size is
        // not known beforehand, and the console's storage may fill meanwhile
        int lastPercent = -1;
        std::int64_t expected = 0;
        std::int64_t checkAt = 0;
        bool noSpace = false;
        const auto enoughSpace = [&] {
            const std::int64_t written = static_cast<std::int64_t>(sink.written());
            checkAt = written + kCheckEvery;
            const std::int64_t free = freeNow ? freeNow() : 0;
            const std::int64_t left = expected > written ? expected - written : 0;
            return free <= 0 || free >= left + kSpareBytes;
        };
        auto result = client->downloadItem(
            id,
            [&](const char* data, std::size_t size) {
                if (static_cast<std::int64_t>(sink.written()) >= checkAt && !enoughSpace()) {
                    noSpace = true;
                    return false;
                }
                return sink.write(data, size);
            },
            [&](long long now, long long total) {
                if (total > 0 && expected != total) {
                    expected = total;
                    checkAt = 0;  // asked again at the next write, with the length
                }
                received = now;
                // a step: a percent, or a megabyte when the length is not known
                const int percent = total > 0 ? static_cast<int>(now * 100 / total)
                                              : static_cast<int>(now / (1024 * 1024));
                if (percent != lastPercent && app != nullptr) {
                    lastPercent = percent;
                    app->mainQueue().post([alive, id, now, total, this] {
                        if (const auto flag = alive.lock(); flag && *flag) {
                            progress(id, now, total);
                        }
                    });
                }
            },
            token);
        if (noSpace) {
            result = core::makeError(core::ErrorKind::Storage, kNoSpace);
        } else if (result.ok() && !sink.commit()) {
            result = core::makeError(core::ErrorKind::Storage, "the file could not be written");
        }
        received = sink.written();
        return std::make_pair(std::move(result), received);
    };
    using Outcome = std::pair<core::Result<core::Unit>, std::int64_t>;
    auto done = [this, id, alive](Outcome outcome) {
        if (const auto flag = alive.lock(); flag && *flag) {
            finished(id, std::move(outcome.first), outcome.second);
        }
    };
    if (session_.synchronous || app == nullptr) {
        done(job());
        return;
    }
    core::runAsync<Outcome>(session_.transfers(), app->mainQueue(), job, done, core::CancelToken{});
}

void Downloads::progress(const std::string& id, std::int64_t received, std::int64_t total) {
    if (Entry* found = entry(id); found != nullptr && found->state == State::Running) {
        found->received = received;
        if (total > 0) {
            found->size = total;
        }
        notify();
    }
}

void Downloads::finished(const std::string& id, core::Result<core::Unit> result, std::int64_t received) {
    if (running_ == id) {
        running_.clear();
    }
    Entry* found = entry(id);
    if (found != nullptr) {  // else removed meanwhile
        if (result.ok()) {
            found->state = State::Done;
            found->size = received;
            found->received = received;
            PF_LOG_INFO("downloads: %s done (%lld bytes)", id.c_str(), static_cast<long long>(received));
        } else if (result.error().kind != core::ErrorKind::Cancelled) {
            const core::Error& error = result.error();
            found->state = State::Failed;
            found->error = error.kind == core::ErrorKind::Unauthorized ? ui::tr("downloads.not_allowed")
                           : error.message == kNoSpace                ? ui::tr("downloads.no_space")
                                                                      : errorText(error);
            PF_LOG_WARN("downloads: %s failed: %s", id.c_str(), error.message.c_str());
        }
        save();
    }
    notify();
    // without the server, the rest stays queued (it goes on when one is added or tried again, or at the next start)
    if (result.ok() || !connectionLost(result.error())) {
        startNext();
    }
}

}  // namespace playfin::app
