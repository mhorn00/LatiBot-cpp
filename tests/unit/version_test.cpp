#include "core/version.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("version string matches the version constants", "[util]") {
    const std::string expected = std::to_string(latibot::version_major) + "." + std::to_string(latibot::version_minor) + "." +
                                 std::to_string(latibot::version_patch);

    REQUIRE(latibot::version_string() == expected);
}
