// Conversation mode's parts that need no model: hearing its name, the
// windows, who is typing, and the check's prompt
// (src/modules/llm/docs/Language_Model.md §2.10).

#include "aliases.hpp"
#include "conversation.hpp"
#include "core/db/database.hpp"
#include "prompt.hpp"

#include "llm_module.hpp"
#include "mocks/mock_clock.hpp"
#include "mocks/mock_discord.hpp"
#include "support/schema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <string>
#include <vector>

using latibot::llm::approach;
using latibot::llm::channel_activity;
using latibot::llm::context_message;
using latibot::llm::conversation_rules;
using latibot::llm::conversation_windows;
using namespace std::chrono_literals;

namespace {

constexpr dpp::snowflake guild{1000};
constexpr dpp::snowflake channel{3000};
constexpr dpp::snowflake other_channel{3001};
constexpr dpp::snowflake bot_id{42};
constexpr dpp::snowflake alice{11};
constexpr dpp::snowflake bob{12};

auto rules_on() -> conversation_rules {
    return {.enabled = true, .quiet_after = 180s, .replies = 6, .longest = 900s, .typing_wait = 6s};
}

auto line(std::uint64_t id, dpp::snowflake author, std::string content, bool from_me = false) -> context_message {
    return {.id = dpp::snowflake{id},
            .author_id = author,
            .author_name = from_me ? "LatiBot" : "someone",
            .from_me = from_me,
            .from_bot = from_me,
            .content = std::move(content)};
}

/// The aliases, over an in-memory database.
struct cast_fixture {
    latibot::db::database db{":memory:"};
    latibot::testing::mock_discord discord;
    latibot::llm::alias_store aliases{db};

    cast_fixture() {
        latibot::testing::create_schema(db);
        latibot::db::apply_schema(db, latibot::llm::llm_schema());
    }
};

} // namespace

TEST_CASE("the bot hears its name anywhere in a message, as a whole word", "[llm][conversation]") {
    for (const char* text : {"thanks latibot", "what do you think, LatiBot?", "LATIBOT", "is latibot here"}) {
        CHECK(latibot::llm::names_bot(text, "LatiBot"));
    }
    for (const char* text : {"latibots are great", "mylatibot", "lati bot", ""}) {
        CHECK_FALSE(latibot::llm::names_bot(text, "LatiBot"));
    }
    CHECK_FALSE(latibot::llm::names_bot("anything", ""));
}

TEST_CASE("a conversation opens when the bot answers, and closes after a quiet spell", "[llm][conversation]") {
    latibot::testing::mock_clock clock;
    conversation_windows windows(clock);
    CHECK_FALSE(windows.open(channel, rules_on()));

    windows.opened(channel);
    CHECK(windows.open(channel, rules_on()));
    CHECK_FALSE(windows.open(other_channel, rules_on()));

    SECTION("only where the guild has conversation mode on") {
        CHECK_FALSE(windows.open(channel, conversation_rules{}));
    }
    SECTION("each reply keeps it open a while longer") {
        clock.advance(170s);
        windows.joined_in(channel);
        clock.advance(170s);
        CHECK(windows.open(channel, rules_on()));
        clock.advance(11s);
        CHECK_FALSE(windows.open(channel, rules_on()));
    }
    SECTION("closing once quiet for long enough") {
        clock.advance(181s);
        CHECK_FALSE(windows.open(channel, rules_on()));
    }
}

TEST_CASE("a conversation closes after so many unaddressed replies, or at its longest", "[llm][conversation]") {
    latibot::testing::mock_clock clock;
    conversation_windows windows(clock);
    windows.opened(channel);

    SECTION("replies") {
        for (int reply = 0; reply < 5; ++reply) {
            windows.joined_in(channel);
        }
        CHECK(windows.open(channel, rules_on()));
        windows.joined_in(channel);
        CHECK_FALSE(windows.open(channel, rules_on()));

        // Being addressed starts a fresh one.
        windows.opened(channel);
        CHECK(windows.open(channel, rules_on()));
    }
    SECTION("time") {
        // A reply a minute keeps it from going quiet, but not past its
        // longest.
        const conversation_rules lively{.enabled = true, .quiet_after = 180s, .replies = 100, .longest = 900s, .typing_wait = 6s};
        for (int minute = 0; minute < 15; ++minute) {
            clock.advance(60s);
            windows.joined_in(channel);
        }
        CHECK(windows.open(channel, lively));
        clock.advance(1s);
        CHECK_FALSE(windows.open(channel, lively));
    }
}

TEST_CASE("somebody typing counts for ten seconds, or until they send something", "[llm][conversation]") {
    latibot::testing::mock_clock clock;
    channel_activity activity(clock);
    CHECK_FALSE(activity.anyone_typing(channel));

    activity.started_typing(channel, alice);
    CHECK(activity.anyone_typing(channel));
    CHECK_FALSE(activity.anyone_typing(other_channel));

    SECTION("until they send it") {
        activity.posted(channel, alice, dpp::snowflake{5001});
        CHECK_FALSE(activity.anyone_typing(channel));
    }
    SECTION("or ten seconds pass") {
        clock.advance(10s);
        CHECK_FALSE(activity.anyone_typing(channel));
    }
    SECTION("somebody else posting does not stop them") {
        activity.posted(channel, bob, dpp::snowflake{5001});
        CHECK(activity.anyone_typing(channel));
    }
}

TEST_CASE("only a person's latest message is worth answering", "[llm][conversation]") {
    latibot::testing::mock_clock clock;
    channel_activity activity(clock);
    activity.posted(channel, alice, dpp::snowflake{5000});
    CHECK(activity.is_latest(channel, alice, dpp::snowflake{5000}));

    activity.posted(channel, alice, dpp::snowflake{5002});
    CHECK_FALSE(activity.is_latest(channel, alice, dpp::snowflake{5000}));
    CHECK(activity.is_latest(channel, alice, dpp::snowflake{5002}));

    // Somebody else's message, or another channel's, does not stand in.
    activity.posted(channel, bob, dpp::snowflake{5003});
    activity.posted(other_channel, alice, dpp::snowflake{5004});
    CHECK(activity.is_latest(channel, alice, dpp::snowflake{5002}));
}

TEST_CASE("the check is asked yes or no about the latest message, with aliases only", "[llm][conversation]") {
    cast_fixture test;
    latibot::llm::people cast(test.aliases, test.discord, guild, bot_id, "LatiBot");
    const std::string alice_alias = cast.meet(alice, "Alice");
    const std::string bob_alias = cast.meet(bob, "Bob");

    const std::vector<context_message> history{line(1, bob, "anyone seen the game"), line(2, bot_id, "i did!", true)};
    const context_message latest = line(3, alice, "what did you think of it");
    const context_message replied = line(2, bot_id, "i did!", true);

    CHECK(latibot::llm::check_question(history, latest, nullptr, 1500, cast) ==
          std::format("Recent messages in the channel, oldest first:\n"
                      "{}: anyone seen the game\n"
                      "LatiBot (you): i did!\n\n"
                      "The latest message:\n{}: what did you think of it\n\n"
                      "Should LatiBot reply to it? Answer yes or no.",
                      bob_alias, alice_alias));
    CHECK(latibot::llm::check_question(history, latest, &replied, 1500, cast)
              .contains("The latest message replies to this one:\nLatiBot (you): i did!\n\n"));
    CHECK(latibot::llm::check_instructions().contains("Answer with one word: yes or no."));
}

TEST_CASE("only a yes from the check is a yes", "[llm][conversation]") {
    for (const char* answer : {"yes", "Yes.", "  YES", "yes, it's for LatiBot"}) {
        CHECK(latibot::llm::check_says_yes(answer));
    }
    for (const char* answer : {"no", "No.", "", "maybe", "I can't tell"}) {
        CHECK_FALSE(latibot::llm::check_says_yes(answer));
    }
}

TEST_CASE("joining in, the model is told it may stay silent, and how", "[llm][conversation]") {
    cast_fixture test;
    latibot::llm::people cast(test.aliases, test.discord, guild, bot_id, "LatiBot");
    const context_message latest = line(3, alice, "that's wild");

    const std::string joining = latibot::llm::question_for({}, latest, nullptr, approach::joined_in, {}, 3000, cast);
    CHECK(joining.contains("Nobody addressed you"));
    CHECK(joining.ends_with("write only [silent]"));

    const std::string named = latibot::llm::question_for({}, latest, nullptr, approach::named, {}, 3000, cast);
    CHECK(named.starts_with("The message to answer:\n"));

    CHECK(latibot::llm::is_silence("  [silent]\n"));
    CHECK_FALSE(latibot::llm::is_silence("[silent] but also this"));
}
