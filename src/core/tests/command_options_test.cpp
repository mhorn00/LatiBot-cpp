// Reading slash command options (core/commands/options).

#include "core/commands/options.hpp"

#include "support/slash_event.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using latibot::testing::bool_option;
using latibot::testing::option;
using latibot::testing::slash_event;

TEST_CASE("each option is read as the type it was declared with", "[commands]") {
    const auto event = slash_event("trigger", "edit",
                                   {option("pattern", std::string("420"), dpp::co_string), option("id", std::int64_t{7}, dpp::co_integer),
                                    bool_option("enabled", false), option("channel", dpp::snowflake{3000}, dpp::co_channel)});

    CHECK(latibot::commands::string_option(event, "pattern") == "420");
    CHECK(latibot::commands::int_option(event, "id") == 7);
    CHECK(latibot::commands::bool_option(event, "enabled") == false);
    CHECK(latibot::commands::snowflake_option(event, "channel") == dpp::snowflake{3000});
}

TEST_CASE("an option left out reads as nothing, not as false or zero", "[commands]") {
    // An edit leaves what it was not told to change, which needs "not given"
    // to be told apart from "false".
    const auto event = slash_event("trigger", "edit");

    CHECK(latibot::commands::string_option(event, "pattern").empty());
    CHECK_FALSE(latibot::commands::int_option(event, "id").has_value());
    CHECK_FALSE(latibot::commands::bool_option(event, "enabled").has_value());
    CHECK_FALSE(latibot::commands::snowflake_option(event, "channel").has_value());
}

TEST_CASE("an option of another type reads as nothing", "[commands]") {
    const auto event = slash_event("trigger", "edit", {option("id", std::string("seven"), dpp::co_string)});
    CHECK_FALSE(latibot::commands::int_option(event, "id").has_value());
}

TEST_CASE("an invoker Discord sent no permissions for has none", "[commands]") {
    // Which is what a DM looks like.
    const auto event = slash_event("urltoggle", "");
    CHECK_FALSE(latibot::commands::invoker_permissions(event).can(dpp::p_manage_guild));
}
