#pragma once

namespace playfin::core {

// PS4: appends "<step> <value>" to /data/Playfin/boot.txt with raw kernel calls (no C library stdio, no
// buffering), so the last step before a crash is on disk even when the log file cannot be opened. The file is
// truncated at the first step of each run (a constructor, before main). No-op elsewhere.
void bootMark(const char* step, long value = 0);

}  // namespace playfin::core
