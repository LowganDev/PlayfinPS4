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

}  // namespace playfin::core
