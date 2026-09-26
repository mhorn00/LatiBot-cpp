// Recognising the bot's old replacements and finding whose link each one was
// (plan v4 §9.7).

#include "core/events/legacy_replacements.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

using latibot::events::attribute;
using latibot::events::classify;
using latibot::events::history_message;
using latibot::events::legacy_format;
using latibot::events::legacy_match;
using latibot::events::mirror_map;

namespace {

constexpr dpp::snowflake bot{42};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};
constexpr dpp::snowflake other_bot{43};

// A function rather than a constant: building the map allocates, and a static
// that throws while the test binary is starting cannot be caught.
auto mirrors() -> const mirror_map& {
    static const mirror_map made{{"fxtwitter.com", "x.com"}, {"vxtwitter.com", "x.com"}, {"tfxktok.com", "tiktok.com"}};
    return made;
}

auto from_bot(std::string content, dpp::snowflake id = dpp::snowflake{900}) -> history_message {
    return {.id = id,
            .author_id = bot,
            .author_is_bot = true,
            .webhook_id = {},
            .is_system = false,
            .replied_to = {},
            .content = std::move(content),
            .reactions = {}};
}

auto from_person(dpp::snowflake who, std::string content, dpp::snowflake id) -> history_message {
    return {.id = id,
            .author_id = who,
            .author_is_bot = false,
            .webhook_id = {},
            .is_system = false,
            .replied_to = {},
            .content = std::move(content),
            .reactions = {}};
}

auto recognised(const history_message& message) -> legacy_match {
    const legacy_match match = classify(message, bot, mirrors());
    REQUIRE(match.what == legacy_match::kind::recognised);
    return match;
}

} // namespace

// --------------------------------------------------------------------------
// The six formats
// --------------------------------------------------------------------------

TEST_CASE("format 1: a copy of the original, sent as a reply", "[events]") {
    auto message = from_bot("lol look https://fxtwitter.com/a/status/1");
    message.replied_to = dpp::snowflake{800};
    CHECK(recognised(message).format == legacy_format::reply_copy);
}

TEST_CASE("format 2: webhook mode is counted and skipped", "[events]") {
    history_message message =
        from_person(alice, "lol look <https://x.com/a/status/1> [.](https://fxtwitter.com/a/status/1)", dpp::snowflake{900});
    message.author_is_bot = true;
    message.webhook_id = dpp::snowflake{77};

    const auto match = classify(message, bot, mirrors());
    CHECK(match.what == legacy_match::kind::webhook);
    CHECK(match.format == legacy_format::webhook);
}

TEST_CASE("format 3: a copy of the original as a plain message", "[events]") {
    CHECK(recognised(from_bot("lol look https://fxtwitter.com/a/status/1 isn't it good")).format == legacy_format::plain_copy);
}

TEST_CASE("format 4: a dot linking to the mirror", "[events]") {
    CHECK(recognised(from_bot("[.](https://fxtwitter.com/a/status/1)")).format == legacy_format::dot);
}

TEST_CASE("format 5: the link emoji and a dot", "[events]") {
    CHECK(recognised(from_bot("🔗[.](https://fxtwitter.com/a/status/1)")).format == legacy_format::link_dot);
    CHECK(recognised(from_bot(":link: [.](https://fxtwitter.com/a/status/1)")).format == legacy_format::link_dot);
}

TEST_CASE("format 6: the link emoji and an underscore, which is still the format", "[events]") {
    CHECK(recognised(from_bot("🔗[_](https://fxtwitter.com/a/status/1)")).format == legacy_format::link_underscore);
    // The Java bot put the spoiler bars inside the emoji; this one outside.
    CHECK(recognised(from_bot("🔗||[_](https://fxtwitter.com/a/status/1)||")).format == legacy_format::link_underscore);
    CHECK(recognised(from_bot("🔗 ||[_](https://fxtwitter.com/a/status/1)||\n🔗 [_](https://tfxktok.com/@a/video/2)")).format ==
          legacy_format::link_underscore);
}

TEST_CASE("the mirror links are collected whatever the format", "[events]") {
    const auto match = recognised(from_bot("🔗 [_](https://fxtwitter.com/a/status/1)\n🔗 [_](https://tfxktok.com/@a/video/2)"));
    REQUIRE(match.mirror_urls.size() == 2);
    CHECK(match.mirror_urls[1] == "https://tfxktok.com/@a/video/2");
}

// --------------------------------------------------------------------------
// What is not a replacement, and what is not understood
// --------------------------------------------------------------------------

TEST_CASE("only the bot's messages with a known mirror count", "[events]") {
    CHECK(classify(from_bot("no links"), bot, mirrors()).what == legacy_match::kind::not_ours);
    CHECK(classify(from_bot("https://example.com/a"), bot, mirrors()).what == legacy_match::kind::not_ours);
    CHECK(classify(from_person(alice, "https://fxtwitter.com/a/status/1", dpp::snowflake{1}), bot, mirrors()).what ==
          legacy_match::kind::not_ours);

    auto command_reply = from_bot("🔗 [_](https://fxtwitter.com/a/status/1)");
    command_reply.is_system = true;
    CHECK(classify(command_reply, bot, mirrors()).what == legacy_match::kind::not_ours);
}

TEST_CASE("a shape nobody wrote down is reported, not guessed at", "[events]") {
    // An underscore without the link emoji never existed.
    CHECK(classify(from_bot("[_](https://fxtwitter.com/a/status/1)"), bot, mirrors()).what == legacy_match::kind::unrecognised);
    // Masked links with words around them.
    CHECK(classify(from_bot("here you go [.](https://fxtwitter.com/a/status/1)"), bot, mirrors()).what == legacy_match::kind::unrecognised);
    // A label that is neither.
    CHECK(classify(from_bot("🔗 [tweet](https://fxtwitter.com/a/status/1)"), bot, mirrors()).what == legacy_match::kind::unrecognised);
    // One link masked and one bare.
    CHECK(classify(from_bot("[.](https://fxtwitter.com/a/status/1) https://vxtwitter.com/b/status/2"), bot, mirrors()).what ==
          legacy_match::kind::unrecognised);
}

// --------------------------------------------------------------------------
// Whose link it was
// --------------------------------------------------------------------------

TEST_CASE("a reply names its original", "[events]") {
    auto ours = from_bot("look https://fxtwitter.com/a/status/1");
    ours.replied_to = dpp::snowflake{800};
    const auto match = recognised(ours);

    SECTION("found in the history at hand") {
        const std::vector<history_message> older{from_person(bob, "chat", dpp::snowflake{850}),
                                                 from_person(alice, "look https://x.com/a/status/1", dpp::snowflake{800})};
        const auto found = attribute(ours, match, older, bot);
        CHECK(found.author_id == alice);
        CHECK(found.original_message_id == dpp::snowflake{800});
        CHECK_FALSE(found.needs_fetch);
    }

    SECTION("further back than the history at hand") {
        const auto found = attribute(ours, match, {}, bot);
        CHECK(found.needs_fetch);
        CHECK(found.original_message_id == dpp::snowflake{800});
        CHECK_FALSE(found.author_id.has_value());
    }
}

TEST_CASE("the original is the nearest earlier link, past any chat", "[events]") {
    const auto ours = from_bot("🔗 [_](https://fxtwitter.com/a/status/1)");
    const std::vector<history_message> older{
        from_person(bob, "lmao", dpp::snowflake{890}),
        from_bot("🔗 [_](https://fxtwitter.com/z/status/9)", dpp::snowflake{880}),
        from_person(alice, "https://x.com/a/status/1?s=20", dpp::snowflake{870}),
    };

    const auto found = attribute(ours, recognised(ours), older, bot);
    CHECK(found.author_id == alice);
    CHECK(found.original_message_id == dpp::snowflake{870});
    CHECK_FALSE(found.skipped_a_link);
}

TEST_CASE("a nearer link that is not ours does not take the credit", "[events]") {
    // Bob posted his link before the bot answered Alice's.
    const auto ours = from_bot("🔗 [_](https://fxtwitter.com/a/status/1)");
    const std::vector<history_message> older{
        from_person(bob, "https://x.com/b/status/2", dpp::snowflake{890}),
        from_person(alice, "https://www.x.com/a/status/1/", dpp::snowflake{880}),
    };

    const auto found = attribute(ours, recognised(ours), older, bot);
    CHECK(found.author_id == alice);
    CHECK(found.skipped_a_link);
}

TEST_CASE("with no matching link the replacement stays unattributed", "[events]") {
    const auto ours = from_bot("🔗 [_](https://fxtwitter.com/a/status/1)");

    SECTION("no earlier link at all") {
        const auto found = attribute(ours, recognised(ours), {}, bot);
        CHECK_FALSE(found.author_id.has_value());
        // Nothing to report: there was nothing it could have been.
        CHECK_FALSE(found.mismatched);
    }

    SECTION("only other links, which is reported rather than accepted") {
        const std::vector<history_message> older{from_person(bob, "https://x.com/b/status/2", dpp::snowflake{890})};
        const auto found = attribute(ours, recognised(ours), older, bot);
        CHECK_FALSE(found.author_id.has_value());
        CHECK(found.mismatched);
    }

    SECTION("the match is too far back to be the one answered") {
        std::vector<history_message> older;
        older.reserve(latibot::events::attribution_candidates + 1);
        for (std::uint64_t index = 0; index < latibot::events::attribution_candidates; ++index) {
            older.push_back(from_person(bob, "https://x.com/b/status/" + std::to_string(index), dpp::snowflake{800 - index}));
        }
        older.push_back(from_person(alice, "https://x.com/a/status/1", dpp::snowflake{700}));
        CHECK_FALSE(attribute(ours, recognised(ours), older, bot).author_id.has_value());
    }
}

TEST_CASE("other bots' links are never the original", "[events]") {
    const auto ours = from_bot("🔗 [_](https://fxtwitter.com/a/status/1)");
    history_message other = from_person(other_bot, "https://x.com/a/status/1", dpp::snowflake{890});
    other.author_is_bot = true;

    const std::vector<history_message> older{other};
    CHECK_FALSE(attribute(ours, recognised(ours), older, bot).author_id.has_value());
}

TEST_CASE("a front-page link proves nothing about which message was answered", "[events]") {
    const auto ours = from_bot("🔗 [_](https://fxtwitter.com/)");
    const std::vector<history_message> older{from_person(alice, "https://x.com", dpp::snowflake{890})};
    CHECK_FALSE(attribute(ours, recognised(ours), older, bot).author_id.has_value());
}

// --------------------------------------------------------------------------
// Times from ids
// --------------------------------------------------------------------------

TEST_CASE("a message's time comes from its id", "[events]") {
    const std::chrono::sys_seconds when{std::chrono::sys_days{std::chrono::year{2022} / 6 / 1}};
    const dpp::snowflake first = latibot::events::first_id_at(when);

    CHECK(latibot::events::created_at(first) == when);
    CHECK(latibot::events::created_at(dpp::snowflake(static_cast<std::uint64_t>(first) - 1)) < when);
    CHECK(latibot::events::first_id_at(std::chrono::sys_seconds{}).empty());
}
