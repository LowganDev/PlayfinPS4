#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/result.hpp"

namespace playfin::net {

// SHA-256 of the DER SubjectPublicKeyInfo of a PEM certificate (what curl's PINNEDPUBLICKEY compares).
// OpenSSL on Linux, mbed TLS on PS4.
std::optional<core::Sha256> spkiSha256FromPem(std::string_view pem);

std::string base64Encode(const std::uint8_t* data, std::size_t size);
// "AB:CD:…" (what the user compares)
std::string hexFingerprint(const core::Sha256& digest);
// "sha256//<base64>" (CURLOPT_PINNEDPUBLICKEY)
std::string curlPin(const core::Sha256& digest);
// "ab12…" (64 lower-case hex digits, as GitHub publishes a file's digest)
std::string hexLower(const core::Sha256& digest);

// SHA-256 of data given in pieces (a download checked as it arrives).
class Sha256Stream {
public:
    Sha256Stream();
    ~Sha256Stream();
    Sha256Stream(const Sha256Stream&) = delete;
    Sha256Stream& operator=(const Sha256Stream&) = delete;
    void update(const void* data, std::size_t size);
    core::Sha256 finish();

private:
    struct State;
    State* state_;
};

}  // namespace playfin::net
