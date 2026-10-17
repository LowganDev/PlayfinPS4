#include "jellyfin/store.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>

#include "core/file_io.hpp"
#include "core/log.hpp"
#include "jellyfin/json.hpp"

namespace playfin::jellyfin {

namespace {

constexpr std::size_t kMaxFileBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxUrls = 5;

core::Error storageError(std::string message) { return core::makeError(core::ErrorKind::Storage, std::move(message)); }

bool parseData(std::string_view text, StoreData& data) {
    const auto doc = json::Document::parse(text);
    if (!doc || !yyjson_is_obj(doc->root()) || json::integer(doc->root(), "version") != 1) {
        return false;
    }
    yyjson_val* root = doc->root();
    data.deviceId = json::string(root, "deviceId");
    data.lastServerId = json::string(root, "lastServerId");
    data.lastUserId = json::string(root, "lastUserId");
    yyjson_val* servers = json::array(root, "servers");
    if (servers == nullptr) {
        return true;
    }
    yyjson_arr_iter serverIter = yyjson_arr_iter_with(servers);
    yyjson_val* entry = nullptr;
    while ((entry = yyjson_arr_iter_next(&serverIter)) != nullptr) {
        StoredServer server;
        server.id = json::string(entry, "id");
        if (server.id.empty()) {
            continue;
        }
        server.name = json::string(entry, "name");
        server.urls = json::strings(entry, "urls");
        server.lastUrl = json::string(entry, "lastUrl");
        server.pin = json::string(entry, "pin");
        if (yyjson_val* users = json::array(entry, "users")) {
            yyjson_arr_iter userIter = yyjson_arr_iter_with(users);
            yyjson_val* userEntry = nullptr;
            while ((userEntry = yyjson_arr_iter_next(&userIter)) != nullptr) {
                StoredUser user;
                user.id = json::string(userEntry, "id");
                if (user.id.empty()) {
                    continue;
                }
                user.name = json::string(userEntry, "name");
                user.token = json::string(userEntry, "token");
                server.users.push_back(std::move(user));
            }
        }
        data.servers.push_back(std::move(server));
    }
    return true;
}

void addString(yyjson_mut_doc* doc, yyjson_mut_val* object, const char* key, const std::string& value) {
    yyjson_mut_obj_add_strncpy(doc, object, key, value.data(), value.size());
}

std::string serialize(const StoreData& data) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", 1);
    addString(doc, root, "deviceId", data.deviceId);
    addString(doc, root, "lastServerId", data.lastServerId);
    addString(doc, root, "lastUserId", data.lastUserId);
    yyjson_mut_val* servers = yyjson_mut_obj_add_arr(doc, root, "servers");
    for (const StoredServer& server : data.servers) {
        yyjson_mut_val* object = yyjson_mut_arr_add_obj(doc, servers);
        addString(doc, object, "id", server.id);
        addString(doc, object, "name", server.name);
        addString(doc, object, "lastUrl", server.lastUrl);
        addString(doc, object, "pin", server.pin);
        yyjson_mut_val* urls = yyjson_mut_obj_add_arr(doc, object, "urls");
        for (const std::string& url : server.urls) {
            yyjson_mut_arr_add_strncpy(doc, urls, url.data(), url.size());
        }
        yyjson_mut_val* users = yyjson_mut_obj_add_arr(doc, object, "users");
        for (const StoredUser& user : server.users) {
            yyjson_mut_val* userObject = yyjson_mut_arr_add_obj(doc, users);
            addString(doc, userObject, "id", user.id);
            addString(doc, userObject, "name", user.name);
            addString(doc, userObject, "token", user.token);
        }
    }
    std::size_t length = 0;
    char* text = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &length);
    std::string out = text != nullptr ? std::string(text, length) : std::string();
    std::free(text);
    yyjson_mut_doc_free(doc);
    return out;
}

std::uint32_t low(std::uint64_t value) { return static_cast<std::uint32_t>(value & 0xFFFFFFFFu); }
std::uint32_t high(std::uint64_t value) { return static_cast<std::uint32_t>(value >> 32); }

}  // namespace

std::string newDeviceId() {
    std::uint64_t random[2] = {0, 0};
    try {
        std::random_device device;
        for (auto& value : random) {
            value = (static_cast<std::uint64_t>(device()) << 32) ^ device();
        }
    } catch (...) {
        // no entropy source: the clock and addresses below still make the id unique enough
    }
    const auto now = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&random));
    std::seed_seq sequence{low(random[0]), high(random[0]), low(random[1]), high(random[1]),
                           low(now),       high(now),       low(address),   high(address)};
    std::mt19937_64 engine(sequence);
    std::uint8_t bytes[16];
    for (int i = 0; i < 16; i += 8) {
        const std::uint64_t value = engine();
        for (int j = 0; j < 8; ++j) {
            bytes[i + j] = static_cast<std::uint8_t>(value >> (8 * j));
        }
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out += '-';
        }
        out += kHex[bytes[i] >> 4];
        out += kHex[bytes[i] & 0x0F];
    }
    return out;
}

StoredServer* findServer(StoreData& data, const std::string& id) {
    for (StoredServer& server : data.servers) {
        if (server.id == id) {
            return &server;
        }
    }
    return nullptr;
}

StoredServer& upsertServer(StoreData& data, const std::string& id) {
    if (StoredServer* server = findServer(data, id)) {
        return *server;
    }
    data.servers.push_back(StoredServer{});
    data.servers.back().id = id;
    return data.servers.back();
}

StoredUser& upsertUser(StoredServer& server, const std::string& id) {
    for (StoredUser& user : server.users) {
        if (user.id == id) {
            return user;
        }
    }
    server.users.push_back(StoredUser{});
    server.users.back().id = id;
    return server.users.back();
}

void rememberUrl(StoredServer& server, const std::string& url) {
    server.urls.erase(std::remove(server.urls.begin(), server.urls.end(), url), server.urls.end());
    server.urls.insert(server.urls.begin(), url);
    if (server.urls.size() > kMaxUrls) {
        server.urls.resize(kMaxUrls);
    }
    server.lastUrl = url;
}

ServerStore::ServerStore(std::string path) : path_(std::move(path)) {}

core::Result<StoreData> ServerStore::load() const {
    std::string text;
    StoreData data;
    switch (core::readSmallFile(path_, kMaxFileBytes, text)) {
    case core::ReadStatus::Missing:
        break;
    case core::ReadStatus::Failed:
        return storageError("cannot read " + path_);
    case core::ReadStatus::Ok:
        if (!parseData(text, data)) {
            if (std::rename(path_.c_str(), (path_ + ".bad").c_str()) != 0) {
                return storageError(path_ + " is damaged and cannot be set aside");
            }
            PF_LOG_WARN("%s is damaged: kept as .bad, starting empty", path_.c_str());
            data = StoreData{};
        }
        break;
    }
    if (data.deviceId.empty()) {
        data.deviceId = newDeviceId();
        const auto saved = save(data);
        if (!saved.ok()) {
            return saved.error();
        }
    }
    return data;
}

core::Result<core::Unit> ServerStore::save(const StoreData& data) const {
    const std::string text = serialize(data);
    if (text.empty() || !core::writeFileAtomic(path_, text)) {
        return storageError("cannot write " + path_);
    }
    return core::Unit{};
}

}  // namespace playfin::jellyfin
