#ifdef PLAYFIN_PS4
#include <orbis/SystemService.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/app.hpp"
#include "core/boot_trace.hpp"

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: playfin [--frames N] [--hidden] [--data-dir DIR] [--log-level debug|info|warn|error]\n"
                 "               [--screen shell|gallery] [--renderer sw|gl] [--bench N] [--no-discovery] [--test-script FILE --test-output DIR]\n"
                 "               [--fixed-time 2026-10-16T19:45:00Z --utc-offset MINUTES]\n");
}

bool parseLevel(const char* text, playfin::log::Level& out) {
    using playfin::log::Level;
    if (std::strcmp(text, "debug") == 0) {
        out = Level::Debug;
    } else if (std::strcmp(text, "info") == 0) {
        out = Level::Info;
    } else if (std::strcmp(text, "warn") == 0) {
        out = Level::Warn;
    } else if (std::strcmp(text, "error") == 0) {
        out = Level::Error;
    } else {
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    playfin::core::bootMark("main");
    playfin::app::Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--hidden") {
            options.hidden = true;
        } else if (arg == "--frames" && hasValue) {
            options.maxFrames = std::atoi(argv[++i]);
        } else if (arg == "--data-dir" && hasValue) {
            options.dataDir = argv[++i];
        } else if (arg == "--bench" && hasValue) {
            options.benchFrames = std::atoi(argv[++i]);
        } else if (arg == "--renderer" && hasValue) {
            options.renderer = argv[++i];
        } else if (arg == "--no-discovery") {
            options.discovery = false;
        } else if (arg == "--screen" && hasValue) {
            options.screen = argv[++i];
        } else if (arg == "--test-script" && hasValue) {
            options.testScript = argv[++i];
        } else if (arg == "--test-output" && hasValue) {
            options.testOutput = argv[++i];
        } else if (arg == "--fixed-time" && hasValue) {
            options.fixedTime = argv[++i];
        } else if (arg == "--utc-offset" && hasValue) {
            options.utcOffsetMinutes = std::atoi(argv[++i]);
        } else if (arg == "--log-level" && hasValue && parseLevel(argv[i + 1], options.logLevel)) {
            ++i;
        } else {
            usage();
            return 2;
        }
    }
    if (!options.testScript.empty() && options.testOutput.empty()) {
        std::fprintf(stderr, "--test-script needs --test-output (where the screenshots go)\n");
        usage();
        return 2;
    }
    const int code = playfin::app::run(options);
#ifdef PLAYFIN_PS4
    // the PS4 kernel refuses _exit from an app (SIGSYS, seen on the console): the system closes it instead
    playfin::core::bootMark("exit", code);
    sceSystemServiceLoadExec("exit", nullptr);
#endif
    return code;
}
