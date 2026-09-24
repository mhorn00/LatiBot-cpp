#include "core/events/midnight.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>

using latibot::events::due;
using latibot::events::is_known_timezone;
using latibot::events::matching_timezones;
using latibot::events::midnight_entry;
using latibot::events::read_local;
using namespace std::chrono_literals;

namespace {

/// The instant a UTC wall-clock reading names.
std::chrono::system_clock::time_point utc(int year, unsigned month, unsigned day, int hour, int minute = 0, int second = 0) {
    return std::chrono::sys_days{std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day}} + std::chrono::hours{hour} +
           std::chrono::minutes{minute} + std::chrono::seconds{second};
}

midnight_entry entry_in(std::string timezone, std::string last_fired = {}) {
    return {.id = 1,
            .guild_id = dpp::snowflake{1000},
            .channel_id = dpp::snowflake{2000},
            .timezone = std::move(timezone),
            .message = "it is a new day",
            .enabled = true,
            .last_fired_date = std::move(last_fired)};
}

} // namespace

// --------------------------------------------------------------------------
// Reading the clock somewhere else
// --------------------------------------------------------------------------

TEST_CASE("the local date is the one where the entry lives, not where the bot runs", "[events]") {
    // 06:30 UTC on the 23rd is still the 23rd in Chicago (01:30) and already
    // the 23rd in Tokyo (15:30); an hour earlier it is still the 22nd in
    // Chicago. The date is the whole decision, so it is worth pinning.
    const auto reading = read_local("America/Chicago", utc(2026, 9, 23, 4, 30));
    REQUIRE(reading.has_value());
    CHECK(reading->date == "2026-09-22");
    CHECK(reading->since_midnight == 23h + 30min);
}

TEST_CASE("a zone this machine does not know is refused rather than guessed at", "[events]") {
    CHECK_FALSE(read_local("Mars/Olympus_Mons", utc(2026, 9, 23, 12, 0)).has_value());
    CHECK_FALSE(is_known_timezone("Mars/Olympus_Mons"));
    CHECK(is_known_timezone("America/Chicago"));
    CHECK(is_known_timezone("UTC"));
}

// --------------------------------------------------------------------------
// When it fires
// --------------------------------------------------------------------------

TEST_CASE("an entry fires just after local midnight", "[events]") {
    const midnight_entry entry = entry_in("UTC");

    // A few seconds of slack, so a tick landing a moment early does not post
    // for yesterday.
    CHECK_FALSE(due(entry, utc(2026, 9, 23, 0, 0, 1)));
    CHECK(due(entry, utc(2026, 9, 23, 0, 0, 5)));
    CHECK(due(entry, utc(2026, 9, 23, 0, 0, 30)));
}

TEST_CASE("an entry that has posted today does not post again", "[events]") {
    const midnight_entry entry = entry_in("UTC", "2026-09-23");

    // Which is what makes a restart at 00:00:30 safe (plan v4 §10).
    CHECK_FALSE(due(entry, utc(2026, 9, 23, 0, 0, 30)));
    CHECK_FALSE(due(entry, utc(2026, 9, 23, 23, 59, 0)));

    // Tomorrow is a different day.
    CHECK(due(entry, utc(2026, 9, 24, 0, 0, 10)));
}

TEST_CASE("an entry off is an entry that does not post", "[events]") {
    midnight_entry entry = entry_in("UTC");
    entry.enabled = false;

    CHECK_FALSE(due(entry, utc(2026, 9, 23, 0, 0, 30)));
}

TEST_CASE("two entries in different timezones fire at different times", "[events]") {
    const midnight_entry tokyo = entry_in("Asia/Tokyo", "2026-09-22");
    const midnight_entry chicago = entry_in("America/Chicago", "2026-09-22");

    // Midnight in Tokyo on the 23rd is 15:00 UTC on the 22nd; midnight in
    // Chicago is 05:00 UTC on the 23rd. At the first of those it is still
    // mid-morning in Chicago, and the day there is already spoken for.
    CHECK(due(tokyo, utc(2026, 9, 22, 15, 0, 10)));
    CHECK_FALSE(due(chicago, utc(2026, 9, 22, 15, 0, 10)));

    CHECK(due(chicago, utc(2026, 9, 23, 5, 0, 10)));
}

TEST_CASE("an entry added this afternoon waits for the next midnight", "[events]") {
    // The rule that fires an entry only asks whether today's date differs
    // from the last one posted for, so a brand new entry would otherwise post
    // within thirty seconds of being added.
    const auto afternoon = utc(2026, 9, 23, 20, 0, 0);
    const midnight_entry added_today = entry_in("America/Chicago", latibot::events::already_posted_today("America/Chicago", afternoon));

    CHECK(added_today.last_fired_date == "2026-09-23");
    CHECK_FALSE(due(added_today, afternoon));

    // Midnight in Chicago that night is 05:00 UTC on the 24th.
    CHECK(due(added_today, utc(2026, 9, 24, 5, 0, 10)));
}

TEST_CASE("a spring-forward night still has a midnight to fire at", "[events]") {
    // The clocks jump from 02:00 to 03:00 in Chicago on 2026-03-08, which is
    // nowhere near midnight but is the day most likely to be got wrong.
    const midnight_entry entry = entry_in("America/Chicago");

    CHECK(due(entry, utc(2026, 3, 8, 6, 0, 10)));
}

TEST_CASE("a fall-back night does not post twice", "[events]") {
    // 01:00-01:59 happens twice in Chicago on 2026-11-01, which is after
    // midnight has already been claimed for the day.
    const midnight_entry fired = entry_in("America/Chicago", "2026-11-01");

    CHECK_FALSE(due(fired, utc(2026, 11, 1, 6, 30, 0)));
    CHECK_FALSE(due(fired, utc(2026, 11, 1, 7, 30, 0)));
}

TEST_CASE("a bad timezone in the database keeps quiet rather than posting wrongly", "[events]") {
    CHECK_FALSE(due(entry_in("Nowhere/Nothing"), utc(2026, 9, 23, 0, 0, 30)));
}

// --------------------------------------------------------------------------
// Picking a timezone
// --------------------------------------------------------------------------

TEST_CASE("timezone completion matches anywhere in the name", "[events]") {
    const auto found = matching_timezones("chicago", 25);

    REQUIRE_FALSE(found.empty());
    CHECK(std::ranges::find(found, "America/Chicago") != found.end());
}

TEST_CASE("timezone completion never offers more than it is asked for", "[events]") {
    // Discord rejects more than twenty-five choices outright.
    CHECK(matching_timezones("", 25).size() == 25);
    CHECK(matching_timezones("america", 3).size() == 3);
    CHECK(matching_timezones("", 0).empty());
}

TEST_CASE("timezone completion finds nothing for nonsense", "[events]") {
    CHECK(matching_timezones("zzzzzz", 25).empty());
}
