#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

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
    // `resume`: writes on after what "<path>.part" already holds (a paused download), instead of starting it over
    explicit FileSink(std::string path, bool resume = false);
    ~FileSink();
    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;
    bool ok() const { return fd_ >= 0; }
    bool write(const void* data, std::size_t size);
    bool commit();  // fsync, close, rename
    // Closes it keeping the part file as it is (a download paused, or stopped by the app's closing).
    void keep();
    // Empties the part file (the server sent the whole file again rather than its rest).
    bool restart();
    long long written() const { return written_; }

    // Pieces are gathered up to this size before they are written (one system call per block, not per piece).
    static constexpr std::size_t kBlock = 1024 * 1024;

private:
    void abandon();
    bool writeOut(const char* bytes, std::size_t size);  // straight to the file
    bool flush();                                         // the gathered pieces
    std::string path_;
    int fd_ = -1;
    long long written_ = 0;  // accepted (gathered or on the disk)
    std::vector<char> buffer_;
};

}  // namespace playfin::core
