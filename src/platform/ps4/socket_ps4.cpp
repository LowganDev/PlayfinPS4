// socket() and socketpair() for the whole program on the PS4 (curl's connections in particular).
//
// musl's headers give SOCK_CLOEXEC and SOCK_NONBLOCK their Linux values (0x80000 and 0x800), which the PS4 kernel
// (FreeBSD) does not know: curl's TCP sockets were refused with "Protocol not supported". The executable is linked
// with --wrap=socket --wrap=socketpair: every call lands here, the flags are removed (there is no exec on the PS4)
// and non-blocking mode is set with fcntl, whose O_NONBLOCK is already the FreeBSD value.
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

extern "C" int __real_socket(int domain, int type, int protocol);
extern "C" int __real_socketpair(int domain, int type, int protocol, int fds[2]);

namespace {

constexpr int kLinuxCloexec = 02000000;
constexpr int kLinuxNonblock = 04000;

bool makeNonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

}  // namespace

extern "C" int __wrap_socket(int domain, int type, int protocol) {
    const bool nonblocking = (type & kLinuxNonblock) != 0;
    const int fd = __real_socket(domain, type & ~(kLinuxCloexec | kLinuxNonblock), protocol);
    if (fd >= 0 && nonblocking && !makeNonblocking(fd)) {
        close(fd);
        return -1;
    }
    return fd;
}

extern "C" int __wrap_socketpair(int domain, int type, int protocol, int fds[2]) {
    const bool nonblocking = (type & kLinuxNonblock) != 0;
    if (__real_socketpair(domain, type & ~(kLinuxCloexec | kLinuxNonblock), protocol, fds) != 0) {
        return -1;
    }
    if (nonblocking && (!makeNonblocking(fds[0]) || !makeNonblocking(fds[1]))) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    return 0;
}
