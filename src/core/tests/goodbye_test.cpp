#include "core/events/goodbye.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using latibot::events::default_goodbye_phrase;
using latibot::events::is_goodbye;

TEST_CASE("the goodbye phrase is recognised however it is typed", "[events]") {
    CHECK(is_goodbye("say goodbye latibot", default_goodbye_phrase));
    CHECK(is_goodbye("Say Goodbye LatiBot", default_goodbye_phrase));
    CHECK(is_goodbye("  say   goodbye  latibot  ", default_goodbye_phrase));

    SECTION("punctuation makes no difference") {
        CHECK(is_goodbye("Say goodbye, LatiBot!", default_goodbye_phrase));
        CHECK(is_goodbye("say goodbye latibot.", default_goodbye_phrase));
    }
}

TEST_CASE("the phrase has to be the whole message", "[events]") {
    // It stops the bot, so quoting it in conversation must not fire it.
    CHECK_FALSE(is_goodbye("if you say goodbye latibot it shuts down", default_goodbye_phrase));
    CHECK_FALSE(is_goodbye("say goodbye latibot please", default_goodbye_phrase));
    CHECK_FALSE(is_goodbye("hey say goodbye latibot", default_goodbye_phrase));

    SECTION("even an emoji counts as more message") {
        // Near enough is not good enough for something that stops the bot.
        CHECK_FALSE(is_goodbye("say goodbye latibot 👋", default_goodbye_phrase));
    }
}

TEST_CASE("a cleared phrase turns the feature off", "[events]") {
    // Comparing against an empty phrase the wrong way round would make every
    // message a match, which would be a very bad bug in this particular
    // feature.
    CHECK_FALSE(is_goodbye("", ""));
    CHECK_FALSE(is_goodbye("anything at all", ""));
    CHECK_FALSE(is_goodbye("say goodbye latibot", "   "));
}

TEST_CASE("a custom phrase replaces the default", "[events]") {
    CHECK(is_goodbye("time for bed", "time for bed"));
    CHECK_FALSE(is_goodbye("say goodbye latibot", "time for bed"));
}

TEST_CASE("an empty message never matches a real phrase", "[events]") {
    CHECK_FALSE(is_goodbye("", default_goodbye_phrase));
    CHECK_FALSE(is_goodbye("!!!", default_goodbye_phrase));
}
