#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "core/task.hpp"

namespace playfin::net {

struct FtpEntry {
    std::string name;
    bool directory = false;
    std::int64_t size = 0;
};

// The lines of a Unix-style LIST answer ("drwxrwxrwx 1 -1 -1 131072 Nov 10 2026 Films"); "." and ".." are left
// out, and so are names with a "/" or a control character.
std::vector<FtpEntry> parseFtpList(std::string_view text);

// A path the FTP client accepts: absolute, without "..", ".", empty parts or control characters.
bool validFtpPath(std::string_view path);

// Blocking FTP over curl, for the console's own FTP server (GoldHEN's, on 127.0.0.1): how Playfin reaches USB
// drives, which the app's sandbox does not show. Paths are absolute (sent as they are, no CWD). Call from a worker.
class FtpClient {
public:
    explicit FtpClient(std::string base);  // "ftp://127.0.0.1:2121"
    const std::string& base() const { return base_; }
    std::string url(const std::string& path) const;  // base + "/" + path (an absolute path on the server)

    // The entries of a folder ("/mnt/usb0/Films"), in the server's order.
    core::Result<std::vector<FtpEntry>> list(const std::string& dir, const core::CancelToken& token = {}) const;
    core::Result<std::int64_t> size(const std::string& path, const core::CancelToken& token = {}) const;
    // A small file (a subtitle), at most `maxBytes`.
    core::Result<std::string> fetch(const std::string& path, std::size_t maxBytes,
                                    const core::CancelToken& token = {}) const;
    core::Result<core::Unit> makeDir(const std::string& path) const;  // fine when it exists
    core::Result<core::Unit> remove(const std::string& path) const;
    core::Result<core::Unit> rename(const std::string& from, const std::string& to) const;

    // A file written from `source` (it fills the buffer and answers the bytes put in it, 0 at the end), replacing
    // it, or after its end with `append`.
    // kUploadAbort from the source stops the upload with a failure.
    using Source = std::function<std::size_t(char* buffer, std::size_t size)>;
    static constexpr std::size_t kUploadAbort = 0x10000000;  // curl's CURL_READFUNC_ABORT
    core::Result<core::Unit> upload(const std::string& path, Source source, bool append,
                                    const core::CancelToken& token = {}) const;

private:
    core::Result<core::Unit> command(const std::vector<std::string>& commands) const;
    std::string base_;
};

// A file read in order, with seeks (the player's view of a file on a USB drive). One transfer at a time, started
// at the position asked; a seek starts a new one. read() blocks until data arrives; cancel() (any thread) makes
// it return -1.
class FtpReader {
public:
    FtpReader(const FtpClient& client, std::string path);
    ~FtpReader();
    FtpReader(const FtpReader&) = delete;
    FtpReader& operator=(const FtpReader&) = delete;

    std::int64_t size();  // -1 when unknown
    // bytes copied to `buffer`, 0 at the end of the file, -1 on a failure or after cancel()
    long long read(char* buffer, std::size_t size);
    bool seek(std::int64_t offset);
    void cancel() { cancelled_ = true; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace playfin::net
