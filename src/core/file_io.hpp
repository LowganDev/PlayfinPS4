#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace playfin::core {

enum class ReadStatus { Ok, Missing, Failed };

// Reads a regular file of at most `maxBytes` (Failed when larger, unreadable or not a regular file).
ReadStatus readSmallFile(const std::string& path, std::size_t maxBytes, std::string& out);

// Replaces `path` with `text` through `path`.tmp, fsync and rename: readers see the old or the new file, never a
// part. The file is private (0600).
bool writeFileAtomic(const std::string& path, std::string_view text);

// A large file written in pieces to `path`.part (private, 0600) and renamed to `path` by commit(); destroyed
// without commit, the part file is removed. The caller resolves `path` (core::dataPath).
class FileSink {
public:
    explicit FileSink(std::string path);
    ~FileSink();
    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;
    bool ok() const { return fd_ >= 0; }
    bool write(const void* data, std::size_t size);
    bool commit();  // fsync, close, rename
    long long written() const { return written_; }

private:
    void abandon();
    std::string path_;
    int fd_ = -1;
    long long written_ = 0;
};

}  // namespace playfin::core
