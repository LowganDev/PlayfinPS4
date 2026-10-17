#include "core/log.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <vector>

#include "core/boot_trace.hpp"
#include "core/redact.hpp"

namespace playfin::log {

namespace {

std::mutex gMutex;
Level gLevel = Level::Info;
bool gStderr = true;
std::FILE* gFile = nullptr;
std::string gPath;
std::size_t gMaxBytes = 0;
std::size_t gWritten = 0;

const char* tag(Level level) {
    switch (level) {
    case Level::Debug:
        return "D";
    case Level::Info:
        return "I";
    case Level::Warn:
        return "W";
    case Level::Error:
        return "E";
    }
    return "?";
}

std::string timestamp() {
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    std::tm local{};
#ifdef PLAYFIN_PS4
    gmtime_r(&now.tv_sec, &local);  // UTC: no time zone files to read through the C library
#else
    localtime_r(&now.tv_sec, &local);
#endif
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03ld", local.tm_hour, local.tm_min, local.tm_sec,
                  now.tv_nsec / 1000000);
    return buffer;
}

std::string format(const char* fmt, va_list args) {
    std::vector<char> buffer(256);
    va_list copy;
    va_copy(copy, args);
    const int needed = std::vsnprintf(buffer.data(), buffer.size(), fmt, copy);
    va_end(copy);
    if (needed < 0) {
        return "[log format error]";
    }
    const auto length = static_cast<std::size_t>(needed);
    if (length >= buffer.size()) {
        buffer.resize(length + 1);
        std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
    }
    std::string message(buffer.data(), length);
    if (message.size() > kMaxMessage) {
        message.resize(kMaxMessage);
        message += "[truncated]";
    }
    return message;
}

// Called with gMutex held
void rotateIfNeeded(std::size_t incoming) {
    if (gFile == nullptr || gWritten == 0 || gWritten + incoming <= gMaxBytes) {
        return;
    }
    std::fclose(gFile);
    std::rename(gPath.c_str(), rotatedName(gPath).c_str());
    gFile = std::fopen(gPath.c_str(), "w");
    gWritten = 0;
}

}  // namespace

void setLevel(Level level) {
    std::lock_guard<std::mutex> lock(gMutex);
    gLevel = level;
}

Level level() {
    std::lock_guard<std::mutex> lock(gMutex);
    return gLevel;
}

void setStderr(bool enabled) {
    std::lock_guard<std::mutex> lock(gMutex);
    gStderr = enabled;
}

bool openFile(const std::string& path, std::size_t maxBytes) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gFile != nullptr) {
        std::fclose(gFile);
    }
    // never "a": on the PS4 (musl port) append mode crashes inside fopen (its fcntl calls), seen on the first
    // console runs. An existing file is opened for update and written at its end; a missing one is created.
    gFile = std::fopen(path.c_str(), "r+");
    if (gFile == nullptr) {
        gFile = std::fopen(path.c_str(), "w");
    }
    if (gFile == nullptr) {
        core::bootMark("log: open failed, errno", errno);
        return false;
    }
    core::bootMark("log: open");
    gPath = path;
    gMaxBytes = maxBytes;
    std::fseek(gFile, 0, SEEK_END);
    const long size = std::ftell(gFile);
    gWritten = size > 0 ? static_cast<std::size_t>(size) : 0;
    return true;
}

void closeFile() {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gFile != nullptr) {
        std::fclose(gFile);
        gFile = nullptr;
    }
}

std::string rotatedName(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    const std::size_t dot = path.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return path + ".1";
    }
    return path.substr(0, dot) + ".1" + path.substr(dot);
}

void write(Level level, const char* fmt, ...) {
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (level < gLevel) {
            return;
        }
    }
    va_list args;
    va_start(args, fmt);
    const std::string message = format(fmt, args);
    va_end(args);
    const std::string line = timestamp() + " " + tag(level) + " " + core::redact(message) + "\n";

    std::lock_guard<std::mutex> lock(gMutex);
    if (gStderr) {
        std::fputs(line.c_str(), stderr);
    }
#ifdef PLAYFIN_PS4
    // also to boot.txt and the kernel log (raw kernel calls): readable even when the log file is not
    static int mirrored = 0;
    if (mirrored < 200) {
        ++mirrored;
        core::bootMark(line.substr(0, line.size() - 1).c_str());
    }
#endif
    rotateIfNeeded(line.size());
    if (gFile != nullptr) {
        std::fputs(line.c_str(), gFile);
        std::fflush(gFile);
        gWritten += line.size();
    }
}

}  // namespace playfin::log
