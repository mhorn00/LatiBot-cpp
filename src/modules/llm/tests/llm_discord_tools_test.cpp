// The tools that look things up in Discord and do things there
// (src/modules/llm/docs/Language_Model.md §3.4), and which a server allows.

#include "aliases.hpp"
#include "core/capabilities/bot_status.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "discord_tools.hpp"
#include "llm_command.hpp"
#include "llm_module.hpp"
#include "reminders.hpp"
#include "tool_switches.hpp"
#include "tools.hpp"

#include "mock_llm.hpp"
#include "mocks/mock_discord.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <string>
#include <vector>

using json = nlohmann::json;
using latibot::llm::tool_context;
using latibot::testing::ran;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake other_guild{2000};
constexpr dpp::snowflake channel{3000};
constexpr dpp::snowflake bot_id{42};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};

/// Noon on 2026-03-15, UTC.
constexpr std::chrono::sys_seconds noon{std::chrono::sys_days{std::chrono::year{2026} / 3 / 15} + 12h};

struct recorded_status final : latibot::capabilities::bot_status {
    std::vector<std::pair<std::string, std::string>> set_to;
    auto set(std::string type, std::string text) -> void override { set_to.emplace_back(std::move(type), std::move(text)); }
};

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::config::guild_settings settings{db};
    latibot::llm::alias_store aliases{db};
    latibot::llm::reminder_store reminders{db};
    latibot::testing::mock_discord discord;
    recorded_status status;
    latibot::llm::tool_registry tools;
    std::vector<std::pair<dpp::message, std::int64_t>> posted;
    bool trusted = true;

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::llm::llm_schema());
        latibot::llm::add_discord_tools(tools, services());
    }

    auto services() -> latibot::llm::discord_tool_services {
        return {
            .discord = &discord,
            .nicknames = nullptr,
            .status = &status,
            .reminders = &reminders,
            .trusted = [this](dpp::snowflake) { return trusted; },
            .bot_id = [] { return bot_id; },
            .post = [this](dpp::message message, std::int64_t persona, dpp::snowflake) -> dpp::task<latibot::ports::result<dpp::message>> {
                message.id = dpp::snowflake{9999};
                posted.emplace_back(message, persona);
                co_return message;
            }};
    }

    /// A request from Alice, with everyone met as the responder meets them.
    auto context(latibot::llm::people& cast) const -> tool_context {
        return {.guild_id = guild,
                .channel_id = channel,
                .author_id = alice,
                .now = noon,
                .cast = &cast,
                .message_id = dpp::snowflake{5000},
                .persona_id = 0,
                .asker_is_bot = false,
                .allowed = {}};
    }

    auto cast() -> latibot::llm::people { return {aliases, discord, guild, bot_id, "LatiBot"}; }

    auto call(const std::string& name, json input, const tool_context& context) -> latibot::llm::tool_result {
        return ran(tools.run({.id = "1", .name = name, .input = std::move(input)}, context));
    }
};

auto message_from(std::uint64_t id, dpp::snowflake author, std::string name, std::string content) -> dpp::message {
    dpp::message message(channel, std::move(content));
    message.id = dpp::snowflake{id};
    message.author.id = author;
    message.author.username = std::move(name);
    return message;
}

} // namespace

TEST_CASE("a message is found by its id, here, or by a link into this server only", "[llm][tools]") {
    using latibot::llm::find_message_place;
    const auto here = find_message_place("123456789012345678", guild, channel);
    REQUIRE(here.has_value());
    CHECK(here->channel_id == channel);

    const auto linked = find_message_place("https://discord.com/channels/1000/3001/123456789012345678", guild, channel);
    REQUIRE(linked.has_value());
    CHECK(linked->channel_id == dpp::snowflake{3001});
    CHECK(linked->message_id == dpp::snowflake{123456789012345678});

    CHECK(find_message_place("https://ptb.discord.com/channels/2000/3001/1", guild, channel).error().contains("another server"));
    CHECK(find_message_place("https://discord.com/channels/@me/3001/1", guild, channel).error().contains("DM"));
    CHECK_FALSE(find_message_place("yesterday's", guild, channel).has_value());
}

TEST_CASE("a reaction takes Unicode as it is, and a custom emoji by this server's name for it", "[llm][tools]") {
    using latibot::llm::reaction_emoji;
    const latibot::ports::server_details server{.name = "test",
                                                .created_at = noon,
                                                .member_count = 2,
                                                .boost_tier = 0,
                                                .boosts = 0,
                                                .channels = {},
                                                .roles = {},
                                                .emoji = {"partyparrot:77"}};
    CHECK(reaction_emoji("👍", server) == "👍");
    CHECK(reaction_emoji(":PartyParrot:", server) == "partyparrot:77");
    CHECK(reaction_emoji("<a:dance:88>", server) == "dance:88");
    CHECK_FALSE(reaction_emoji(":nope:", server).has_value());
}

TEST_CASE("current_time answers in UTC or a named zone, and refuses one that is not", "[llm][tools]") {
    fixture test;
    auto cast = test.cast();
    CHECK(test.call("current_time", json::object(), test.context(cast)).content.starts_with("Sunday 15 March 2026, 12:00"));
    CHECK(test.call("current_time", {{"timezone", "Asia/Tokyo"}}, test.context(cast)).content.starts_with("Sunday 15 March 2026, 21:00"));
    CHECK(test.call("current_time", {{"timezone", "Mars/Olympus"}}, test.context(cast)).is_error);
}

TEST_CASE("get_message shows who wrote it by alias, and never their name", "[llm][tools]") {
    fixture test;
    test.discord.members[{guild, bob}] = {.nickname = "Big Bob", .display_name = "Bobby", .username = "bob_99"};
    test.discord.stored_messages[dpp::snowflake{6000}] = message_from(6000, bob, "bob_99", "Big Bob was here");
    auto cast = test.cast();

    const auto found = test.call("get_message", {{"message", "6000"}}, test.context(cast));
    REQUIRE_FALSE(found.is_error);
    const std::string bob_alias = test.aliases.alias_for(guild, bob);
    CHECK(found.content.contains(std::format("by {}", bob_alias)));
    CHECK_FALSE(found.content.contains("Bob"));
    CHECK_FALSE(found.content.contains("bob_99"));

    CHECK(test.call("get_message", {{"message", "6001"}}, test.context(cast)).is_error);
}

TEST_CASE("search_messages reads back through the channel, by text and by author", "[llm][tools]") {
    fixture test;
    test.discord.message_pages.emplace_back(std::vector<dpp::message>{message_from(4003, alice, "alice", "pizza tonight?"),
                                                                      message_from(4002, bob, "bob", "PIZZA is great"),
                                                                      message_from(4001, bob, "bob", "hello")});
    auto cast = test.cast();
    const auto by_text = test.call("search_messages", {{"query", "pizza"}}, test.context(cast));
    REQUIRE_FALSE(by_text.is_error);
    CHECK(by_text.content.contains("2 match(es)"));
    CHECK(by_text.content.contains("- 4003"));
    CHECK(by_text.content.contains("- 4002"));

    const std::string bob_alias = test.aliases.alias_for(guild, bob);
    test.discord.message_pages.emplace_back(
        std::vector<dpp::message>{message_from(4003, alice, "alice", "pizza tonight?"), message_from(4002, bob, "bob", "PIZZA is great")});
    const auto by_bob = test.call("search_messages", {{"query", "pizza"}, {"author", bob_alias}}, test.context(cast));
    CHECK(by_bob.content.contains("1 match(es)"));
    CHECK_FALSE(by_bob.content.contains("4003"));

    // It starts from the message being answered, and reads no further back
    // than the history goes.
    CHECK(test.discord.history_requests.at(0).before == dpp::snowflake{5000});
    CHECK(test.call("search_messages", {{"author", "unot99"}}, test.context(cast)).is_error);
}

TEST_CASE("user_info describes a member by alias and roles, and server_info the server", "[llm][tools]") {
    fixture test;
    test.discord.members[{guild, bob}] = {.nickname = "Big Bob", .display_name = "Bobby", .username = "bob_99"};
    test.discord.member_info[{guild, bob}] = {.joined_at = noon - std::chrono::days{30},
                                              .created_at = noon - std::chrono::days{400},
                                              .roles = {"Admins", "Pizza Fans"},
                                              .is_bot = false,
                                              .owns_server = true,
                                              .boosting_since = std::nullopt};
    auto cast = test.cast();
    const std::string bob_alias = test.aliases.alias_for(guild, bob);

    const auto info = test.call("user_info", {{"user", bob_alias}}, test.context(cast));
    REQUIRE_FALSE(info.is_error);
    CHECK(info.content.starts_with(bob_alias));
    CHECK(info.content.contains("owns this server"));
    CHECK(info.content.contains("Roles: Admins, Pizza Fans"));
    CHECK_FALSE(info.content.contains("Bob"));

    test.discord.servers[guild] = {
        .name = "Pizza Club",
        .created_at = noon,
        .member_count = 12,
        .boost_tier = 1,
        .boosts = 3,
        .channels =
            {{.id = channel, .guild_id = guild, .name = "general", .kind = "text", .topic = {}, .age_restricted = false, .parent = "Chat"}},
        .roles = {"Admins"},
        .emoji = {"partyparrot:77"}};
    const auto server = test.call("server_info", json::object(), test.context(cast));
    CHECK(server.content.starts_with("Pizza Club"));
    CHECK(server.content.contains("#general (text, in Chat)"));
    CHECK(server.content.contains(":partyparrot:"));
}

TEST_CASE("a tool that changes something acts for people only", "[llm][tools]") {
    fixture test;
    auto cast = test.cast();
    tool_context from_bot = test.context(cast);
    from_bot.asker_is_bot = true;
    const std::string bob_alias = test.aliases.alias_for(guild, bob);

    for (const auto& [name, input] :
         std::vector<std::pair<std::string, json>>{{"set_nickname", {{"user", bob_alias}, {"nickname", "x"}}},
                                                   {"set_status", {{"text", "hi"}}},
                                                   {"add_reaction", {{"emoji", "👍"}}},
                                                   {"pin_message", {{"message", "6000"}}},
                                                   {"create_poll", {{"question", "?"}, {"answers", {"a", "b"}}}},
                                                   {"set_reminder", {{"text", "x"}, {"in_minutes", 5}}}}) {
        INFO(name);
        const auto refused = test.call(name, input, from_bot);
        CHECK(refused.is_error);
        CHECK(refused.content.contains("bot's"));
    }
    CHECK(test.discord.nicknames_set.empty());
    CHECK(test.status.set_to.empty());
    CHECK(test.posted.empty());
}

TEST_CASE("set_nickname renames anyone, for whoever asked, but not the bot", "[llm][tools]") {
    fixture test;
    auto cast = test.cast();
    const std::string bob_alias = test.aliases.alias_for(guild, bob);
    const std::string alice_alias = test.aliases.alias_for(guild, alice);
    (void)cast.meet(alice, "Alice");

    CHECK_FALSE(
        test.call("set_nickname", {{"user", bob_alias}, {"nickname", std::format("<{}:name>'s pal", alice_alias)}}, test.context(cast))
            .is_error);
    REQUIRE(test.discord.nicknames_set.size() == 1);
    CHECK(test.discord.nicknames_set[0].user_id == bob);
    CHECK(test.discord.nicknames_set[0].nickname == "Alice's pal");

    CHECK_FALSE(test.call("set_nickname", {{"user", bob_alias}}, test.context(cast)).is_error);
    CHECK_FALSE(test.discord.nicknames_set[1].nickname.has_value());

    CHECK(test.call("set_nickname", {{"user", bob_alias}, {"nickname", std::string(33, 'x')}}, test.context(cast)).is_error);
    test.discord.nickname_results.emplace_back(
        std::unexpected(latibot::ports::api_error{.http_status = 403, .message = "Missing Permissions"}));
    CHECK(test.call("set_nickname", {{"user", bob_alias}, {"nickname", "x"}}, test.context(cast)).content.contains("Missing Permissions"));
}

TEST_CASE("set_status works from a trusted server only", "[llm][tools]") {
    fixture test;
    auto cast = test.cast();
    test.trusted = false;
    CHECK(test.call("set_status", {{"text", "pizza"}}, test.context(cast)).content.contains("trusted"));
    CHECK(test.status.set_to.empty());

    test.trusted = true;
    CHECK_FALSE(test.call("set_status", {{"text", "pizza"}, {"type", "playing"}}, test.context(cast)).is_error);
    CHECK(test.status.set_to == std::vector<std::pair<std::string, std::string>>{{"playing", "pizza"}});
    CHECK(test.call("set_status", {{"text", "pizza"}, {"type", "dancing"}}, test.context(cast)).is_error);
}

TEST_CASE("pin_message needs Manage Messages from whoever asked", "[llm][tools]") {
    fixture test;
    auto cast = test.cast();
    CHECK(test.call("pin_message", {{"message", "6000"}}, test.context(cast)).content.contains("Manage Messages"));
    CHECK(test.discord.pins.empty());

    test.discord.permissions[{guild, alice}] = dpp::p_manage_messages;
    CHECK_FALSE(test.call("pin_message", {{"message", "6000"}}, test.context(cast)).is_error);
    CHECK_FALSE(test.call("pin_message", {{"message", "6000"}, {"pin", false}}, test.context(cast)).is_error);
    REQUIRE(test.discord.pins.size() == 2);
    CHECK(test.discord.pins[0].pinned);
    CHECK_FALSE(test.discord.pins[1].pinned);
}

TEST_CASE("add_reaction reacts to the message answered, or the one named", "[llm][tools]") {
    fixture test;
    auto cast = test.cast();
    CHECK_FALSE(test.call("add_reaction", {{"emoji", "👍"}}, test.context(cast)).is_error);
    CHECK_FALSE(test.call("add_reaction", {{"emoji", "🍕"}, {"message", "6000"}}, test.context(cast)).is_error);
    REQUIRE(test.discord.reactions_added.size() == 2);
    CHECK(test.discord.reactions_added[0].message_id == dpp::snowflake{5000});
    CHECK(test.discord.reactions_added[1].message_id == dpp::snowflake{6000});
    CHECK(test.call("add_reaction", {{"emoji", ":nope:"}}, test.context(cast)).is_error);
}

TEST_CASE("create_poll posts a poll as whoever is answering", "[llm][tools]") {
    fixture test;
    auto cast = test.cast();
    tool_context as_persona = test.context(cast);
    as_persona.persona_id = 7;
    CHECK_FALSE(test.call("create_poll", {{"question", "Pizza?"}, {"answers", {"yes", "no"}}, {"hours", 2}}, as_persona).is_error);
    REQUIRE(test.posted.size() == 1);
    CHECK(test.posted[0].second == 7);
    REQUIRE(test.posted[0].first.attached_poll.has_value());
    CHECK(test.posted[0].first.attached_poll->answers.size() == 2);

    CHECK(test.call("create_poll", {{"question", "Pizza?"}, {"answers", {"only one"}}}, as_persona).is_error);
}

TEST_CASE("a reminder is kept, then posted when due by whoever set it, mentioning whom it is for", "[llm][tools][coro]") {
    fixture test;
    auto cast = test.cast();
    const std::string bob_alias = test.aliases.alias_for(guild, bob);
    tool_context as_persona = test.context(cast);
    as_persona.persona_id = 7;

    const auto set = test.call("set_reminder", {{"text", "take the pizza out"}, {"in_minutes", 30}, {"user", bob_alias}}, as_persona);
    REQUIRE_FALSE(set.is_error);
    CHECK(set.content.contains("2026-03-15 12:30 UTC"));
    CHECK(test.call("set_reminder", {{"text", "x"}, {"at", "2020-01-01T00:00"}}, test.context(cast)).is_error);
    CHECK(test.call("set_reminder", {{"text", "x"}}, test.context(cast)).is_error);

    const auto services = test.services();
    REQUIRE(latibot::llm::deliver_reminders(services, noon + 10min).sync_wait_for(2s));
    CHECK(test.posted.empty());

    REQUIRE(latibot::llm::deliver_reminders(services, noon + 31min).sync_wait_for(2s));
    REQUIRE(test.posted.size() == 1);
    CHECK(test.posted[0].second == 7);
    CHECK(test.posted[0].first.content == "<@12> ⏰ take the pizza out");
    CHECK(test.posted[0].first.allowed_mentions.users == std::vector<dpp::snowflake>{bob});
    CHECK(test.reminders.count(guild) == 0);
}

TEST_CASE("a server turns tools off for LatiBot and every persona, and a persona never sets the status", "[llm][tools]") {
    fixture test;
    const auto everything = latibot::llm::allowed_tools(test.tools, test.settings, guild, 0);
    CHECK(std::ranges::find(everything, "set_status") != everything.end());
    const auto for_persona = latibot::llm::allowed_tools(test.tools, test.settings, guild, 7);
    CHECK(std::ranges::find(for_persona, "set_status") == for_persona.end());
    CHECK(for_persona.size() + 1 == everything.size());

    CHECK(latibot::llm::switch_tool(test.settings, guild, "pin_message", false));
    CHECK_FALSE(latibot::llm::switch_tool(test.settings, guild, "pin_message", false));
    CHECK(latibot::llm::switch_tool(test.settings, guild, "set_nickname", false));
    const auto narrowed = latibot::llm::allowed_tools(test.tools, test.settings, guild, 0);
    CHECK(narrowed.size() + 2 == everything.size());
    CHECK(latibot::llm::allowed_tools(test.tools, test.settings, other_guild, 0).size() == everything.size());

    // A tool turned off is neither shown nor run.
    auto cast = test.cast();
    tool_context context = test.context(cast);
    context.allowed = narrowed;
    CHECK(test.tools.definitions(context).size() == narrowed.size());
    CHECK(test.call("pin_message", {{"message", "1"}}, context).content.contains("turned off"));

    CHECK(latibot::llm::switch_tool(test.settings, guild, "pin_message", true));
    CHECK(latibot::llm::tools_off(test.settings, guild) == std::set<std::string, std::less<>>{"set_nickname"});

    const std::string listed = latibot::commands::render_tools(test.tools.definitions(), latibot::llm::tools_off(test.settings, guild));
    CHECK(listed.contains("⛔ `set_nickname`"));
    CHECK(listed.contains("✅ `set_status` (LatiBot only)"));
}
