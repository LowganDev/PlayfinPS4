#include "core/file_io.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <utility>

namespace playfin::core {

ReadStatus readSmallFile(const std::string& path, std::size_t maxBytes, std::string& out) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return errno == ENOENT ? ReadStatus::Missing : ReadStatus::Failed;
    }
    struct stat info {};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<std::size_t>(info.st_size) > maxBytes) {
        ::close(fd);
        return ReadStatus::Failed;
    }
    out.resize(static_cast<std::size_t>(info.st_size));
    std::size_t done = 0;
    while (done < out.size()) {
        const ssize_t n = ::read(fd, &out[done], out.size() - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        done += static_cast<std::size_t>(n);
    }
    ::close(fd);
    if (done != out.size()) {  // a read error or a file shortened meanwhile: never taken for the whole file
        out.clear();
        return ReadStatus::Failed;
    }
    return ReadStatus::Ok;
}

bool writeFileAtomic(const std::string& path, std::string_view text) {
    const std::string temp = path + ".tmp";
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return false;
    }
    ::fchmod(fd, 0600);
    std::size_t done = 0;
    while (done < text.size()) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            ::close(fd);
            ::unlink(temp.c_str());
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    const bool synced = ::fsync(fd) == 0;
    if (::close(fd) != 0 || !synced || std::rename(temp.c_str(), path.c_str()) != 0) {
        ::unlink(temp.c_str());
        return false;
    }
    return true;
}

FileSink::FileSink(std::string path) : path_(std::move(path)) {
    fd_ = ::open((path_ + ".part").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd_ >= 0) {
        ::fchmod(fd_, 0600);
    }
}

FileSink::~FileSink() { abandon(); }

void FileSink::abandon() {
    buffer_.clear();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
        ::unlink((path_ + ".part").c_str());
    }
}

bool FileSink::writeOut(const char* bytes, std::size_t size) {
    std::size_t done = 0;
    while (fd_ >= 0 && done < size) {
        const ssize_t n = ::write(fd_, bytes + done, size - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            abandon();  // a full disk or a write error: nothing half-written stays
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return fd_ >= 0;
}

bool FileSink::flush() {
    if (buffer_.empty()) {
        return fd_ >= 0;
    }
    const bool ok = writeOut(buffer_.data(), buffer_.size());
    buffer_.clear();
    return ok;
}

bool FileSink::write(const void* data, std::size_t size) {
    if (fd_ < 0) {
        return false;
    }
    const char* bytes = static_cast<const char*>(data);
    if (buffer_.size() + size > kBlock && !flush()) {
        return false;
    }
    if (size >= kBlock) {  // a large piece goes straight to the file
        if (!writeOut(bytes, size)) {
            return false;
        }
    } else {
        if (buffer_.capacity() < kBlock) {
            buffer_.reserve(kBlock);
        }
        buffer_.insert(buffer_.end(), bytes, bytes + size);
    }
    written_ += static_cast<long long>(size);
    return true;
}

bool FileSink::commit() {
    if (fd_ < 0 || !flush()) {
        return false;
    }
    const bool synced = ::fsync(fd_) == 0;
    const bool closed = ::close(fd_) == 0;
    fd_ = -1;
    const std::string part = path_ + ".part";
    if (!synced || !closed || std::rename(part.c_str(), path_.c_str()) != 0) {
        ::unlink(part.c_str());
        return false;
    }
    return true;
}

}  // namespace playfin::core
