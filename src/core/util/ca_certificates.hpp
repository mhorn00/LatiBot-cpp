#pragma once

#include <filesystem>
#include <optional>

namespace latibot::util {

/// Writes the operating system's trusted root certificates to `destination`
/// as a PEM bundle, and returns the path on success.
///
/// Windows only. Elsewhere this does nothing and returns nothing, because
/// OpenSSL's compiled-in default paths already find the system bundle.
[[nodiscard]] std::optional<std::filesystem::path> export_system_certificates(const std::filesystem::path& destination);

/// Points OpenSSL at the system's root certificates for this process.
///
/// The OpenSSL we link is built by Conan and has no certificates of its own:
/// its OPENSSLDIR is an empty directory in the package cache, so every TLS
/// handshake fails verification. DPP asks OpenSSL for the default verify
/// paths, and those honour SSL_CERT_FILE, so exporting the Windows root store
/// to a file and setting that variable makes verification work without
/// shipping a bundle of our own or depending on one another tool installed.
///
/// Does nothing when SSL_CERT_FILE or SSL_CERT_DIR is already set, so an
/// explicit choice in `.env` or the shell still wins. Failures are not fatal:
/// the connection is what reports them, and it may still succeed.
void use_system_certificates(const std::filesystem::path& destination);

} // namespace latibot::util
