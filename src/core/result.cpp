#include "core/result.hpp"

namespace playfin::core {

const char* errorKindName(ErrorKind kind) {
    switch (kind) {
    case ErrorKind::InvalidAddress:
        return "InvalidAddress";
    case ErrorKind::Network:
        return "Network";
    case ErrorKind::Timeout:
        return "Timeout";
    case ErrorKind::Tls:
        return "Tls";
    case ErrorKind::CertificateUntrusted:
        return "CertificateUntrusted";
    case ErrorKind::Unauthorized:
        return "Unauthorized";
    case ErrorKind::NotFound:
        return "NotFound";
    case ErrorKind::Server:
        return "Server";
    case ErrorKind::BadResponse:
        return "BadResponse";
    case ErrorKind::UnsupportedServer:
        return "UnsupportedServer";
    case ErrorKind::Cancelled:
        return "Cancelled";
    case ErrorKind::Storage:
        return "Storage";
    }
    return "Unknown";
}

Error makeError(ErrorKind kind, std::string message, int httpStatus) {
    Error error;
    error.kind = kind;
    error.httpStatus = httpStatus;
    error.message = std::move(message);
    return error;
}

}  // namespace playfin::core
