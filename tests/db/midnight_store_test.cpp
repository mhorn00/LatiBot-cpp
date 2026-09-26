#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/midnight.hpp"
#include "core/ports/clock.hpp"

#include "mocks/mock_clock.hpp"
#include "support/capture_log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <string>
#include <variant>

using latibot::events::midnight_entry;
using latibot::events::midnight_scheduler;
using latibot::events::midnight_store;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake other_guild{2000};
constexpr dpp::snowflake channel{3000};

struct store_fixture {
    latibot::db::database db{":memory:"};
    midnight_store store{db};

    store_fixture() { latibot::db::migrate(db); }
};

auto utc(int year, unsigned month, unsigned day, int hour, int minute = 0, int second = 0) -> std::chrono::system_clock::time_point {
    return std::chrono::sys_days{std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day}} + std::chrono::hours{hour} +
           std::chrono::minutes{minute} + std::chrono::seconds{second};
}

auto entry_in(std::string timezone, dpp::snowflake in = guild, std::string last_fired = {}) -> midnight_entry {
    return {.id = 0,
            .guild_id = in,
            .channel_id = channel,
            .timezone = std::move(timezone),
            .message = "it is a new day",
            .enabled = true,
            .last_fired_date = std::move(last_fired)};
}

} // namespace

TEST_CASE("an added entry comes back as it went in", "[db]") {
    store_fixture fixture;

    const std::int64_t id = fixture.store.add(entry_in("America/Chicago"));
    REQUIRE(id != 0);

    const auto read = fixture.store.find(id, guild);
    REQUIRE(read.has_value());
    CHECK(read->channel_id == channel);
    CHECK(read->timezone == "America/Chicago");
    CHECK(read->message == "it is a new day");
    CHECK(read->enabled);
    CHECK(read->last_fired_date.empty());
}

TEST_CASE("entries belong to one guild", "[db]") {
    store_fixture fixture;

    const std::int64_t mine = fixture.store.add(entry_in("UTC"));
    fixture.store.add(entry_in("UTC", other_guild));

    CHECK(fixture.store.for_guild(guild).size() == 1);
    CHECK_FALSE(fixture.store.find(mine, other_guild).has_value());
    CHECK_FALSE(fixture.store.remove(mine, other_guild));
    CHECK(fixture.store.remove(mine, guild));
}

TEST_CASE("only enabled entries are looked at on a tick", "[db]") {
    store_fixture fixture;

    fixture.store.add(entry_in("UTC"));

    midnight_entry off = entry_in("UTC");
    off.enabled = false;
    fixture.store.add(off);

    CHECK(fixture.store.for_guild(guild).size() == 2);
    CHECK(fixture.store.enabled().size() == 1);
}

TEST_CASE("a day can only be claimed once", "[db]") {
    store_fixture fixture;

    const std::int64_t id = fixture.store.add(entry_in("UTC"));

    CHECK(fixture.store.mark_fired(id, "2026-09-23"));
    CHECK_FALSE(fixture.store.mark_fired(id, "2026-09-23"));
    CHECK(fixture.store.mark_fired(id, "2026-09-24"));
}

TEST_CASE("editing an entry leaves the day it last posted alone", "[db]") {
    store_fixture fixture;

    const std::int64_t id = fixture.store.add(entry_in("UTC"));
    REQUIRE(fixture.store.mark_fired(id, "2026-09-23"));

    auto entry = fixture.store.find(id, guild);
    REQUIRE(entry.has_value());
    entry->message = "something else";
    REQUIRE(fixture.store.update(*entry));

    // Otherwise changing the wording would post it again the same day.
    CHECK(fixture.store.find(id, guild)->last_fired_date == "2026-09-23");
}

TEST_CASE("a tick posts an entry once and then leaves it alone", "[db]") {
    store_fixture fixture;
    latibot::testing::mock_clock clock;
    midnight_scheduler scheduler(fixture.store, clock);

    fixture.store.add(entry_in("UTC"));

    clock.set(utc(2026, 9, 23, 0, 0, 30));

    const auto posts = scheduler.tick();
    REQUIRE(posts.size() == 1);

    const auto* post = std::get_if<latibot::events::send_message>(&posts.front());
    REQUIRE(post != nullptr);
    CHECK(post->channel_id == channel);
    CHECK(post->content == "it is a new day");

    // Every thirty seconds for the rest of the day, and nothing happens.
    CHECK(scheduler.tick().empty());
    clock.set(utc(2026, 9, 23, 12, 0, 0));
    CHECK(scheduler.tick().empty());

    clock.set(utc(2026, 9, 24, 0, 0, 10));
    CHECK(scheduler.tick().size() == 1);
}

TEST_CASE("an entry that cannot be claimed does not cost the others their post", "[db]") {
    // The posts a tick has claimed are only sent if it returns, so an error on
    // one entry that escaped would lose every other entry's message that day.
    store_fixture fixture;
    latibot::testing::mock_clock clock;
    midnight_scheduler scheduler(fixture.store, clock);

    fixture.store.add(entry_in("UTC"));
    const std::int64_t broken = fixture.store.add(entry_in("UTC"));
    fixture.store.add(entry_in("UTC"));

    // Stands in for a disk error on that one row.
    fixture.db.execute(
        std::format("CREATE TRIGGER fail_claim BEFORE UPDATE ON midnight_messages WHEN NEW.id = {} "
                    "BEGIN SELECT RAISE(ABORT, 'disk error'); END",
                    broken));

    clock.set(utc(2026, 9, 23, 0, 0, 30));
    {
        const latibot::testing::capture_log log;
        CHECK(scheduler.tick().size() == 2);
        CHECK(log.contains(latibot::util::log_level::error, std::format("midnight message {}", broken)));
    }

    // Once the error clears, the next tick posts the one it held up.
    fixture.db.execute("DROP TRIGGER fail_claim");
    CHECK(scheduler.tick().size() == 1);
    CHECK(scheduler.tick().empty());
}

TEST_CASE("a restart moments after posting does not post again", "[db]") {
    store_fixture fixture;
    latibot::testing::mock_clock clock;

    fixture.store.add(entry_in("UTC"));
    clock.set(utc(2026, 9, 23, 0, 0, 10));

    {
        midnight_scheduler before(fixture.store, clock);
        REQUIRE(before.tick().size() == 1);
    }

    // The date was saved with the post, not counted from it, so a scheduler
    // that has never run still knows the day is spoken for (plan v4 §10).
    clock.set(utc(2026, 9, 23, 0, 0, 40));
    midnight_scheduler after(fixture.store, clock);
    CHECK(after.tick().empty());
}

TEST_CASE("a night the bot slept through is given up on, not posted at breakfast", "[db]") {
    store_fixture fixture;
    latibot::testing::mock_clock clock;
    midnight_scheduler scheduler(fixture.store, clock);

    const std::int64_t id = fixture.store.add(entry_in("UTC", guild, "2026-09-22"));

    // The bot comes back mid-morning, hours after the midnight it was meant
    // to post at.
    clock.set(utc(2026, 9, 23, 9, 0, 0));
    CHECK(scheduler.tick().empty());

    // Nothing is written down for a day that was missed: `last_fired_date`
    // means "posted", and it would be a lie there.
    CHECK(fixture.store.find(id, guild)->last_fired_date == "2026-09-22");

    // It stays quiet for the rest of that day...
    clock.set(utc(2026, 9, 23, 18, 0, 0));
    CHECK(scheduler.tick().empty());

    // ...and posts normally at the next midnight.
    clock.set(utc(2026, 9, 24, 0, 0, 10));
    CHECK(scheduler.tick().size() == 1);
}

TEST_CASE("a restart a minute after midnight still posts", "[db]") {
    store_fixture fixture;
    latibot::testing::mock_clock clock;

    fixture.store.add(entry_in("UTC", guild, "2026-09-22"));

    // A scheduler that has only just started, a minute into the new day: the
    // window is wider than the tick precisely so this counts as being there.
    clock.set(utc(2026, 9, 23, 0, 1, 0));
    midnight_scheduler scheduler(fixture.store, clock);

    CHECK(scheduler.tick().size() == 1);
}

TEST_CASE("each timezone posts at its own midnight", "[db]") {
    store_fixture fixture;
    latibot::testing::mock_clock clock;
    midnight_scheduler scheduler(fixture.store, clock);

    // Both added on the 22nd, where they live, so neither is owed that day.
    fixture.store.add(entry_in("Asia/Tokyo", guild, "2026-09-22"));
    fixture.store.add(entry_in("America/Chicago", guild, "2026-09-22"));

    // Midnight in Tokyo, nine hours ahead. It is still mid-morning of the
    // same day in Chicago, so only one of them posts.
    clock.set(utc(2026, 9, 22, 15, 0, 10));
    CHECK(scheduler.tick().size() == 1);

    // Midnight in Chicago, fourteen hours later.
    clock.set(utc(2026, 9, 23, 5, 0, 10));
    CHECK(scheduler.tick().size() == 1);
}

TEST_CASE("a midnight message's flags survive a round trip and default to silent", "[db]") {
    store_fixture fixture;

    const std::int64_t quiet = fixture.store.add(entry_in("UTC"));
    CHECK(fixture.store.find(quiet, guild)->message_flags == dpp::m_suppress_notifications);

    midnight_entry loud = entry_in("UTC");
    loud.message_flags = dpp::m_suppress_embeds;
    const std::int64_t id = fixture.store.add(loud);
    CHECK(fixture.store.find(id, guild)->message_flags == dpp::m_suppress_embeds);

    auto found = fixture.store.find(id, guild);
    found->message_flags = 0;
    REQUIRE(fixture.store.update(*found));
    CHECK(fixture.store.find(id, guild)->message_flags == 0);
}

TEST_CASE("a midnight post carries its entry's flags", "[db]") {
    store_fixture fixture;
    latibot::testing::mock_clock clock;
    midnight_scheduler scheduler(fixture.store, clock);

    midnight_entry loud = entry_in("UTC");
    loud.message_flags = 0;
    const std::int64_t id = fixture.store.add(loud);

    clock.set(utc(2026, 9, 23, 0, 0, 30));
    const auto posts = scheduler.tick();
    REQUIRE(posts.size() == 1);
    CHECK(std::get<latibot::events::send_message>(posts.front()).flags == 0);

    // And says which entry it is, for the line saying whether it was posted.
    CHECK(std::get<latibot::events::send_message>(posts.front()).what == std::format("midnight message {}", id));
}
