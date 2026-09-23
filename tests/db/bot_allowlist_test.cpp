#include "core/events/bot_allowlist.hpp"

#include "core/db/database.hpp"
#include "core/db/migrations.hpp"

#include <catch2/catch_test_macros.hpp>

using latibot::events::bot_allowlist;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake other_guild{2000};
constexpr dpp::snowflake friendly_bot{55};
constexpr dpp::snowflake another_bot{66};

struct allowlist_fixture {
    latibot::db::database db{":memory:"};
    bot_allowlist allowlist{db};

    allowlist_fixture() { latibot::db::migrate(db); }
};

} // namespace

TEST_CASE("nothing is allowed until somebody says so", "[db]") {
    // The safe default: a new server ignores every bot, because two bots
    // answering each other is the failure this list exists to prevent.
    allowlist_fixture fixture;

    CHECK_FALSE(fixture.allowlist.contains(guild, friendly_bot));
    CHECK(fixture.allowlist.for_guild(guild).empty());
}

TEST_CASE("an allowed bot is remembered and can be taken back", "[db]") {
    allowlist_fixture fixture;

    CHECK(fixture.allowlist.allow(guild, friendly_bot));
    CHECK(fixture.allowlist.contains(guild, friendly_bot));

    CHECK(fixture.allowlist.deny(guild, friendly_bot));
    CHECK_FALSE(fixture.allowlist.contains(guild, friendly_bot));
}

TEST_CASE("allowing and denying report whether anything changed", "[db]") {
    // The command says "i was already listening to that one" rather than
    // claiming a change it did not make.
    allowlist_fixture fixture;

    CHECK(fixture.allowlist.allow(guild, friendly_bot));
    CHECK_FALSE(fixture.allowlist.allow(guild, friendly_bot));

    CHECK(fixture.allowlist.deny(guild, friendly_bot));
    CHECK_FALSE(fixture.allowlist.deny(guild, friendly_bot));
}

TEST_CASE("guilds keep their own allowlists", "[db]") {
    allowlist_fixture fixture;

    fixture.allowlist.allow(guild, friendly_bot);

    CHECK_FALSE(fixture.allowlist.contains(other_guild, friendly_bot));
    CHECK(fixture.allowlist.for_guild(other_guild).empty());
    CHECK_FALSE(fixture.allowlist.deny(other_guild, friendly_bot));
    CHECK(fixture.allowlist.contains(guild, friendly_bot));
}

TEST_CASE("for_guild lists everything allowed there", "[db]") {
    allowlist_fixture fixture;

    fixture.allowlist.allow(guild, another_bot);
    fixture.allowlist.allow(guild, friendly_bot);
    fixture.allowlist.allow(other_guild, friendly_bot);

    const auto listed = fixture.allowlist.for_guild(guild);
    REQUIRE(listed.size() == 2);
    CHECK(listed[0] == friendly_bot); // ordered by id
    CHECK(listed[1] == another_bot);
}
