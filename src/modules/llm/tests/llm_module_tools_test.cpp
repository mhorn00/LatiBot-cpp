// The other modules' tools, through the model_tools capability
// (src/modules/llm/docs/Language_Model.md §3.4), and the hidden text they may
// hand the model (§3.8).

#include "aliases.hpp"
#include "core/db/database.hpp"
#include "llm_module.hpp"
#include "module_tools.hpp"
#include "tools.hpp"

#include "mocks/mock_discord.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/permissions.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using latibot::capabilities::model_tool;
using latibot::capabilities::model_tool_context;
using latibot::capabilities::model_tool_result;
using latibot::llm::tool_context;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{2000};
constexpr dpp::snowflake bot_id{42};
constexpr dpp::snowflake alice{11};

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::llm::alias_store aliases{db};
    latibot::testing::mock_discord discord;
    latibot::llm::tool_registry registry;

    struct post_seen {
        std::string content;
        std::int64_t persona;
    };
    std::vector<post_seen> posts;

    latibot::llm::module_tools tools{
        registry, discord,
        [this](dpp::message message, std::int64_t persona, dpp::snowflake) -> dpp::task<latibot::ports::result<dpp::message>> {
            posts.push_back({.content = message.content, .persona = persona});
            co_return message;
        }};

    std::vector<model_tool_context> seen;

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::llm::llm_schema());
    }

    /// A tool that notes what it was told and posts what it was given.
    auto add(std::string name, bool changes) -> void {
        tools.add({.name = std::move(name),
                   .description = "a test tool",
                   .input_schema = latibot::capabilities::tool_schema(nlohmann::json::object()),
                   .changes = changes,
                   .run = [this](nlohmann::json input, model_tool_context context) -> dpp::task<model_tool_result> {
                       seen.push_back(context);
                       if (const auto text = latibot::capabilities::tool_text(input, "post")) {
                           std::ignore = co_await context.post(dpp::message(context.channel_id, *text));
                       }
                       co_return latibot::capabilities::tool_done("done");
                   }});
    }

    auto run(std::string name, nlohmann::json input, tool_context context) -> latibot::llm::tool_result {
        auto result =
            registry.run({.id = "call", .name = std::move(name), .input = std::move(input)}, std::move(context)).sync_wait_for(2s);
        REQUIRE(result.has_value());
        return *result;
    }
};

auto asked_by_alice() -> tool_context {
    return {.guild_id = guild,
            .channel_id = channel,
            .author_id = alice,
            .now = {},
            .cast = nullptr,
            .message_id = dpp::snowflake{5},
            .persona_id = 0,
            .asker_is_bot = false,
            .allowed = {}};
}

} // namespace

TEST_CASE("a module's tool joins the model's own, and is told who asked and what they may do", "[llm]") {
    fixture test;
    test.add("look_up", false);
    test.discord.permissions[{guild, alice}] = dpp::p_manage_guild;
    CHECK(test.registry.contains("look_up"));

    const auto result = test.run("look_up", nlohmann::json::object(), asked_by_alice());
    CHECK_FALSE(result.is_error);
    CHECK(result.content == "done");
    REQUIRE(test.seen.size() == 1);
    CHECK(test.seen[0].guild_id == guild);
    CHECK(test.seen[0].channel_id == channel);
    CHECK(test.seen[0].author_id == alice);
    CHECK(test.seen[0].message_id == dpp::snowflake{5});
    CHECK(test.seen[0].author_permissions == dpp::p_manage_guild);
}

TEST_CASE("a module's tool that changes something refuses a bot's message, and one that reads does not", "[llm]") {
    fixture test;
    test.add("look_up", false);
    test.add("change_it", true);
    tool_context from_bot = asked_by_alice();
    from_bot.asker_is_bot = true;

    CHECK_FALSE(test.run("look_up", nlohmann::json::object(), from_bot).is_error);
    const auto refused = test.run("change_it", nlohmann::json::object(), from_bot);
    CHECK(refused.is_error);
    CHECK(refused.content.contains("bot's"));
    CHECK(test.seen.size() == 1);
}

TEST_CASE("a module's tool posts as whoever is answering", "[llm]") {
    fixture test;
    test.add("post_it", true);
    tool_context as_persona = asked_by_alice();
    as_persona.persona_id = 7;

    test.run("post_it", {{"post", "hello"}}, as_persona);
    REQUIRE(test.posts.size() == 1);
    CHECK(test.posts[0].content == "hello");
    CHECK(test.posts[0].persona == 7);
}

TEST_CASE("a module's tool reads and writes people by alias, and can hide a name", "[llm]") {
    fixture test;
    test.add("look_up", false);
    latibot::llm::people cast{test.aliases, test.discord, guild, bot_id, "LatiBot"};
    const std::string alias = cast.meet(alice, "Alice");
    tool_context context = asked_by_alice();
    context.cast = &cast;

    test.run("look_up", nlohmann::json::object(), context);
    REQUIRE(test.seen.size() == 1);
    const model_tool_context& told = test.seen[0];
    CHECK(told.user_for(alias) == alice);
    CHECK(told.shown_as(alice) == alias);
    CHECK(told.sanitize("Alice said hi") == std::format("<{}:name> said hi", alias));

    const std::string first = told.hide("Sir Robert");
    const std::string second = told.hide("Bobby");
    CHECK(first == "<hidden1>");
    CHECK(second == "<hidden2>");
    CHECK(cast.restore(std::format("they were {} before {}", second, first)) == "they were Bobby before Sir Robert");
}

TEST_CASE("hidden text only comes back in its own request", "[llm]") {
    fixture test;
    latibot::llm::people cast{test.aliases, test.discord, guild, bot_id, "LatiBot"};
    std::ignore = cast.hide("Bobby");
    CHECK(cast.restore("<hidden1>") == "Bobby");
    // A memory saved with one, read in a later request.
    latibot::llm::people later{test.aliases, test.discord, guild, bot_id, "LatiBot"};
    CHECK(later.restore("was <hidden1>") == "was something");
    // Text that only looks like one stays.
    CHECK(later.restore("<hiddenx> <hidden>") == "<hiddenx> <hidden>");
}
