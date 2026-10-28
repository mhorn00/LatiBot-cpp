// The language model's /trigger (src/modules/triggers/docs/Triggers.md §2.6).

#include "trigger_tools.hpp"
#include "core/db/database.hpp"
#include "triggers.hpp"

#include "mocks/mock_model_tools.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/permissions.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <string>

using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{3000};
constexpr dpp::snowflake moderator{11};
constexpr dpp::snowflake member{12};

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::events::trigger_store store{db};
    latibot::testing::mock_model_tools tools;

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::events::triggers_schema());
        latibot::events::add_trigger_tools(tools, store);
    }

    auto as_moderator() -> latibot::capabilities::model_tool_context {
        return tools.context(guild, channel, moderator, dpp::p_manage_messages);
    }
    auto as_member() -> latibot::capabilities::model_tool_context { return tools.context(guild, channel, member); }
};

} // namespace

TEST_CASE("add_trigger adds what /trigger add would, for someone who may", "[triggers]") {
    fixture test;
    const auto added = test.tools.run(
        "add_trigger", {{"pattern", "420"}, {"responses", {"nice", " ", "blaze it"}}, {"cooldown", 60}, {"mode", "substring"}},
        test.as_moderator());
    REQUIRE_FALSE(added.is_error);
    CHECK(added.content.contains("2 response(s)"));

    const auto all = test.store.for_guild(guild);
    REQUIRE(all.size() == 1);
    CHECK(all[0].pattern == "420");
    CHECK(all[0].responses.size() == 2);
    CHECK(all[0].cooldown == 60s);
    CHECK(all[0].mode == latibot::events::match_mode::substring);
    CHECK(all[0].enabled);
    CHECK_FALSE(all[0].respond_to_bots);
}

TEST_CASE("adding and removing a trigger need Manage Messages, and listing does not", "[triggers]") {
    fixture test;
    CHECK(test.tools.run("add_trigger", {{"pattern", "hi"}, {"responses", {"hello"}}}, test.as_member()).is_error);
    CHECK(test.store.for_guild(guild).empty());

    test.tools.run("add_trigger", {{"pattern", "hi"}, {"responses", {"hello"}}}, test.as_moderator());
    const std::int64_t id = test.store.for_guild(guild).at(0).id;

    const auto listed = test.tools.run("list_triggers", nlohmann::json::object(), test.as_member());
    CHECK(listed.content.contains(std::format("- {}: \"hi\"", id)));
    CHECK(listed.content.contains("→ hello"));

    CHECK(test.tools.run("remove_trigger", {{"id", id}}, test.as_member()).is_error);
    CHECK_FALSE(test.tools.run("remove_trigger", {{"id", id}}, test.as_moderator()).is_error);
    CHECK(test.store.for_guild(guild).empty());
    CHECK(test.tools.run("remove_trigger", {{"id", id}}, test.as_moderator()).is_error);
}

TEST_CASE("add_trigger needs a pattern and a response, within /trigger's limits", "[triggers]") {
    fixture test;
    CHECK(test.tools.run("add_trigger", {{"responses", {"x"}}}, test.as_moderator()).is_error);
    CHECK(test.tools.run("add_trigger", {{"pattern", "x"}, {"responses", nlohmann::json::array()}}, test.as_moderator()).is_error);
    CHECK(test.tools.run("add_trigger", {{"pattern", std::string(201, 'x')}, {"responses", {"x"}}}, test.as_moderator()).is_error);
    CHECK(test.tools.run("add_trigger", {{"pattern", "x"}, {"responses", {std::string(2001, 'x')}}}, test.as_moderator()).is_error);
    CHECK(test.store.for_guild(guild).empty());
}
