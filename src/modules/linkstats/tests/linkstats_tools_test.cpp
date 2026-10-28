// The language model's link_stats (src/modules/linkstats/docs/Link_Stats.md
// §1.1): the boards and counts, with people by alias.

#include "linkstats_tools.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "links/replacements.hpp"
#include "reactions.hpp"

#include "links/module.hpp"
#include "mocks/mock_model_tools.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>

using latibot::events::reaction_emoji;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{2000};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};
constexpr dpp::snowflake carol{13};
constexpr dpp::snowflake alices_link{501};
constexpr dpp::snowflake bobs_link{502};

const std::chrono::sys_seconds day_one{std::chrono::sys_days{std::chrono::year{2026} / 1 / 1}};

struct fixture {
    latibot::db::database db{":memory:"};
    latibot::config::guild_settings settings{db};
    latibot::events::replacement_store replacements{db};
    latibot::events::reaction_store reactions{db};
    latibot::testing::mock_model_tools tools;

    fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::links::schema());
        latibot::db::apply_schema(db, latibot::events::linkstats_schema());
        replacement(alices_link, alice, day_one);
        replacement(bobs_link, bob, day_one + 24h);
        latibot::events::add_linkstats_tools(tools, reactions, &settings);
        tools.aliases = {{"ualice", alice}, {"ubob", bob}, {"ucarol", carol}};

        // Alice's link: two skulls and a laugh; Bob's: one skull, from Alice, on day two.
        reactions.add(alices_link, bob, reaction_emoji({}, "💀"), day_one + 1h);
        reactions.add(alices_link, carol, reaction_emoji({}, "💀"), day_one + 2h);
        reactions.add(alices_link, carol, reaction_emoji(dpp::snowflake{7001}, "partyparrot"), day_one + 3h);
        reactions.add(bobs_link, alice, reaction_emoji({}, "💀"), day_one + 25h);
    }

    auto replacement(dpp::snowflake id, std::optional<dpp::snowflake> author, std::chrono::sys_seconds at) -> void {
        replacements.record({.message_id = id,
                             .guild_id = guild,
                             .channel_id = channel,
                             .original_message_id = std::nullopt,
                             .original_author_id = author,
                             .state = latibot::events::replacement_state::ok,
                             .created_at = at,
                             .retried_at = std::nullopt,
                             .links = {}});
    }

    auto run(nlohmann::json input) -> latibot::capabilities::model_tool_result {
        return tools.run("link_stats", std::move(input), tools.context(guild, channel, alice));
    }
};

} // namespace

TEST_CASE("link_stats ranks people by alias, received by default", "[linkstats]") {
    fixture test;
    const auto result = test.run(nlohmann::json::object());
    CHECK_FALSE(result.is_error);
    CHECK(result.content.contains("1. ualice: 3"));
    CHECK(result.content.contains("2. ubob: 1"));

    const auto given = test.run({{"by", "given"}});
    CHECK(given.content.contains("1. ucarol: 2"));
}

TEST_CASE("link_stats counts one person, an emoji, or a range of days", "[linkstats]") {
    fixture test;
    CHECK(test.run({{"user", "ualice"}}).content == "ualice: 3 reaction(s) received");
    CHECK(test.run({{"emoji", "💀"}}).content.contains("ualice: 2"));
    CHECK(test.run({{"emoji", ":partyparrot:"}}).content.contains("ualice: 1"));
    CHECK(test.run({{"since", "2026-01-02"}}).content.contains("1. ubob: 1"));
    CHECK_FALSE(test.run({{"until", "2026-01-01"}}).content.contains("ubob"));
}

TEST_CASE("link_stats lists the emojis, custom ones by name", "[linkstats]") {
    fixture test;
    const auto result = test.run({{"by", "emojis"}});
    CHECK(result.content.contains("1. 💀 ×3"));
    CHECK(result.content.contains(":partyparrot: ×1"));
}

TEST_CASE("link_stats explains what it cannot read", "[linkstats]") {
    fixture test;
    CHECK(test.run({{"by", "loudest"}}).is_error);
    CHECK(test.run({{"user", "unobody"}}).is_error);
    CHECK(test.run({{"emoji", "notanemoji"}}).is_error);
    CHECK(test.run({{"since", "last week"}}).is_error);
    CHECK(test.run({{"since", "2026-02-01"}, {"until", "2026-01-01"}}).is_error);
}
