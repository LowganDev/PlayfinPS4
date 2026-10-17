#include "core/boot_trace.hpp"

#ifdef PLAYFIN_PS4
#include <fcntl.h>
#include <sys/stat.h>
#include <orbis/libkernel.h>

#include <cstdio>
#include <cstring>
#endif

namespace playfin::core {

#ifdef PLAYFIN_PS4

namespace {

int gFd = -1;

}  // namespace

void bootMark(const char* step, long value) {
    if (gFd < 0) {
        sceKernelMkdir("/data/Playfin", 0777);  // the app data folder; fails harmlessly when it exists
        chmod("/data/Playfin", 0777);  // see core::makeDirs
        gFd = sceKernelOpen("/data/Playfin/boot.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (gFd < 0) {
            char text[96];
            std::snprintf(text, sizeof(text), "playfin boot: cannot open boot.txt (0x%08x)\n", static_cast<unsigned>(gFd));
            sceKernelDebugOutText(0, text);  // prints its text as is (no formatting, despite the declaration)
            return;
        }
    }
    char line[256];
    const int length = std::snprintf(line, sizeof(line), "%s %ld\n", step, value);
    if (length > 0) {
        sceKernelWrite(gFd, line, std::strlen(line));
    }
    char text[300];
    std::snprintf(text, sizeof(text), "playfin boot: %s", line);
    sceKernelDebugOutText(0, text);
}

namespace {

// runs before the C++ static objects of the app: proves the process got that far
__attribute__((constructor(101))) void earliest() { bootMark("loaded"); }

}  // namespace

#else

void bootMark(const char*, long) {}

#endif

}  // namespace playfin::core
