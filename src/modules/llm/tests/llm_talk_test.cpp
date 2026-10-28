// talk_to: LatiBot and the personas that talk message each other, posted in
// the channel, and the one messaged answers it
// (src/modules/llm/docs/Language_Model.md §2.11).

#include "aliases.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "llm_module.hpp"
#include "personas.hpp"
#include "talk_tool.hpp"
#include "tool_switches.hpp"
#include "tools.hpp"

#include "mock_llm.hpp"
#include "mocks/mock_discord.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

using json = nlohmann::json;
using latibot::llm::ask_llm;
using latibot::llm::persona;
using latibot::llm::tool_context;
using latibot::testing::ran;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{3000};
constexpr dpp::snowflake bot_id{42};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake hook{8500};

/// Noon on 2026-03-15, UTC.
constexpr std::chrono::sys_seconds noon{std::chrono::sys_days{std::chrono::year{2026} / 3 / 15} + 12h};

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::config::guild_settings settings{db};
    latibot::llm::persona_store personas{db};
    latibot::llm::alias_store aliases{db};
    latibot::testing::mock_discord discord;
    latibot::llm::handoffs expected;
    latibot::llm::tool_registry tools;

    std::vector<std::pair<dpp::message, std::int64_t>> posted;
    std::vector<ask_llm> handed;
    std::optional<std::string> refuse_turn;

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::llm::llm_schema());
        latibot::llm::add_talk_tool(
            tools,
            {.personas = &personas,
             .expected = &expected,
             .claim_turn = [this](dpp::snowflake, dpp::snowflake) -> std::expected<std::chrono::seconds, std::string> {
                 if (refuse_turn) return std::unexpected(*refuse_turn);
                 return 5s;
             },
             .post = [this](dpp::message message, std::int64_t persona, dpp::snowflake) -> dpp::task<latibot::ports::result<dpp::message>> {
                 message.id = dpp::snowflake{9000 + posted.size()};
                 message.author.id = persona == 0 ? bot_id : hook;
                 posted.emplace_back(message, persona);
                 co_return message;
             },
             .hand_off = [this](ask_llm ask) { handed.push_back(std::move(ask)); }});
    }

    auto make(std::string name, bool talks) -> persona {
        persona made{.id = 0,
                     .guild_id = guild,
                     .name = std::move(name),
                     .avatar_url = {},
                     .model = {},
                     .enabled = true,
                     .created_by = alice,
                     .created_at = noon,
                     .talks = talks};
        made.id = personas.add(made);
        return made;
    }

    auto as(std::int64_t persona_id) const -> tool_context {
        return {.guild_id = guild,
                .channel_id = channel,
                .author_id = alice,
                .now = noon,
                .cast = nullptr,
                .message_id = dpp::snowflake{5000},
                .persona_id = persona_id,
                .asker_is_bot = false,
                .allowed = {}};
    }

    auto talk(std::int64_t from, std::string to, std::string message) -> latibot::llm::tool_result {
        return ran(tools.run({.id = "1", .name = "talk_to", .input = {{"to", std::move(to)}, {"message", std::move(message)}}}, as(from)));
    }
};

} // namespace

TEST_CASE("who talk_to reaches: LatiBot and the personas that talk, never itself", "[llm][talk]") {
    fixture test;
    const persona claude = test.make("Claude", true);
    const persona gemini = test.make("Gemini", true);
    const persona quiet = test.make("Quiet", false);
    const auto all = test.personas.for_guild(guild);

    CHECK(latibot::llm::reachable_by(0, all) == std::vector<std::string>{"Claude", "Gemini"});
    CHECK(latibot::llm::reachable_by(claude.id, all) == std::vector<std::string>{"LatiBot", "Gemini"});
    CHECK(latibot::llm::reachable_by(quiet.id, all).empty());
    (void)gemini;
}

TEST_CASE("talk_to posts as the sender, and hands the message to its target, paced", "[llm][talk]") {
    fixture test;
    const persona claude = test.make("Claude", true);

    SECTION("LatiBot to a persona") {
        const auto sent = test.talk(0, "claude", "Claude, what do you make of this?");
        REQUIRE_FALSE(sent.is_error);
        REQUIRE(test.posted.size() == 1);
        CHECK(test.posted[0].second == 0);
        CHECK(test.posted[0].first.content == "Claude, what do you make of this?");

        REQUIRE(test.handed.size() == 1);
        const ask_llm& ask = test.handed[0];
        CHECK(ask.persona_id == claude.id);
        CHECK(ask.how == latibot::llm::approach::addressed);
        CHECK(ask.speaker == "LatiBot");
        CHECK(ask.author_is_bot);
        CHECK(ask.message_id == test.posted[0].first.id);
        CHECK(ask.wait == 5s);

        // Its echo sets nothing else off, once.
        CHECK(test.expected.take(channel, "Claude, what do you make of this?"));
        CHECK_FALSE(test.expected.take(channel, "Claude, what do you make of this?"));
    }

    SECTION("a persona to LatiBot") {
        REQUIRE_FALSE(test.talk(claude.id, "LatiBot", "over to you").is_error);
        CHECK(test.posted.at(0).second == claude.id);
        CHECK(test.handed.at(0).persona_id == 0);
        CHECK(test.handed.at(0).speaker == "Claude");
        CHECK(test.handed.at(0).author_id == hook);
    }

    SECTION("not when the bots have talked enough") {
        test.refuse_turn = "6 bot turns in a row";
        const auto refused = test.talk(0, "Claude", "again?");
        CHECK(refused.is_error);
        CHECK(refused.content.contains("6 bot turns in a row"));
        CHECK(test.posted.empty());
        CHECK(test.handed.empty());
    }
}

TEST_CASE("talk_to reaches only those that talk, and never the sender", "[llm][talk]") {
    fixture test;
    const persona claude = test.make("Claude", true);
    test.make("Quiet", false);

    const auto quiet = test.talk(0, "Quiet", "hello?");
    CHECK(quiet.is_error);
    CHECK(quiet.content.contains("you can reach Claude"));
    CHECK(test.talk(claude.id, "Claude", "talking to myself").is_error);
    CHECK(test.talk(0, "LatiBot", "talking to myself").is_error);
    CHECK(test.talk(0, "Nobody", "hello?").is_error);
    CHECK(test.posted.empty());
}

TEST_CASE("only LatiBot and the personas that talk are given talk_to", "[llm][talk]") {
    fixture test;
    const auto latibot = latibot::llm::allowed_tools(test.tools, test.settings, guild, 0);
    CHECK(std::ranges::find(latibot, "talk_to") != latibot.end());
    const auto talker = latibot::llm::allowed_tools(test.tools, test.settings, guild, 7, true);
    CHECK(std::ranges::find(talker, "talk_to") != talker.end());
    const auto quiet = latibot::llm::allowed_tools(test.tools, test.settings, guild, 7, false);
    CHECK(std::ranges::find(quiet, "talk_to") == quiet.end());
}
