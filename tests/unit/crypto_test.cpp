#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include "net/crypto.hpp"
#include "support/files.hpp"

using playfin::net::base64Encode;

namespace {

std::string b64(const std::string& text) {
    return base64Encode(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

}  // namespace

TEST_CASE("base64 follows RFC 4648") {
    CHECK(b64("") == "");
    CHECK(b64("f") == "Zg==");
    CHECK(b64("fo") == "Zm8=");
    CHECK(b64("foo") == "Zm9v");
    CHECK(b64("foob") == "Zm9vYg==");
    CHECK(b64("fooba") == "Zm9vYmE=");
    CHECK(b64("foobar") == "Zm9vYmFy");
}

TEST_CASE("the public key hash of a certificate matches openssl") {
    const auto digest = playfin::net::spkiSha256FromPem(playfin::test::readFixture("tls/server.crt"));
    REQUIRE(digest.has_value());
    CHECK(playfin::net::curlPin(*digest) == "sha256//e/lJHyILdq+f84w+ilCF7/374MlV1SHk2yfT9CzB3nM=");
    CHECK(playfin::net::hexFingerprint(*digest) ==
          "7B:F9:49:1F:22:0B:76:AF:9F:F3:8C:3E:8A:50:85:EF:FD:FB:E0:C9:55:D5:21:E4:DB:27:D3:F4:2C:C1:DE:73");
    const auto self = playfin::net::spkiSha256FromPem(playfin::test::readFixture("tls/self.crt"));
    REQUIRE(self.has_value());
    CHECK(playfin::net::curlPin(*self) == "sha256//7/0+2QD8rSzUfQ66IS3X1evBTQkOAfJZ7ASxI8fSyTk=");
}

TEST_CASE("invalid certificates give no hash") {
    for (const char* bad : {"", "not a pem", "-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n"}) {
        INFO(bad);
        CHECK_FALSE(playfin::net::spkiSha256FromPem(bad).has_value());
    }
}
