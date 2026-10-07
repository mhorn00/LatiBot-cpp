#include "join_command.hpp"

#include "core/commands/registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dpp/dpp.h>

#include <memory>

using latibot::commands::join_action;
using latibot::commands::plan_join;

namespace {

constexpr dpp::snowflake nowhere{};
constexpr dpp::snowflake general{111};
constexpr dpp::snowflake music{222};

} // namespace

TEST_CASE("joining follows the target and moves only when it has to", "[voice]") {
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

TEST_CASE("joining says whom it followed, as the Java bot did", "[voice]") {
    using latibot::commands::describe_join;
    CHECK(describe_join(join_action::connect, dpp::snowflake{42}) == "ok joining <@42>");
    CHECK(describe_join(join_action::move, dpp::snowflake{42}) == "ok moving to <@42>");
}

TEST_CASE("a target who left voice is not followed to their old channel", "[voice]") {
    // The bot staying put matters more than the wording: the previous
    // implementation read the stale channel id and moved to an empty channel.
    const auto decision = plan_join(nowhere, music);
    CHECK(decision.action == join_action::target_not_in_voice);
    CHECK(decision.channel_id.empty());
}

TEST_CASE("join and leave pass the registry's checks, and the room sees the bot come and go", "[voice]") {
    // The checks registry::add makes of every command, for these
    // (docs/modules/Module_Plan_Final.md §10). A refusal is still only for
    // whoever asked (src/core/docs/Basic_Commands.md §5).
    latibot::commands::registry commands;
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::join_command>()));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::leave_command>()));

    const latibot::commands::join_command join;
    const latibot::commands::leave_command leave;
    for (const latibot::commands::command_info* each : {&join.info(), &leave.info()}) {
        INFO(each->name);
        CHECK(each->responses_for("").result == dpp::m_suppress_notifications);
        CHECK(each->responses_for("").refusal == dpp::m_ephemeral);
    }
}
