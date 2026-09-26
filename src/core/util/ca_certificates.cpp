#include "core/util/ca_certificates.hpp"

#include "core/util/env.hpp"
#include "core/util/log.hpp"

#include <fstream>
#include <string>

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <wincrypt.h>
// clang-format on
#endif

namespace latibot::util {

#ifdef _WIN32

auto export_system_certificates(const std::filesystem::path& destination) -> std::optional<std::filesystem::path> {
    // "ROOT" is the trusted root store: the anchors Windows Update keeps
    // current, and the same set Edge and PowerShell verify against.
    HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
    if (store == nullptr) return std::nullopt;

    std::string bundle;
    int exported = 0;

    PCCERT_CONTEXT certificate = nullptr;
    while ((certificate = CertEnumCertificatesInStore(store, certificate)) != nullptr) {
        if ((certificate->dwCertEncodingType & X509_ASN_ENCODING) == 0) continue;

        // CRYPT_STRING_BASE64HEADER is PEM: the base64 wrapped in the BEGIN
        // and END CERTIFICATE lines OpenSSL looks for.
        DWORD size = 0;
        if (CryptBinaryToStringA(certificate->pbCertEncoded, certificate->cbCertEncoded, CRYPT_STRING_BASE64HEADER, nullptr, &size) ==
            FALSE) {
            continue;
        }

        std::string block(size, '\0');
        if (CryptBinaryToStringA(certificate->pbCertEncoded, certificate->cbCertEncoded, CRYPT_STRING_BASE64HEADER, block.data(), &size) ==
            FALSE) {
            continue;
        }

        block.resize(size); // the reported size excludes the terminator
        bundle += block;
        ++exported;
    }

    CertCloseStore(store, 0);

    // An empty bundle is worse than none: it would point OpenSSL at a file
    // that trusts nothing, instead of leaving its defaults in place.
    if (exported == 0) return std::nullopt;

    if (destination.has_parent_path() && !destination.parent_path().empty()) {
        std::error_code ignored;
        std::filesystem::create_directories(destination.parent_path(), ignored);
    }

    std::ofstream file(destination, std::ios::binary | std::ios::trunc);
    if (!file) return std::nullopt;
    file << bundle;
    if (!file) return std::nullopt;

    return destination;
}

#else

auto export_system_certificates(const std::filesystem::path&) -> std::optional<std::filesystem::path> {
    return std::nullopt;
}

#endif

auto use_system_certificates(const std::filesystem::path& destination) -> void {
    for (const char* already : {"SSL_CERT_FILE", "SSL_CERT_DIR"}) {
        if (const auto set = env_var(already); set && !set->empty()) {
            log().debug("{} is set; leaving certificate verification alone", already);
            return;
        }
    }

    const auto written = export_system_certificates(destination);
    if (!written) {
#ifdef _WIN32
        log().warn("could not export the system root certificates; HTTPS may fail verification");
#endif
        return;
    }

    set_env_var("SSL_CERT_FILE", written->string());
    log().debug("exported the system root certificates to {}", written->generic_string());
}

} // namespace latibot::util
