// getaddrinfo() for the whole program on the PS4 (FFmpeg's connections in particular).
//
// musl asks the DNS server for the IPv4 (A) and IPv6 (AAAA) addresses together and waits for both answers; behind
// a router that does not answer AAAA queries, every host name took musl's whole timeout (5 s) before its IPv4
// address was used (measured on the console with a channel's own stream, 2026-11-09). The console's network is
// IPv4 (curl already resolves IPv4 only): a caller without a preference gets IPv4 only. The executable is linked
// with --wrap=getaddrinfo.
#include <netdb.h>
#include <sys/socket.h>

extern "C" int __real_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints,
                                  struct addrinfo** result);

extern "C" int __wrap_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints,
                                  struct addrinfo** result) {
    struct addrinfo ipv4 {};
    if (hints != nullptr) {
        ipv4 = *hints;
    }
    if (ipv4.ai_family == AF_UNSPEC) {
        ipv4.ai_family = AF_INET;
    }
    return __real_getaddrinfo(node, service, &ipv4, result);
}
