#include "core/commands/trigger.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

using latibot::commands::describe;
using latibot::commands::format_responses;
using latibot::commands::parse_responses;
using namespace std::chrono_literals;

TEST_CASE("responses are one per line", "[commands]") {
    const auto responses = parse_responses("nice\nvery nice\n");

    REQUIRE(responses.size() == 2);
    CHECK(responses[0].text == "nice");
    CHECK(responses[0].weight == 1);
    CHECK(responses[1].text == "very nice");
}

TEST_CASE("a leading number and bar sets the weight", "[commands]") {
    const auto responses = parse_responses("3 | common\nrare");

    REQUIRE(responses.size() == 2);
    CHECK(responses[0].text == "common");
    CHECK(responses[0].weight == 3);
    CHECK(responses[1].text == "rare");
    CHECK(responses[1].weight == 1);
}

TEST_CASE("a bar that is not a weight stays part of the response", "[commands]") {
    // People write "a | b" meaning the text, and losing half of it would be
    // worse than not supporting weights at all.
    const auto responses = parse_responses("this | that\n|leading bar");

    REQUIRE(responses.size() == 2);
    CHECK(responses[0].text == "this | that");
    CHECK(responses[0].weight == 1);
    CHECK(responses[1].text == "|leading bar");
}

TEST_CASE("blank lines are skipped", "[commands]") {
    const auto responses = parse_responses("\n\nnice\n   \n\nalso nice\n\n");

    REQUIRE(responses.size() == 2);
    CHECK(responses[0].text == "nice");
    CHECK(responses[1].text == "also nice");
}

TEST_CASE("nothing usable parses to nothing", "[commands]") {
    // The command turns this into an error rather than saving a trigger that
    // matches and then has nothing to say.
    CHECK(parse_responses("").empty());
    CHECK(parse_responses("   \n\t\n").empty());
}

TEST_CASE("responses round trip through their text form", "[commands]") {
    const std::string original = "3 | common\nrare";
    const auto responses = parse_responses(original);

    // The weight is only written back when it is not the default, so editing
    // a trigger nobody weighted does not add noise to the box.
    CHECK(format_responses(responses) == original);
    CHECK(format_responses(parse_responses("just this")) == "just this");
}

TEST_CASE("a trigger describes itself in one line", "[commands]") {
    latibot::events::trigger entry{.id = 7,
                                   .guild_id = dpp::snowflake{1},
                                   .pattern = "420",
                                   .mode = latibot::events::match_mode::whole_word,
                                   .cooldown = 30s,
                                   .enabled = true,
                                   .responses = {{.text = "nice", .weight = 1}}};

    CHECK(describe(entry) == "`7` **420** (whole word, 30s) -> 1 response");

    SECTION("a zero cooldown says so rather than showing 0s") {
        entry.cooldown = 0s;
        CHECK(describe(entry) == "`7` **420** (whole word, no cooldown) -> 1 response");
    }

    SECTION("a disabled trigger is marked") {
        entry.enabled = false;
        CHECK(describe(entry) == "`7` **420** (whole word, 30s, disabled) -> 1 response");
    }

    SECTION("several responses pluralise") {
        entry.responses.push_back({.text = "very nice", .weight = 1});
        CHECK(describe(entry) == "`7` **420** (whole word, 30s) -> 2 responses");
    }
}
