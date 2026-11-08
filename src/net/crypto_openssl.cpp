#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <climits>

#include "net/crypto.hpp"

namespace playfin::net {

std::optional<core::Sha256> spkiSha256FromPem(std::string_view pem) {
    if (pem.empty() || pem.size() > INT_MAX) {
        return std::nullopt;
    }
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (bio == nullptr) {
        return std::nullopt;
    }
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (cert == nullptr) {
        return std::nullopt;
    }
    unsigned char* der = nullptr;
    const int length = i2d_X509_PUBKEY(X509_get_X509_PUBKEY(cert), &der);
    X509_free(cert);
    if (length <= 0 || der == nullptr) {
        return std::nullopt;
    }
    core::Sha256 digest{};
    SHA256(der, static_cast<std::size_t>(length), digest.data());
    OPENSSL_free(der);
    return digest;
}

struct Sha256Stream::State {
    EVP_MD_CTX* context;
};

Sha256Stream::Sha256Stream() : state_(new State{EVP_MD_CTX_new()}) {
    EVP_DigestInit_ex(state_->context, EVP_sha256(), nullptr);
}

Sha256Stream::~Sha256Stream() {
    EVP_MD_CTX_free(state_->context);
    delete state_;
}

void Sha256Stream::update(const void* data, std::size_t size) { EVP_DigestUpdate(state_->context, data, size); }

core::Sha256 Sha256Stream::finish() {
    core::Sha256 digest{};
    unsigned int length = 0;
    EVP_DigestFinal_ex(state_->context, digest.data(), &length);
    return digest;
}

}  // namespace playfin::net
