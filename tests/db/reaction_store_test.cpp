// Reaction statistics on replacement messages (plan v4 §9.6, §9.7).

#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/reactions.hpp"
#include "core/events/replacements.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

using latibot::events::emoji_ref;
using latibot::events::reaction_emoji;
using latibot::events::reaction_store;
using latibot::events::stat_kind;
using latibot::events::stat_query;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{2000};

constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};
constexpr dpp::snowflake carol{13};

constexpr dpp::snowflake alices_link{501};
constexpr dpp::snowflake bobs_link{502};
constexpr dpp::snowflake unattributed{503};
constexpr dpp::snowflake not_ours{999};

const std::chrono::sys_seconds day_one{std::chrono::sys_days{std::chrono::year{2026} / 1 / 1}};

// Functions rather than constants: building one allocates, and a static that
// throws while the test binary is starting cannot be caught.
auto skull() -> const emoji_ref& {
    static const emoji_ref made = reaction_emoji({}, "💀");
    return made;
}

auto laugh() -> const emoji_ref& {
    static const emoji_ref made = reaction_emoji({}, "😂");
    return made;
}

auto custom_skull() -> const emoji_ref& {
    static const emoji_ref made = reaction_emoji(dpp::snowflake{7001}, "skull");
    return made;
}

auto custom_skull_again() -> const emoji_ref& {
    static const emoji_ref made = reaction_emoji(dpp::snowflake{7002}, "Skull");
    return made;
}

struct store_fixture {
    latibot::db::database db{":memory:"};
    latibot::events::replacement_store replacements{db};
    reaction_store reactions{db};

    store_fixture() {
        latibot::db::migrate(db);
        replacement(alices_link, alice, day_one);
        replacement(bobs_link, bob, day_one + 24h);
        replacement(unattributed, std::nullopt, day_one + 48h);
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

    [[nodiscard]] auto count(stat_kind kind, std::optional<dpp::snowflake> who = std::nullopt,
                             std::optional<std::string> emoji = std::nullopt) const -> std::int64_t {
        return reactions.total(guild, {.kind = kind, .emoji_key = std::move(emoji), .user_id = who, .since = {}, .until = {}});
    }
};

} // namespace

// --------------------------------------------------------------------------
// Recording
// --------------------------------------------------------------------------

TEST_CASE("only reactions on our replacements are counted", "[db]") {
    store_fixture fixture;

    CHECK(fixture.reactions.add(alices_link, bob, skull(), day_one));
    CHECK_FALSE(fixture.reactions.add(not_ours, bob, skull(), day_one));

    // The same reaction twice is one reaction.
    CHECK_FALSE(fixture.reactions.add(alices_link, bob, skull(), day_one + 1s));

    CHECK(fixture.count(stat_kind::received) == 1);
}

TEST_CASE("taking a reaction back removes it", "[db]") {
    store_fixture fixture;
    fixture.reactions.add(alices_link, bob, skull(), day_one);

    CHECK(fixture.reactions.remove(alices_link, bob, skull().key, day_one + 1min));
    CHECK_FALSE(fixture.reactions.remove(alices_link, bob, skull().key, day_one + 2min));
    CHECK(fixture.count(stat_kind::received) == 0);
}

TEST_CASE("a moderator clearing reactions clears the counts", "[db]") {
    store_fixture fixture;
    fixture.reactions.add(alices_link, bob, skull(), day_one);
    fixture.reactions.add(alices_link, carol, skull(), day_one);
    fixture.reactions.add(alices_link, carol, laugh(), day_one);

    CHECK(fixture.reactions.remove_emoji(alices_link, skull().key, day_one + 1h) == 2);
    CHECK(fixture.count(stat_kind::received) == 1);

    CHECK(fixture.reactions.remove_all(alices_link, day_one + 2h) == 1);
    CHECK(fixture.count(stat_kind::received) == 0);
}

// --------------------------------------------------------------------------
// Received, given and self
// --------------------------------------------------------------------------

TEST_CASE("received, given and self-reactions are counted apart", "[db]") {
    store_fixture fixture;
    fixture.reactions.add(alices_link, bob, skull(), day_one);    // bob gives alice a skull
    fixture.reactions.add(alices_link, carol, skull(), day_one);  // carol gives alice a skull
    fixture.reactions.add(alices_link, alice, laugh(), day_one);  // alice laughs at her own link
    fixture.reactions.add(bobs_link, alice, skull(), day_one);    // alice gives bob a skull
    fixture.reactions.add(unattributed, carol, laugh(), day_one); // nobody knows who posted this one

    SECTION("received goes to the poster, never counting their own") {
        CHECK(fixture.count(stat_kind::received, alice) == 2);
        CHECK(fixture.count(stat_kind::received, bob) == 1);

        const auto board = fixture.reactions.leaderboard(guild, {.kind = stat_kind::received}, 10);
        REQUIRE(board.size() == 2);
        CHECK(board[0].user_id == alice);
        CHECK(board[0].count == 2);
    }

    SECTION("given goes to the reactor, and an unknown poster still counts") {
        CHECK(fixture.count(stat_kind::given, alice) == 1);
        CHECK(fixture.count(stat_kind::given, carol) == 2);
    }

    SECTION("self-reactions are their own statistic") {
        CHECK(fixture.count(stat_kind::self, alice) == 1);
        CHECK(fixture.count(stat_kind::self) == 1);
    }

    SECTION("one emoji at a time") {
        CHECK(fixture.count(stat_kind::received, std::nullopt, skull().key) == 3);
        CHECK(fixture.count(stat_kind::received, alice, laugh().key) == 0);
    }

    SECTION("an emoji breakdown is most used first") {
        const auto given = fixture.reactions.emoji_breakdown(guild, {.kind = stat_kind::given, .user_id = carol}, 3);
        REQUIRE(given.size() == 2);
        CHECK(given[0].count == 1);
    }
}

TEST_CASE("a backfilled reaction is dated by its message", "[db]") {
    // Discord never says when a reaction was added (plan v4 §9.7).
    store_fixture fixture;
    const std::vector<reaction_store::observed> seen{{.user_id = carol, .emoji_key = skull().key}};
    fixture.reactions.replace_for_message(bobs_link, seen);

    const stat_query before_bob{.kind = stat_kind::received, .since = {}, .until = day_one + 24h};
    const stat_query from_bob{.kind = stat_kind::received, .since = day_one + 24h, .until = {}};
    CHECK(fixture.reactions.total(guild, before_bob) == 0);
    CHECK(fixture.reactions.total(guild, from_bob) == 1);
}

TEST_CASE("a live reaction is dated when it was added", "[db]") {
    store_fixture fixture;
    fixture.reactions.add(alices_link, bob, skull(), day_one + 30 * 24h);

    const stat_query first_week{.kind = stat_kind::received, .since = day_one, .until = day_one + 7 * 24h};
    CHECK(fixture.reactions.total(guild, first_week) == 0);
}

// --------------------------------------------------------------------------
// Backfill
// --------------------------------------------------------------------------

TEST_CASE("rebuilding a message's reactions is safe to repeat", "[db]") {
    store_fixture fixture;
    fixture.reactions.add(alices_link, bob, skull(), day_one + 10 * 24h);

    const std::vector<reaction_store::observed> seen{{.user_id = bob, .emoji_key = skull().key},
                                                     {.user_id = carol, .emoji_key = laugh().key}};
    CHECK(fixture.reactions.replace_for_message(alices_link, seen) == 2);
    CHECK(fixture.reactions.replace_for_message(alices_link, seen) == 2);
    CHECK(fixture.count(stat_kind::received) == 2);

    // Bob's live reaction kept the time it was seen being added; Carol's,
    // backfilled, only has the message's.
    const stat_query later{.kind = stat_kind::received, .since = day_one + 1s, .until = {}};
    CHECK(fixture.reactions.total(guild, later) == 1);

    SECTION("a reaction gone from Discord goes from the counts") {
        const std::vector<reaction_store::observed> now{{.user_id = carol, .emoji_key = laugh().key}};
        CHECK(fixture.reactions.replace_for_message(alices_link, now) == 1);
        CHECK(fixture.count(stat_kind::given, bob) == 0);
    }
}

// --------------------------------------------------------------------------
// Aliases
// --------------------------------------------------------------------------

TEST_CASE("an alias merges one emoji into another across all history, and can be undone", "[db]") {
    store_fixture fixture;
    fixture.reactions.add(alices_link, bob, custom_skull(), day_one);
    fixture.reactions.add(alices_link, carol, custom_skull_again(), day_one);

    CHECK(fixture.count(stat_kind::received, std::nullopt, custom_skull().key) == 1);

    REQUIRE_FALSE(fixture.reactions.set_alias(guild, custom_skull_again().key, custom_skull().key).has_value());
    CHECK(fixture.count(stat_kind::received, std::nullopt, custom_skull().key) == 2);
    // Asking for the alias asks for what it counts as.
    CHECK(fixture.count(stat_kind::received, std::nullopt, custom_skull_again().key) == 2);

    const auto breakdown = fixture.reactions.emoji_breakdown(guild, {.kind = stat_kind::received}, 10);
    REQUIRE(breakdown.size() == 1);
    CHECK(breakdown[0].emoji.key == custom_skull().key);

    CHECK(fixture.reactions.remove_alias(guild, custom_skull_again().key));
    CHECK(fixture.count(stat_kind::received, std::nullopt, custom_skull().key) == 1);
    CHECK_FALSE(fixture.reactions.remove_alias(guild, custom_skull_again().key));
}

TEST_CASE("alias chains are flattened as they are written", "[db]") {
    store_fixture fixture;
    const std::string first = "c:1";
    const std::string second = "c:2";
    const std::string third = "c:3";

    REQUIRE_FALSE(fixture.reactions.set_alias(guild, first, second).has_value());
    REQUIRE_FALSE(fixture.reactions.set_alias(guild, second, third).has_value());

    // first pointed at second, which now points at third; first follows.
    CHECK(fixture.reactions.canonical(guild, first) == third);
    CHECK(fixture.reactions.canonical(guild, second) == third);

    SECTION("an alias to something that is itself an alias goes to the end") {
        REQUIRE_FALSE(fixture.reactions.set_alias(guild, "c:4", first).has_value());
        CHECK(fixture.reactions.canonical(guild, "c:4") == third);
    }
}

TEST_CASE("an alias that would loop is refused", "[db]") {
    store_fixture fixture;
    REQUIRE_FALSE(fixture.reactions.set_alias(guild, "c:1", "c:2").has_value());

    CHECK(fixture.reactions.set_alias(guild, "c:2", "c:1").has_value());
    CHECK(fixture.reactions.set_alias(guild, "c:1", "c:1").has_value());
}

TEST_CASE("aliases belong to one guild", "[db]") {
    store_fixture fixture;
    REQUIRE_FALSE(fixture.reactions.set_alias(guild, "c:1", "c:2").has_value());
    CHECK(fixture.reactions.canonical(dpp::snowflake{77}, "c:1") == "c:1");
}

// --------------------------------------------------------------------------
// Knowing the emojis
// --------------------------------------------------------------------------

TEST_CASE("emojis are known by name once somebody has used them", "[db]") {
    store_fixture fixture;
    fixture.reactions.add(alices_link, bob, custom_skull(), day_one);
    fixture.reactions.add(alices_link, carol, custom_skull_again(), day_one);
    fixture.reactions.add(bobs_link, carol, laugh(), day_one);

    const auto skulls = fixture.reactions.known_emojis(guild, "SKULL", 25);
    CHECK(skulls.size() == 2);

    CHECK(fixture.reactions.describe(custom_skull().key).name == "skull");
    CHECK(fixture.reactions.describe("c:424242").name.empty());
    CHECK(fixture.reactions.describe("u:🔥").name == "🔥");

    const auto duplicates = fixture.reactions.likely_duplicates(guild);
    REQUIRE(duplicates.size() == 1);
    CHECK(duplicates[0].size() == 2);
}
