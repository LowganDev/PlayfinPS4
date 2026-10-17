#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>

#include <string>

#include "net/crypto.hpp"

namespace playfin::net {

std::optional<core::Sha256> spkiSha256FromPem(std::string_view pem) {
    if (pem.empty() || pem.size() > 64 * 1024) {
        return std::nullopt;
    }
    const std::string text(pem);  // mbed TLS wants the PEM NUL-terminated, length including the NUL
    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);
    if (mbedtls_x509_crt_parse(&cert, reinterpret_cast<const unsigned char*>(text.c_str()), text.size() + 1) != 0) {
        mbedtls_x509_crt_free(&cert);
        return std::nullopt;
    }
    unsigned char buffer[4096];
    // written at the end of the buffer
    const int length = mbedtls_pk_write_pubkey_der(&cert.pk, buffer, sizeof(buffer));
    mbedtls_x509_crt_free(&cert);
    if (length <= 0) {
        return std::nullopt;
    }
    core::Sha256 digest{};
    const auto size = static_cast<std::size_t>(length);
    if (mbedtls_sha256(buffer + sizeof(buffer) - size, size, digest.data(), 0) != 0) {
        return std::nullopt;
    }
    return digest;
}

}  // namespace playfin::net
