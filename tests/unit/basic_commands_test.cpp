#include "core/commands/basic.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/presence.h>
#include <dpp/snowflake.h>

#include <string>

using latibot::commands::join_action;
using latibot::commands::make_activity;
using latibot::commands::parse_activity_type;
using latibot::commands::plan_join;
using latibot::commands::plan_say;
using latibot::commands::say_action;

namespace {

constexpr dpp::snowflake nowhere{};
constexpr dpp::snowflake general{111};
constexpr dpp::snowflake music{222};

} // namespace

TEST_CASE("joining follows the target and moves only when it has to", "[commands]") {
    SECTION("nobody to follow") {
        const auto decision = plan_join(nowhere, nowhere);
        CHECK(decision.action == join_action::target_not_in_voice);
        CHECK(decision.channel_id.empty());
    }

    SECTION("the target is in voice and the bot is not connected") {
        const auto decision = plan_join(general, nowhere);
        CHECK(decision.action == join_action::connect);
        CHECK(decision.channel_id == general);
    }

    SECTION("already in the right channel") {
        const auto decision = plan_join(general, general);
        CHECK(decision.action == join_action::already_there);
    }

    SECTION("connected somewhere else in the same guild") {
        const auto decision = plan_join(general, music);
        CHECK(decision.action == join_action::move);
        CHECK(decision.channel_id == general);
    }
}

TEST_CASE("a status is kept for the next start, and none is kept until one is set", "[commands]") {
    latibot::db::database db{":memory:"};
    latibot::db::migrate(db);
    latibot::config::guild_settings settings(db);

    CHECK_FALSE(latibot::commands::load_status(settings).has_value());

    latibot::commands::save_status(settings, {.text = "the logs", .type = "watching"});
    const auto kept = latibot::commands::load_status(settings);
    REQUIRE(kept.has_value());
    CHECK(kept->text == "the logs");
    CHECK(kept->type == "watching");

    // It belongs to the bot, not to any server.
    CHECK_FALSE(settings.find(dpp::snowflake{1000}, "status_text").has_value());

    const dpp::presence restored = latibot::commands::presence_for(*kept);
    REQUIRE(restored.activities.size() == 1);
    CHECK(restored.activities[0].type == dpp::at_watching);
    CHECK(restored.activities[0].name == "the logs");
}

TEST_CASE("joining says whom it followed, as the Java bot did", "[commands]") {
    using latibot::commands::describe_join;
    CHECK(describe_join(join_action::connect, dpp::snowflake{42}) == "ok joining <@42>");
    CHECK(describe_join(join_action::move, dpp::snowflake{42}) == "ok moving to <@42>");
}

TEST_CASE("a target who left voice is not followed to their old channel", "[commands]") {
    // The bot staying put matters more than the wording: the previous
    // implementation read the stale channel id and moved to an empty channel.
    const auto decision = plan_join(nowhere, music);
    CHECK(decision.action == join_action::target_not_in_voice);
    CHECK(decision.channel_id.empty());
}

TEST_CASE("say refuses a message that is only whitespace", "[commands]") {
    // Discord's own minimum length of 1 counts characters, so "   " reaches
    // us and then fails at the API with nothing useful to show the caller.
    const auto decision = plan_say("   \t ", "");
    CHECK(decision.action == say_action::blank_message);
}

TEST_CASE("say replies only when given a message id", "[commands]") {
    SECTION("no reply option") {
        const auto decision = plan_say("hello", "");
        CHECK(decision.action == say_action::send);
        CHECK(decision.reply_to.empty());
    }

    SECTION("whitespace around the id is fine") {
        const auto decision = plan_say("hello", "  1234567890123456789  ");
        CHECK(decision.action == say_action::reply);
        CHECK(decision.reply_to == dpp::snowflake{1234567890123456789ULL});
    }

    SECTION("a message link is not a message id") {
        const auto decision = plan_say("hello", "https://discord.com/channels/1/2/1234567890123456789");
        CHECK(decision.action == say_action::bad_reply_id);
    }

    SECTION("trailing rubbish is not accepted") {
        // from_chars stops at the first bad character and reports success for
        // the prefix, so the whole option has to be checked.
        const auto decision = plan_say("hello", "123abc");
        CHECK(decision.action == say_action::bad_reply_id);
    }

    SECTION("0 is nobody's message") {
        CHECK(plan_say("hello", "0").action == say_action::bad_reply_id);
    }

    SECTION("a blank message is refused before the id is looked at") {
        const auto decision = plan_say("", "not-an-id");
        CHECK(decision.action == say_action::blank_message);
    }
}

TEST_CASE("status types are matched case-insensitively and fall back to playing", "[commands]") {
    CHECK(parse_activity_type("watching") == dpp::at_watching);
    CHECK(parse_activity_type("LISTENING") == dpp::at_listening);
    CHECK(parse_activity_type(" Competing ") == dpp::at_competing);
    CHECK(parse_activity_type("custom") == dpp::at_custom);

    // The Java bot's own choice values, so an old habit still works.
    CHECK(parse_activity_type("CUSTOM_STATUS") == dpp::at_custom);
    CHECK(parse_activity_type("PLAYING") == dpp::at_game);

    SECTION("anything unrecognised plays") {
        CHECK(parse_activity_type("") == dpp::at_game);
        CHECK(parse_activity_type("sleeping") == dpp::at_game);
    }
}

TEST_CASE("a custom status carries its text in state, not name", "[commands]") {
    // Discord ignores `name` for custom statuses and shows `state` instead,
    // so setting only the name leaves the status blank.
    const dpp::activity custom = make_activity(dpp::at_custom, "up to no good");
    CHECK(custom.state == "up to no good");
    CHECK(custom.name == "Custom Status");

    const dpp::activity playing = make_activity(dpp::at_game, "with fire");
    CHECK(playing.name == "with fire");
    CHECK(playing.state.empty());
}
