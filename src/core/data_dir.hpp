#pragma once

#include <sys/types.h>

#include <optional>
#include <string>
#include <string_view>

namespace playfin::core {

// Joins `relative` to `root` only if the result stays inside `root`: rejects absolute paths, empty, "." and ".."
// segments, backslashes and NUL bytes. Every file the app writes goes through this (data dir only).
std::optional<std::string> resolveInside(const std::string& root, std::string_view relative);

// Root of the app data, set once by platform::init.
void setDataRoot(std::string root);
const std::string& dataRoot();

std::optional<std::string> dataPath(std::string_view relative);

// mkdir -p; true when the directory exists afterwards.
bool makeDirs(const std::string& path);
// makeDirs with the mkdir call injected (tests); folders it creates get exactly `mode` (umask ignored).
bool makeDirsWith(const std::string& path, int (*makeDir)(const char*, mode_t), mode_t mode = 0755);

}  // namespace playfin::core
