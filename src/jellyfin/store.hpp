#pragma once

#include <string>
#include <vector>

#include "core/result.hpp"

namespace playfin::jellyfin {

struct StoredUser {
    std::string id;
    std::string name;
    std::string token;  // never a password
};

struct StoredServer {
    std::string id;
    std::string name;
    std::vector<std::string> urls;  // most recent first, at most 5
    std::string lastUrl;
    std::string pin;  // "sha256//…" when the user trusted a certificate
    std::vector<StoredUser> users;
};

struct StoreData {
    std::string deviceId;
    std::string lastServerId;
    std::string lastUserId;
    std::vector<StoredServer> servers;
};

// Random UUID v4 (unique per installation, not a secret).
std::string newDeviceId();

StoredServer* findServer(StoreData& data, const std::string& id);
StoredServer& upsertServer(StoreData& data, const std::string& id);
StoredUser& upsertUser(StoredServer& server, const std::string& id);
// `url` becomes lastUrl and moves to the front of urls (no duplicates, at most 5)
void rememberUrl(StoredServer& server, const std::string& url);

// servers.json in the data folder: atomic writes (temp file + rename), mode 0600. A damaged file is renamed
// servers.json.bad and the store starts empty; a missing device id is created and saved.
class ServerStore {
public:
    explicit ServerStore(std::string path);

    core::Result<StoreData> load() const;
    core::Result<core::Unit> save(const StoreData& data) const;
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

}  // namespace playfin::jellyfin
