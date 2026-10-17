#pragma once

#include <string>

#include "core/log.hpp"

namespace playfin::app {

struct Options {
    int maxFrames = 0;       // > 0: quit after this many frames
    bool hidden = false;     // no visible window (tests)
    std::string dataDir;     // empty: platform default
    std::string testScript;  // UI test script (Linux test builds only)
    std::string testOutput;  // where test screenshots go
    log::Level logLevel = log::Level::Info;
    std::string screen = "shell";  // first screen: shell or gallery
    bool discovery = true;         // false: never broadcast on the network (tests)
    std::string renderer = "sw";   // sw: CPU drawing (both platforms); gl: NanoVG GLES2 (Linux, comparisons)
    int benchFrames = 0;           // > 0: draw this many frames of the first screen, log the times, quit
    std::string fixedTime;         // tests: the Live TV clock ("2026-10-16T19:45:00Z")
    int utcOffsetMinutes = 0;      // tests, with fixedTime: the local time's distance to UTC
};

// Runs the app until the user quits; returns the process exit code.
int run(const Options& options);

}  // namespace playfin::app
