// The language model's /midnight (src/modules/midnight/docs/Midnight.md §2.3).

#include "midnight_tools.hpp"
#include "core/db/database.hpp"
#include "midnight.hpp"

#include "mocks/mock_model_tools.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/permissions.h>

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{3000};
constexpr dpp::snowflake general{3001};
constexpr dpp::snowflake admin{11};
constexpr dpp::snowflake member{12};

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::events::midnight_store store{db};
    latibot::testing::mock_model_tools tools;

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::events::midnight_schema());
        latibot::events::add_midnight_tools(tools, store);
        tools.channels = {{"general", general}};
    }

    auto as_admin() -> latibot::capabilities::model_tool_context { return tools.context(guild, channel, admin, dpp::p_manage_guild); }
    auto as_member() -> latibot::capabilities::model_tool_context { return tools.context(guild, channel, member); }
};

} // namespace

TEST_CASE("add_midnight adds an entry that first posts at the next midnight", "[midnight]") {
    fixture test;
    const auto added = test.tools.run(
        "add_midnight", {{"timezone", "America/Chicago"}, {"message", "it's midnight"}, {"channel", "#general"}}, test.as_admin());
    REQUIRE_FALSE(added.is_error);

    const auto all = test.store.for_guild(guild);
    REQUIRE(all.size() == 1);
    CHECK(all[0].channel_id == general);
    CHECK(all[0].timezone == "America/Chicago");
    CHECK(all[0].message == "it's midnight");
    CHECK_FALSE(all[0].last_fired_date.empty());

    // Here, when no channel is named.
    test.tools.run("add_midnight", {{"timezone", "Europe/London"}, {"message", "hi"}}, test.as_admin());
    CHECK(test.store.for_guild(guild).at(1).channel_id == channel);
}

TEST_CASE("add_midnight refuses what /midnight add would, and anyone without Manage Server", "[midnight]") {
    fixture test;
    CHECK(test.tools.run("add_midnight", {{"timezone", "America/Chicago"}, {"message", "x"}}, test.as_member()).is_error);
    CHECK(test.tools.run("add_midnight", {{"timezone", "Mars/Olympus"}, {"message", "x"}}, test.as_admin()).is_error);
    CHECK(test.tools.run("add_midnight", {{"timezone", "America/Chicago"}}, test.as_admin()).is_error);
    CHECK(test.tools.run("add_midnight", {{"timezone", "America/Chicago"}, {"message", "x"}, {"channel", "nowhere"}}, test.as_admin())
              .is_error);
    CHECK(test.store.for_guild(guild).empty());
}

TEST_CASE("edit_midnight changes only what it is given, and list_midnight shows it", "[midnight]") {
    fixture test;
    test.tools.run("add_midnight", {{"timezone", "America/Chicago"}, {"message", "midnight"}}, test.as_admin());
    const std::int64_t id = test.store.for_guild(guild).at(0).id;

    CHECK(test.tools.run("edit_midnight", {{"id", id}, {"enabled", false}}, test.as_member()).is_error);
    CHECK_FALSE(test.tools.run("edit_midnight", {{"id", id}, {"enabled", false}, {"message", "late"}}, test.as_admin()).is_error);
    const auto changed = test.store.find(id, guild);
    REQUIRE(changed);
    CHECK_FALSE(changed->enabled);
    CHECK(changed->message == "late");
    CHECK(changed->timezone == "America/Chicago");
    CHECK(test.tools.run("edit_midnight", {{"id", id + 1}}, test.as_admin()).is_error);

    const auto listed = test.tools.run("list_midnight", nlohmann::json::object(), test.as_member());
    CHECK(listed.content.contains(std::format("- {}:", id)));
    CHECK(listed.content.contains("America/Chicago, off"));
    CHECK(listed.content.contains(": late"));
}
