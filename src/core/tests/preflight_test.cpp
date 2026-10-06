#include "core/commands/preflight.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <dpp/permissions.h>

#include <array>

using Catch::Matchers::ContainsSubstring;
using latibot::commands::describe_permissions;
using latibot::commands::requirement;
using latibot::commands::unmet;

namespace {

constexpr std::array<requirement, 2> two_features{{
    {.permissions = dpp::p_view_channel | dpp::p_send_messages, .purpose = "replying"},
    {.permissions = dpp::p_connect | dpp::p_speak, .purpose = "speaking"},
}};

} // namespace

TEST_CASE("only the missing bits of a requirement are reported", "[commands]") {
    // Reporting the whole requirement would tell an admin to grant
    // permissions the bot already has.
    const auto gaps = unmet(two_features, dpp::p_view_channel | dpp::p_connect);

    REQUIRE(gaps.size() == 2);
    CHECK(gaps[0].permissions == dpp::p_send_messages);
    CHECK(gaps[0].purpose == "replying");
    CHECK(gaps[1].permissions == dpp::p_speak);
    CHECK(gaps[1].purpose == "speaking");
}

TEST_CASE("a satisfied requirement is not reported", "[commands]") {
    const auto gaps = unmet(two_features, dpp::p_view_channel | dpp::p_send_messages);

    REQUIRE(gaps.size() == 1);
    CHECK(gaps[0].purpose == "speaking");
}

TEST_CASE("administrator satisfies everything", "[commands]") {
    // Discord treats Administrator as every permission at once, so warning
    // about a bit it does not literally carry would be wrong.
    CHECK(unmet(two_features, dpp::p_administrator).empty());
}

TEST_CASE("a requirement of nothing is always met", "[commands]") {
    constexpr std::array<requirement, 1> nothing{{{.permissions = 0, .purpose = "a free feature"}}};
    CHECK(unmet(nothing, 0).empty());
}

TEST_CASE("permissions are described by name", "[commands]") {
    CHECK(describe_permissions(0).empty());
    CHECK(describe_permissions(dpp::p_speak) == "Speak");
    CHECK(describe_permissions(dpp::p_connect | dpp::p_speak) == "Connect, Speak");

    SECTION("a permission with no name still shows up") {
        // The table only covers what this bot uses. A bit outside it must not
        // vanish, or a warning would name fewer permissions than are missing.
        constexpr std::uint64_t unlisted = dpp::p_moderate_members;
        CHECK_THAT(describe_permissions(dpp::p_speak | unlisted), ContainsSubstring("Speak"));
        CHECK_THAT(describe_permissions(dpp::p_speak | unlisted), ContainsSubstring("0x"));
    }
}
