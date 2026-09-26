#include "core/events/nickname_import.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

using latibot::events::central_time_to_utc;
using latibot::events::import_report;
using latibot::events::imported_author;
using latibot::events::nickname_source;
using latibot::events::read_nicknames_json;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{142409638556467200};
constexpr dpp::snowflake member{111111111111111111};
constexpr dpp::snowflake other{222222222222222222};

/// The instant a UTC wall-clock reading names, for comparing conversions
/// against something readable.
auto utc(int year, unsigned month, unsigned day, int hour, int minute = 0, int second = 0) -> std::chrono::system_clock::time_point {
    return std::chrono::sys_days{std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day}} + std::chrono::hours{hour} +
           std::chrono::minutes{minute} + std::chrono::seconds{second};
}

} // namespace

// --------------------------------------------------------------------------
// Times
// --------------------------------------------------------------------------

TEST_CASE("a winter timestamp is read as Central Standard Time", "[events]") {
    // CST is UTC-6.
    CHECK(central_time_to_utc("2023-11-25 01:58:23") == utc(2023, 11, 25, 7, 58, 23));
}

TEST_CASE("a summer timestamp is read as Central Daylight Time", "[events]") {
    // CDT is UTC-5, and nothing in the text says which it was.
    CHECK(central_time_to_utc("2023-07-04 12:00:00") == utc(2023, 7, 4, 17, 0, 0));
}

TEST_CASE("the hour that happens twice each November takes the earlier one", "[events]") {
    // 2023-11-05 01:30 Central happened at 06:30 UTC and again at 07:30 UTC.
    // Either is at most an hour wrong and there is no way to know which was
    // meant, so the choice is made once and written down (plan v4 §8.3).
    CHECK(central_time_to_utc("2023-11-05 01:30:00") == utc(2023, 11, 5, 6, 30, 0));
}

TEST_CASE("the hour that never happens each March is shifted forward", "[events]") {
    // 2023-03-12 02:30 Central does not exist: the clocks went from 02:00 to
    // 03:00. Shifting forward keeps the minutes, where snapping to the
    // transition would flatten the whole gap onto one instant.
    CHECK(central_time_to_utc("2023-03-12 02:30:00") == utc(2023, 3, 12, 8, 30, 0));

    // Two times in the gap stay an hour apart rather than collapsing together.
    CHECK(central_time_to_utc("2023-03-12 02:00:00") != central_time_to_utc("2023-03-12 02:59:59"));
}

TEST_CASE("dates before the 2007 rule change use the rules of their own year", "[events]") {
    // Daylight saving began in April before 2007 and in March after it, so a
    // date in early March is CST in 2006 and CDT in 2023.
    CHECK(central_time_to_utc("2006-03-20 12:00:00") == utc(2006, 3, 20, 18, 0, 0));
    CHECK(central_time_to_utc("2023-03-20 12:00:00") == utc(2023, 3, 20, 17, 0, 0));
}

TEST_CASE("a timestamp that is not one is refused rather than guessed at", "[events]") {
    CHECK_FALSE(central_time_to_utc("").has_value());
    CHECK_FALSE(central_time_to_utc("yesterday").has_value());
    CHECK_FALSE(central_time_to_utc("2023-11-25").has_value());
    CHECK_FALSE(central_time_to_utc("25/11/2023 01:58:23").has_value());
}

// --------------------------------------------------------------------------
// Attribution carried over
// --------------------------------------------------------------------------

TEST_CASE("an imported author is kept only when it is not the guess", "[events]") {
    // The Java bot wrote the member's own id whenever it had no idea who did
    // it, so that value carries no information at all.
    CHECK_FALSE(imported_author(member, member).has_value());
    CHECK_FALSE(imported_author(member, dpp::snowflake{}).has_value());

    // A different id could only have come from its /nickname, which is the
    // one case it did know.
    CHECK(imported_author(member, other) == other);
}

// --------------------------------------------------------------------------
// Reading the file
// --------------------------------------------------------------------------

TEST_CASE("a member's entries are read with their guild and id", "[events]") {
    const std::string text = R"json({
        "142409638556467200": [{
            "guild": "142409638556467200",
            "member": {"id": "111111111111111111", "username": "somebody"},
            "nicknames": [
                {"datetime": "2023-11-25 01:58:23", "changedById": "222222222222222222", "nickname": "one"},
                {"datetime": "2023-11-26 01:58:23", "changedById": "111111111111111111", "nickname": "two"}
            ]
        }]
    })json";

    const import_report report = read_nicknames_json(text);

    REQUIRE(report.problems.empty());
    REQUIRE(report.entries.size() == 2);
    REQUIRE(report.members.size() == 1);
    CHECK(report.members.front().second == "somebody");

    CHECK(report.entries[0].guild_id == guild);
    CHECK(report.entries[0].user_id == member);
    CHECK(report.entries[0].nickname == "one");
    CHECK(report.entries[0].source == nickname_source::imported);
    CHECK(report.entries[0].changed_by == other);

    // The self-attribution the Java bot guessed at does not come across.
    CHECK_FALSE(report.entries[1].changed_by.has_value());
}

TEST_CASE("an imported entry keeps the text its time was read from", "[events]") {
    const std::string text = R"json({
        "142409638556467200": [{
            "member": {"id": "111111111111111111"},
            "nicknames": [{"datetime": "2023-11-25 01:58:23", "nickname": "one"}]
        }]
    })json";

    const import_report report = read_nicknames_json(text);

    REQUIRE(report.entries.size() == 1);

    // So the conversion can be redone if the timezone turns out to be wrong
    // (plan v4 §8.3).
    CHECK(report.entries.front().imported_raw == "2023-11-25 01:58:23");
}

TEST_CASE("a cleared nickname imports as nothing rather than as an empty name", "[events]") {
    const std::string text = R"json({
        "142409638556467200": [{
            "member": {"id": "111111111111111111"},
            "nicknames": [{"datetime": "2023-11-25 01:58:23", "nickname": ""}]
        }]
    })json";

    const import_report report = read_nicknames_json(text);

    REQUIRE(report.entries.size() == 1);
    CHECK_FALSE(report.entries.front().nickname.has_value());
}

TEST_CASE("one unreadable entry does not lose the rest", "[events]") {
    const std::string text = R"json({
        "142409638556467200": [{
            "member": {"id": "111111111111111111"},
            "nicknames": [
                {"datetime": "not a time", "nickname": "one"},
                {"nickname": "no time at all"},
                {"datetime": "2023-11-25 01:58:23", "nickname": "good"}
            ]
        }]
    })json";

    const import_report report = read_nicknames_json(text);

    REQUIRE(report.entries.size() == 1);
    CHECK(report.entries.front().nickname == "good");
    CHECK(report.problems.size() == 2);
}

TEST_CASE("malformed shapes are named rather than dropped quietly", "[events]") {
    SECTION("not JSON") {
        CHECK_FALSE(read_nicknames_json("{nope").problems.empty());
    }

    SECTION("a key that is not a guild id") {
        const import_report report = read_nicknames_json(R"json({"not-an-id": []})json");
        REQUIRE(report.problems.size() == 1);
        CHECK(report.problems.front().find("not-an-id") != std::string::npos);
    }

    SECTION("a guild that does not hold a list") {
        CHECK(read_nicknames_json(R"json({"142409638556467200": {}})json").problems.size() == 1);
    }

    SECTION("a record with no member") {
        CHECK(read_nicknames_json(R"json({"142409638556467200": [{"nicknames": []}]})json").problems.size() == 1);
    }

    SECTION("a member id that is not an id") {
        const std::string text = R"json({"142409638556467200": [{"member": {"id": "abc"}, "nicknames": []}]})json";
        CHECK(read_nicknames_json(text).problems.size() == 1);
    }
}

TEST_CASE("an empty file imports nothing and complains about nothing", "[events]") {
    const import_report report = read_nicknames_json("{}");

    CHECK(report.entries.empty());
    CHECK(report.problems.empty());
}
