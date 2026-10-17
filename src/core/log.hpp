#pragma once

#include <cstddef>
#include <string>

namespace playfin::log {

enum class Level { Debug = 0, Info, Warn, Error };

// Longest message kept; longer ones end with "[truncated]".
constexpr std::size_t kMaxMessage = 16 * 1024;

void setLevel(Level level);
Level level();

// Copy every line to stderr (on by default).
void setStderr(bool enabled);

// Appends to `path`; when it would exceed maxBytes, the file is renamed to rotatedName(path) (replacing the
// previous backup) and a new one is started. Returns false if the file cannot be opened: logging then continues
// on stderr only.
bool openFile(const std::string& path, std::size_t maxBytes = 1024 * 1024);
void closeFile();

// "playfin.log" -> "playfin.1.log"
std::string rotatedName(const std::string& path);

// printf-style; secrets are masked (core::redact) before the line is written. Thread-safe.
void write(Level level, const char* format, ...) __attribute__((format(printf, 2, 3)));

}  // namespace playfin::log

#define PF_LOG_DEBUG(...) ::playfin::log::write(::playfin::log::Level::Debug, __VA_ARGS__)
#define PF_LOG_INFO(...) ::playfin::log::write(::playfin::log::Level::Info, __VA_ARGS__)
#define PF_LOG_WARN(...) ::playfin::log::write(::playfin::log::Level::Warn, __VA_ARGS__)
#define PF_LOG_ERROR(...) ::playfin::log::write(::playfin::log::Level::Error, __VA_ARGS__)
