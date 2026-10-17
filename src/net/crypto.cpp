#include "net/crypto.hpp"

namespace playfin::net {

std::string base64Encode(const std::uint8_t* data, std::size_t size) {
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < size; i += 3) {
        const unsigned value = (unsigned(data[i]) << 16) | (unsigned(data[i + 1]) << 8) | unsigned(data[i + 2]);
        out += kAlphabet[(value >> 18) & 0x3F];
        out += kAlphabet[(value >> 12) & 0x3F];
        out += kAlphabet[(value >> 6) & 0x3F];
        out += kAlphabet[value & 0x3F];
    }
    if (i < size) {
        unsigned value = unsigned(data[i]) << 16;
        if (i + 1 < size) {
            value |= unsigned(data[i + 1]) << 8;
        }
        out += kAlphabet[(value >> 18) & 0x3F];
        out += kAlphabet[(value >> 12) & 0x3F];
        out += i + 1 < size ? kAlphabet[(value >> 6) & 0x3F] : '=';
        out += '=';
    }
    return out;
}

std::string hexFingerprint(const core::Sha256& digest) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (std::size_t i = 0; i < digest.size(); ++i) {
        if (i != 0) {
            out += ':';
        }
        out += kHex[digest[i] >> 4];
        out += kHex[digest[i] & 0x0F];
    }
    return out;
}

std::string curlPin(const core::Sha256& digest) { return "sha256//" + base64Encode(digest.data(), digest.size()); }

}  // namespace playfin::net
