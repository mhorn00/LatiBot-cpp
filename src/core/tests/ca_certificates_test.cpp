#include "core/util/ca_certificates.hpp"

#include "support/temp_directory.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

using latibot::util::export_system_certificates;

namespace {

auto read(const std::filesystem::path& path) -> std::string {
    const std::ifstream file(path, std::ios::binary);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

auto count_of(std::string_view text, std::string_view needle) -> std::size_t {
    std::size_t found = 0;
    for (std::size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + needle.size())) {
        ++found;
    }
    return found;
}

} // namespace

#ifdef _WIN32

TEST_CASE("the system root certificates export as a readable PEM bundle", "[util][fs]") {
    // This is what stands between the bot and "Malformed HTTP response": the
    // OpenSSL we link ships no roots, so these are the only ones it gets.
    const latibot::testing::temp_directory temp;
    const auto written = export_system_certificates(temp.file("ca-bundle.pem"));

    REQUIRE(written.has_value());
    REQUIRE(std::filesystem::exists(*written));

    const std::string bundle = read(*written);
    const std::size_t begins = count_of(bundle, "-----BEGIN CERTIFICATE-----");

    CHECK(begins > 0);
    CHECK(begins == count_of(bundle, "-----END CERTIFICATE-----"));
}

TEST_CASE("exporting creates the directory it was given", "[util][fs]") {
    // main() asks for a path inside the data directory, which on a first run
    // does not exist yet.
    const latibot::testing::temp_directory temp;
    const auto written = export_system_certificates(temp.path() / "not-yet" / "ca-bundle.pem");

    REQUIRE(written.has_value());
    CHECK(std::filesystem::exists(*written));
}

#endif
