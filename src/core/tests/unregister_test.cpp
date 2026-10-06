#include "core/commands/unregister.hpp"

#include "mocks/mock_command_host.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>

using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;
using latibot::commands::unregister_all;
using latibot::commands::unregister_report;
using latibot::testing::mock_command_host;

namespace {

constexpr dpp::snowflake first_guild{101};
constexpr dpp::snowflake second_guild{202};

auto run(mock_command_host& host) -> unregister_report {
    const auto report = unregister_all(host).sync_wait_for(2s);
    REQUIRE(report.has_value());
    return *report;
}

} // namespace

TEST_CASE("unregistering deletes the global commands and every server's own", "[commands][coro]") {
    mock_command_host host;
    host.global = {"ping", "speak", "linkstats"};
    host.per_guild[first_guild] = {"old"};
    host.per_guild[second_guild] = {};

    const unregister_report report = run(host);

    CHECK(report.ok());
    CHECK(report.global_deleted == 3);
    CHECK(report.guild_deleted == 1);
    CHECK(report.guilds_with_commands == 1);
    CHECK(report.guilds_checked == 2);
    CHECK(host.global.empty());
    CHECK(host.per_guild[first_guild].empty());
}

TEST_CASE("a set with no commands in it is not deleted", "[commands][coro]") {
    // So a second run changes nothing, and costs one call per server.
    mock_command_host host;
    host.per_guild[first_guild] = {};

    const unregister_report report = run(host);

    CHECK(report.ok());
    CHECK(report.global_deleted == 0);
    CHECK(report.guild_deleted == 0);
    CHECK(report.guilds_checked == 1);
    CHECK(host.deletes == 0);
}

TEST_CASE("a refused deletion is reported and the other servers still cleared", "[commands][coro]") {
    mock_command_host host;
    host.global = {"ping"};
    host.fail_global_delete = "Missing Access";
    host.per_guild[first_guild] = {"old"};
    host.fail_guild_delete[first_guild] = "Unknown Guild";
    host.per_guild[second_guild] = {"older"};

    const unregister_report report = run(host);

    CHECK_FALSE(report.ok());
    REQUIRE(report.failures.size() == 2);
    CHECK_THAT(report.failures[0], ContainsSubstring("globally") && ContainsSubstring("Missing Access"));
    CHECK_THAT(report.failures[1], ContainsSubstring("101") && ContainsSubstring("Unknown Guild"));
    CHECK(report.global_deleted == 0);
    CHECK(report.guild_deleted == 1);
    CHECK(host.per_guild[second_guild].empty());
}

TEST_CASE("the servers not being listable still leaves the global commands deleted", "[commands][coro]") {
    mock_command_host host;
    host.global = {"ping"};
    host.fail_guild_list = "Unauthorized";

    const unregister_report report = run(host);

    CHECK_FALSE(report.ok());
    REQUIRE(report.failures.size() == 1);
    CHECK_THAT(report.failures[0], ContainsSubstring("servers") && ContainsSubstring("Unauthorized"));
    CHECK(report.global_deleted == 1);
    CHECK(host.global.empty());
}

TEST_CASE("the global commands not being listable is reported and nothing global deleted", "[commands][coro]") {
    mock_command_host host;
    host.global = {"ping"};
    host.fail_global_list = "Unauthorized";

    const unregister_report report = run(host);

    CHECK_FALSE(report.ok());
    CHECK(host.global.size() == 1);
    CHECK(host.deletes == 0);
}
