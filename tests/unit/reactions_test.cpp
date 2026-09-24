// How emojis are keyed and shown for the reaction statistics (plan v4 §9.6).

#include "core/events/reactions.hpp"

#include <catch2/catch_test_macros.hpp>

using latibot::events::display_emoji;
using latibot::events::parse_emoji;
using latibot::events::reaction_emoji;

TEST_CASE("a reaction is keyed by id when custom, by itself when Unicode", "[events]") {
    const auto custom = reaction_emoji(dpp::snowflake{123}, "skull", true);
    CHECK(custom.key == "c:123");
    CHECK(custom.name == "skull");
    CHECK(custom.animated);

    CHECK(reaction_emoji({}, "💀").key == "u:💀");
}

TEST_CASE("the colour-form selector does not make a second emoji", "[events]") {
    // "❤" and "❤️" differ only by U+FE0F, which depends on the keyboard.
    CHECK(reaction_emoji({}, "\xE2\x9D\xA4\xEF\xB8\x8F").key == reaction_emoji({}, "\xE2\x9D\xA4").key);
}

TEST_CASE("typed emojis are understood in every form a command sees", "[events]") {
    CHECK(parse_emoji("<:skull:123>")->key == "c:123");
    CHECK(parse_emoji("<:skull:123>")->name == "skull");
    CHECK(parse_emoji("<a:party:456>")->animated);
    CHECK(parse_emoji(" 💀 ")->key == "u:💀");

    // What an autocomplete choice fills in.
    CHECK(parse_emoji("c:123")->key == "c:123");
    CHECK(parse_emoji("u:💀")->key == "u:💀");

    CHECK_FALSE(parse_emoji("   ").has_value());
}

TEST_CASE("an emoji is shown the way Discord draws it", "[events]") {
    CHECK(display_emoji(reaction_emoji({}, "💀")) == "💀");
    CHECK(display_emoji(reaction_emoji(dpp::snowflake{123}, "skull")) == "<:skull:123>");
    CHECK(display_emoji(reaction_emoji(dpp::snowflake{456}, "party", true)) == "<a:party:456>");

    // A custom emoji whose name was never seen still draws from its id.
    CHECK(display_emoji({.key = "c:789", .name = "", .animated = false}) == "<:_:789>");
}
