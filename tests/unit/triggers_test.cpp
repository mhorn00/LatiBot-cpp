#include "core/events/triggers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <map>
#include <string>
#include <vector>

using latibot::events::choose;
using latibot::events::match_mode;
using latibot::events::match_mode_from_string;
using latibot::events::matches;
using latibot::events::off_cooldown;
using latibot::events::weighted_response;
using namespace std::chrono_literals;

TEST_CASE("whole word matching ignores the middle of longer words", "[events]") {
    CHECK(matches("it is 420 somewhere", "420", match_mode::whole_word));
    CHECK(matches("420", "420", match_mode::whole_word));
    CHECK(matches("(420)", "420", match_mode::whole_word));

    CHECK_FALSE(matches("4200 is not it", "420", match_mode::whole_word));
    CHECK_FALSE(matches("a4201", "420", match_mode::whole_word));
    CHECK_FALSE(matches("nothing here", "420", match_mode::whole_word));
}

TEST_CASE("a later occurrence still counts as a whole word", "[events]") {
    // Checking only the first occurrence would miss this, and "4200" leading
    // is exactly the sort of message that turns up.
    CHECK(matches("4200 and also 420", "420", match_mode::whole_word));
}

TEST_CASE("substring matching does not care about boundaries", "[events]") {
    CHECK(matches("4200 is not it", "420", match_mode::substring));
    CHECK(matches("catastrophe", "cat", match_mode::substring));
}

TEST_CASE("matching ignores case on both sides", "[events]") {
    CHECK(matches("NICE Try", "nice", match_mode::whole_word));
    CHECK(matches("nice try", "NICE", match_mode::whole_word));
}

TEST_CASE("a pattern with punctuation matches as a word", "[events]") {
    // "4:20" was one of the Java bot's patterns, and the colon is not a word
    // character, so the boundaries are the digits at either end.
    CHECK(matches("it's 4:20", "4:20", match_mode::whole_word));
    CHECK_FALSE(matches("14:205", "4:20", match_mode::whole_word));
}

TEST_CASE("an empty pattern never matches", "[events]") {
    // Otherwise a trigger saved with a blank pattern answers every message.
    CHECK_FALSE(matches("anything", "", match_mode::whole_word));
    CHECK_FALSE(matches("anything", "", match_mode::substring));
}

TEST_CASE("match modes parse from their stored and spoken names", "[events]") {
    CHECK(match_mode_from_string("whole_word") == match_mode::whole_word);
    CHECK(match_mode_from_string("WORD") == match_mode::whole_word);
    CHECK(match_mode_from_string("substring") == match_mode::substring);
    CHECK(match_mode_from_string("anywhere") == match_mode::substring);
    CHECK_FALSE(match_mode_from_string("regex").has_value());
    CHECK_FALSE(match_mode_from_string("").has_value());
}

TEST_CASE("weighted responses are picked in proportion", "[events]") {
    const std::array<weighted_response, 3> responses{{
        {.text = "a", .weight = 1},
        {.text = "b", .weight = 3},
        {.text = "c", .weight = 1},
    }};

    // Total weight 5: rolls 0 pick a, 1-3 pick b, 4 picks c.
    CHECK(choose(responses, 0)->text == "a");
    CHECK(choose(responses, 1)->text == "b");
    CHECK(choose(responses, 3)->text == "b");
    CHECK(choose(responses, 4)->text == "c");

    SECTION("rolls beyond the total wrap round") {
        CHECK(choose(responses, 5)->text == "a");
        CHECK(choose(responses, 9)->text == "c");
    }

    SECTION("every response is reachable") {
        std::map<std::string, int> seen;
        for (std::uint64_t roll = 0; roll < 5; ++roll) {
            ++seen[choose(responses, roll)->text];
        }
        CHECK(seen["a"] == 1);
        CHECK(seen["b"] == 3);
        CHECK(seen["c"] == 1);
    }
}

TEST_CASE("a trigger with nothing to say picks nothing", "[events]") {
    CHECK(choose({}, 0) == nullptr);

    SECTION("weights of zero are not a division by zero") {
        const std::array<weighted_response, 2> useless{{
            {.text = "a", .weight = 0},
            {.text = "b", .weight = -1},
        }};
        CHECK(choose(useless, 7) == nullptr);
    }
}

TEST_CASE("a zero-weight response is skipped but its neighbours still work", "[events]") {
    const std::array<weighted_response, 3> responses{{
        {.text = "never", .weight = 0},
        {.text = "always", .weight = 2},
        {.text = "sometimes", .weight = 1},
    }};

    CHECK(choose(responses, 0)->text == "always");
    CHECK(choose(responses, 1)->text == "always");
    CHECK(choose(responses, 2)->text == "sometimes");
}

TEST_CASE("cooldowns are measured from the last reply", "[events]") {
    const auto start = std::chrono::steady_clock::time_point{};

    CHECK(off_cooldown(std::nullopt, start, 30s));
    CHECK_FALSE(off_cooldown(start, start + 29s, 30s));
    CHECK(off_cooldown(start, start + 30s, 30s));
    CHECK(off_cooldown(start, start + 31s, 30s));
}

TEST_CASE("a zero cooldown means no cooldown", "[events]") {
    // Plan v4 §11 allows 0 explicitly, and it must not mean "never again".
    const auto start = std::chrono::steady_clock::time_point{};
    CHECK(off_cooldown(start, start, 0s));
    CHECK(off_cooldown(start, start, -5s));
}
