// clock_gettime for the whole program on the PS4 (libc++'s steady_clock, curl's timeouts, our code).
//
// musl's headers carry Linux clock ids, but calls reach the PS4 kernel (FreeBSD) unchanged: CLOCK_MONOTONIC (1)
// read CLOCK_VIRTUAL there, the process's CPU time, so time spent blocked did not count and timeouts never
// expired (discovery never ended, frame times read 0 ms). Linked into the executable itself, this definition wins
// over libkernel's import: it translates the id and asks the kernel through sceKernelClockGettime.
#include <errno.h>
#include <time.h>

extern "C" int sceKernelClockGettime(int clock, struct timespec* ts);

namespace {

int freebsdClock(clockid_t linuxClock) {
    switch (linuxClock) {
    case 0:
        return 0;   // CLOCK_REALTIME
    case 1:
        return 4;   // CLOCK_MONOTONIC
    case 2:
        return 15;  // CLOCK_PROCESS_CPUTIME_ID
    case 3:
        return 14;  // CLOCK_THREAD_CPUTIME_ID
    case 4:
        return 4;   // CLOCK_MONOTONIC_RAW
    case 5:
        return 10;  // CLOCK_REALTIME_COARSE -> CLOCK_REALTIME_FAST
    case 6:
        return 12;  // CLOCK_MONOTONIC_COARSE -> CLOCK_MONOTONIC_FAST
    case 7:
        return 5;   // CLOCK_BOOTTIME -> CLOCK_UPTIME
    default:
        return -1;
    }
}

}  // namespace

extern "C" int clock_gettime(clockid_t clock, struct timespec* ts) {
    const int id = freebsdClock(clock);
    if (id < 0 || ts == nullptr) {
        errno = EINVAL;
        return -1;
    }
    if (sceKernelClockGettime(id, ts) != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
