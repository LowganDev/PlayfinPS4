#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace playfin::core {

enum class ErrorKind {
    InvalidAddress,
    Network,
    Timeout,
    Tls,
    CertificateUntrusted,
    Unauthorized,
    NotFound,
    Server,
    BadResponse,
    UnsupportedServer,
    Cancelled,
    Storage,
};

const char* errorKindName(ErrorKind kind);

using Sha256 = std::array<std::uint8_t, 32>;

// The certificate a server presented (for trust on first use).
struct CertificateInfo {
    Sha256 spkiSha256{};  // SHA-256 of the public key (what gets pinned)
    std::string subject;
    std::string issuer;
    std::string notAfter;
};

struct Error {
    ErrorKind kind = ErrorKind::Network;
    int httpStatus = 0;
    std::string message;
    CertificateInfo certificate;  // set for CertificateUntrusted
};

Error makeError(ErrorKind kind, std::string message, int httpStatus = 0);

// A value or an error. No exceptions cross module boundaries.
template <typename T>
class Result {
public:
    Result(T value) : data_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : data_(std::in_place_index<1>, std::move(error)) {}

    bool ok() const { return data_.index() == 0; }
    T& value() { return std::get<0>(data_); }
    const T& value() const { return std::get<0>(data_); }
    const Error& error() const { return std::get<1>(data_); }

private:
    std::variant<T, Error> data_;
};

// Value of the calls that only succeed or fail
struct Unit {};

}  // namespace playfin::core
