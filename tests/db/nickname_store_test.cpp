#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/nicknames.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>

using latibot::events::nickname_change;
using latibot::events::nickname_source;
using latibot::events::nickname_store;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake other_guild{2000};
constexpr dpp::snowflake member{3000};
constexpr dpp::snowflake moderator{4000};

struct store_fixture {
    latibot::db::database db{":memory:"};
    nickname_store store{db};

    store_fixture() { latibot::db::migrate(db); }
};

/// A fixed instant, so "recorded a minute ago" is exact rather than racy.
const auto noon = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::September / 23} + 12h;

nickname_change change_to(std::optional<std::string> nickname, std::chrono::system_clock::time_point when = noon) {
    return {.guild_id = guild, .user_id = member, .nickname = std::move(nickname), .changed_at = when};
}

} // namespace

TEST_CASE("a recorded change comes back as it went in", "[db]") {
    store_fixture fixture;

    nickname_change written = change_to("worm scientist");
    written.changed_by = moderator;
    written.source = nickname_source::command;

    const std::int64_t id = fixture.store.record(written);
    REQUIRE(id != 0);

    const auto read = fixture.store.find(id);
    REQUIRE(read.has_value());
    CHECK(read->guild_id == guild);
    CHECK(read->user_id == member);
    CHECK(read->nickname == "worm scientist");
    CHECK(read->changed_at == noon);
    CHECK(read->changed_by == moderator);
    CHECK(read->source == nickname_source::command);
    CHECK(read->imported_raw.empty());
}

TEST_CASE("a cleared nickname is stored as nothing, not as an empty string", "[db]") {
    store_fixture fixture;

    // One of the three Java bugs being fixed while porting (plan v4 §8.2):
    // there, a cleared nickname became a null that later crashed the display.
    const std::int64_t id = fixture.store.record(change_to(std::nullopt));

    const auto read = fixture.store.find(id);
    REQUIRE(read.has_value());
    CHECK_FALSE(read->nickname.has_value());
}

TEST_CASE("history reads newest first", "[db]") {
    store_fixture fixture;

    fixture.store.record(change_to("first", noon));
    fixture.store.record(change_to("second", noon + 1h));
    fixture.store.record(change_to("third", noon + 2h));

    const auto history = fixture.store.history(guild, member);
    REQUIRE(history.size() == 3);
    CHECK(history[0].nickname == "third");
    CHECK(history[1].nickname == "second");
    CHECK(history[2].nickname == "first");
}

TEST_CASE("two changes in the same second keep the order they were recorded", "[db]") {
    store_fixture fixture;

    // Only the second is stored, so a rename undone immediately would
    // otherwise read back in whichever order SQLite felt like.
    fixture.store.record(change_to("earlier", noon));
    fixture.store.record(change_to("later", noon));

    const auto history = fixture.store.history(guild, member);
    REQUIRE(history.size() == 2);
    CHECK(history[0].nickname == "later");
}

TEST_CASE("history is per guild", "[db]") {
    store_fixture fixture;

    fixture.store.record(change_to("here"));

    nickname_change elsewhere = change_to("there");
    elsewhere.guild_id = other_guild;
    fixture.store.record(elsewhere);

    CHECK(fixture.store.count(guild, member) == 1);
    CHECK(fixture.store.count(other_guild, member) == 1);
    CHECK(fixture.store.history(guild, member).front().nickname == "here");
}

TEST_CASE("the latest row is what a new sighting is compared against", "[db]") {
    store_fixture fixture;

    CHECK_FALSE(fixture.store.latest(guild, member).has_value());

    fixture.store.record(change_to("first", noon));
    fixture.store.record(change_to("current", noon + 1h));

    const auto latest = fixture.store.latest(guild, member);
    REQUIRE(latest.has_value());
    CHECK(latest->nickname == "current");
}

TEST_CASE("an audit entry finds the row it describes", "[db]") {
    store_fixture fixture;

    const std::int64_t id = fixture.store.record(change_to("worm scientist", noon));

    const auto found = fixture.store.unattributed(guild, member, "worm scientist", noon + 3s, 10s);
    REQUIRE(found.has_value());
    CHECK(found->id == id);
}

TEST_CASE("an audit entry does not attach itself to an older identical change", "[db]") {
    store_fixture fixture;

    // Somebody used this nickname last week and is using it again now. Without
    // the window the audit entry would credit the wrong row.
    fixture.store.record(change_to("worm scientist", noon - 168h));

    CHECK_FALSE(fixture.store.unattributed(guild, member, "worm scientist", noon, 10s).has_value());
}

TEST_CASE("an audit entry for a different nickname matches nothing", "[db]") {
    store_fixture fixture;

    fixture.store.record(change_to("worm scientist", noon));

    CHECK_FALSE(fixture.store.unattributed(guild, member, "something else", noon + 1s, 10s).has_value());
}

TEST_CASE("a row that already names somebody is not offered for attribution", "[db]") {
    store_fixture fixture;

    nickname_change known = change_to("worm scientist", noon);
    known.changed_by = moderator;
    fixture.store.record(known);

    CHECK_FALSE(fixture.store.unattributed(guild, member, "worm scientist", noon + 1s, 10s).has_value());
}

TEST_CASE("attributing a row fills in the author and where it came from", "[db]") {
    store_fixture fixture;

    const std::int64_t id = fixture.store.record(change_to("worm scientist"));

    REQUIRE(fixture.store.attribute(id, moderator, nickname_source::audit_log));

    const auto read = fixture.store.find(id);
    REQUIRE(read.has_value());
    CHECK(read->changed_by == moderator);
    CHECK(read->source == nickname_source::audit_log);
}

TEST_CASE("the first audit entry to attribute a row wins", "[db]") {
    store_fixture fixture;

    // Two entries can describe the same change: the gateway one and the
    // ten-second safety-net query. The second must not overwrite the first.
    const std::int64_t id = fixture.store.record(change_to("worm scientist"));

    REQUIRE(fixture.store.attribute(id, moderator, nickname_source::audit_log));
    CHECK_FALSE(fixture.store.attribute(id, dpp::snowflake{5000}, nickname_source::audit_log));

    CHECK(fixture.store.find(id)->changed_by == moderator);
}

TEST_CASE("a change that did not go through can be taken back", "[db]") {
    store_fixture fixture;

    // `/nickname` records first and asks Discord second, so a refusal has to
    // leave nothing behind: history should never claim something that did not
    // happen (plan v4 §8.1).
    const std::int64_t id = fixture.store.record(change_to("worm scientist"));

    CHECK(fixture.store.remove(id));
    CHECK(fixture.store.count(guild, member) == 0);
    CHECK_FALSE(fixture.store.remove(id));
}

TEST_CASE("the members of a guild are listed once each", "[db]") {
    store_fixture fixture;

    fixture.store.record(change_to("one", noon));
    fixture.store.record(change_to("two", noon + 1h));

    nickname_change other = change_to("elsewhere");
    other.user_id = dpp::snowflake{5555};
    fixture.store.record(other);

    const auto members = fixture.store.members(guild);
    REQUIRE(members.size() == 2);
    CHECK(fixture.store.members(other_guild).empty());
}

TEST_CASE("an imported row keeps the text its timestamp was read from", "[db]") {
    store_fixture fixture;

    nickname_change imported = change_to("worm scientist");
    imported.source = nickname_source::imported;
    imported.imported_raw = "2023-11-25 01:58:23";

    const auto read = fixture.store.find(fixture.store.record(imported));
    REQUIRE(read.has_value());
    CHECK(read->imported_raw == "2023-11-25 01:58:23");
    CHECK(read->source == nickname_source::imported);
}
