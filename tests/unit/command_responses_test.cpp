// How each command's messages are flagged (see `command_info::responses`),
// checked against the real commands rather than a stand-in, so a misspelt
// subcommand or a view that should be public cannot slip through.

#include "core/commands/basic.hpp"
#include "core/commands/bots.hpp"
#include "core/commands/linkstats.hpp"
#include "core/commands/message_options.hpp"
#include "core/commands/registry.hpp"
#include "core/commands/urlrepl.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/events/bot_allowlist.hpp"
#include "core/events/reactions.hpp"
#include "core/events/url_rules.hpp"

#include "mocks/mock_clock.hpp"
#include "support/schema.hpp"
#include "support/slash_event.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using latibot::commands::command_info;

namespace {

/// Every store a command needs, over one database.
struct stores {
    latibot::db::database db{":memory:"};
    latibot::config::guild_settings settings{db};
    latibot::events::bot_allowlist allowlist{db};
    latibot::events::url_rule_store url_rules{db};
    latibot::events::reaction_store reactions{db};
    latibot::testing::mock_clock clock;

    stores() { latibot::testing::create_schema(db); }
};

} // namespace

TEST_CASE("every command's response flags pass registration", "[commands]") {
    // The commands that need a live cluster (/say, /status, /nickname) are
    // left out; their flags are the defaults, and the bot registers them at
    // startup, where the same check would stop it.
    stores all;
    latibot::commands::registry commands;

    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::ping_command>(all.clock)));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::join_command>()));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::leave_command>()));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::shutdown_command>([] {})));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::goodbye_command>(all.settings)));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::bots_command>(all.allowlist)));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::links_command>(all.url_rules)));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::urltoggle_command>(all.url_rules)));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::linkstats_command>(all.reactions)));
    CHECK(commands.size() == 9);
}

TEST_CASE("the views meant for the room are public and the rest are private", "[commands]") {
    stores all;

    const latibot::commands::linkstats_command linkstats(all.reactions);
    const command_info& stats = linkstats.info();
    for (const char* board : {"top", "reactions", "alias list"}) {
        INFO(board);
        CHECK(stats.responses_for(board).result == dpp::m_suppress_notifications);
    }
    for (const char* private_answer :
         {"duplicates", "alias add", "alias remove", "recompute start", "recompute cancel", "images on", "images off"}) {
        INFO(private_answer);
        CHECK(stats.responses_for(private_answer).result == dpp::m_ephemeral);
    }
    CHECK(stats.responses_for("top").refusal == dpp::m_ephemeral);
    CHECK(stats.responses_for("recompute start").post == dpp::m_suppress_notifications);

    // The room sees the bot come, go or stop, so it sees why; a refusal is
    // still only for whoever asked (docs/features/Basic_Commands.md §5).
    const latibot::commands::join_command join;
    const latibot::commands::leave_command leave;
    const latibot::commands::shutdown_command shutdown([] {});
    for (const command_info* voice : {&join.info(), &leave.info(), &shutdown.info()}) {
        INFO(voice->name);
        CHECK(voice->responses_for("").result == dpp::m_suppress_notifications);
        CHECK(voice->responses_for("").refusal == dpp::m_ephemeral);
    }

    // The URL commands stay private, as they have been since the port.
    const latibot::commands::urltoggle_command urltoggle(all.url_rules);
    CHECK(urltoggle.info().responses_for("").result == dpp::m_ephemeral);
}

TEST_CASE("the URL dry run hides the previews of the links it shows", "[commands]") {
    stores all;
    const latibot::commands::links_command links(all.url_rules);

    CHECK(links.info().responses_for("test").result == (dpp::m_ephemeral | dpp::m_suppress_embeds));
    CHECK(links.info().responses_for("list").result == dpp::m_ephemeral);
}

// --------------------------------------------------------------------------
// silent and previews, on messages set up to be posted later
// --------------------------------------------------------------------------

TEST_CASE("the silent and previews options change only what they are given", "[commands]") {
    using latibot::commands::apply_message_options;
    using latibot::testing::bool_option;
    using latibot::testing::slash_event;

    latibot::discord::message_flags flags = dpp::m_suppress_notifications;

    // Left out, as an edit that is about something else leaves them.
    apply_message_options(slash_event("trigger", "edit"), flags);
    CHECK(flags == dpp::m_suppress_notifications);

    apply_message_options(slash_event("trigger", "edit", {bool_option("silent", false)}), flags);
    CHECK(flags == 0);

    apply_message_options(slash_event("trigger", "edit", {bool_option("previews", false)}), flags);
    CHECK(flags == dpp::m_suppress_embeds);

    apply_message_options(slash_event("trigger", "edit", {bool_option("silent", true), bool_option("previews", true)}), flags);
    CHECK(flags == dpp::m_suppress_notifications);
}

TEST_CASE("only a difference from silent with previews is worth describing", "[commands]") {
    using latibot::commands::describe_message_options;

    CHECK(describe_message_options(dpp::m_suppress_notifications).empty());
    CHECK(describe_message_options(0) == "notifies");
    CHECK(describe_message_options(dpp::m_suppress_notifications | dpp::m_suppress_embeds) == "no previews");
    CHECK(describe_message_options(dpp::m_suppress_embeds) == "notifies, no previews");
}
