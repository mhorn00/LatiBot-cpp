// The language model's own feature tools: advanced triggers, posting in
// another channel, and what the bot is (src/modules/llm/docs/Language_Model.md §3.4).

#include "bot_tools.hpp"
#include "core/db/database.hpp"
#include "llm_module.hpp"
#include "personas.hpp"

#include "mocks/mock_discord.hpp"
#include "mocks/mock_model_tools.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/permissions.h>

#include <chrono>
#include <cstdint>
#include <string>

using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake here{2000};
constexpr dpp::snowflake general{2001};
constexpr dpp::snowflake member{12};

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::llm::persona_store personas{db};
    latibot::testing::mock_discord discord;
    latibot::testing::mock_model_tools tools;

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::llm::llm_schema());
        tools.channels = {{"general", general}, {"here", here}};
        latibot::llm::add_bot_tools(tools, {.discord = &discord,
                                            .gateway_ping = [] { return 42ms; },
                                            .server_count = [] { return std::size_t{3}; },
                                            .started_at = std::chrono::sys_seconds{std::chrono::seconds{1'789'900'000}}});
    }

    auto as(dpp::snowflake who, std::uint64_t permissions = dpp::p_send_messages) -> latibot::capabilities::model_tool_context {
        return tools.context(guild, here, who, permissions);
    }
};

} // namespace

TEST_CASE("post_in_channel posts elsewhere, as whoever answers, when the asker could post there", "[llm]") {
    fixture test;
    CHECK(test.tools.run("post_in_channel", {{"channel", "general"}, {"text", "stream's up"}}, test.as(member)).is_error);

    test.discord.permissions[{guild, member}] = dpp::p_send_messages;
    const auto posted = test.tools.run("post_in_channel", {{"channel", "#general"}, {"text", "stream's up"}}, test.as(member));
    REQUIRE_FALSE(posted.is_error);
    REQUIRE(test.tools.posted.size() == 1);
    CHECK(test.tools.posted[0].channel_id == general);
    CHECK(test.tools.posted[0].content == "stream's up");

    CHECK(test.tools.run("post_in_channel", {{"channel", "nowhere"}, {"text", "x"}}, test.as(member)).is_error);
    CHECK(test.tools.run("post_in_channel", {{"channel", "here"}, {"text", "x"}}, test.as(member)).is_error);
}

TEST_CASE("bot_info says the version, uptime, servers and latency", "[llm]") {
    fixture test;
    const auto told = test.tools.run("bot_info", nlohmann::json::object(), test.as(member));
    CHECK(told.content.starts_with("LatiBot "));
    CHECK(told.content.contains("up 1d 3h 46m"));
    CHECK(told.content.contains("in 3 server(s)"));
    CHECK(told.content.contains("42ms"));
}
