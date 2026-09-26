// How each command's messages are flagged (see `command_info::responses`),
// checked against the real commands rather than a stand-in, so a misspelt
// subcommand or a view that should be public cannot slip through.

#include "core/commands/basic.hpp"
#include "core/commands/bots.hpp"
#include "core/commands/linkstats.hpp"
#include "core/commands/message_options.hpp"
#include "core/commands/midnight.hpp"
#include "core/commands/nickname.hpp"
#include "core/commands/registry.hpp"
#include "core/commands/trigger.hpp"
#include "core/commands/urlrepl.hpp"
#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/db/migrations.hpp"
#include "core/events/bot_allowlist.hpp"
#include "core/events/midnight.hpp"
#include "core/events/nicknames.hpp"
#include "core/events/reactions.hpp"
#include "core/events/triggers.hpp"
#include "core/events/url_rules.hpp"

#include "mocks/mock_clock.hpp"
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
    latibot::events::trigger_store triggers{db};
    latibot::events::nickname_store nicknames{db};
    latibot::events::midnight_store midnight{db};
    latibot::events::url_rule_store url_rules{db};
    latibot::events::reaction_store reactions{db};
    latibot::testing::mock_clock clock;

    stores() { latibot::db::migrate(db); }
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
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::trigger_command>(all.triggers)));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::nicknames_command>(all.nicknames)));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::midnight_command>(all.midnight, all.clock)));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::urlrepl_command>(all.url_rules)));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::urltoggle_command>(all.url_rules)));
    CHECK_NOTHROW(commands.add(std::make_unique<latibot::commands::linkstats_command>(all.reactions)));
    CHECK(commands.size() == 12);
}

TEST_CASE("the views meant for the room are public and the rest are private", "[commands]") {
    stores all;

    const latibot::commands::nicknames_command nicknames(all.nicknames);
    CHECK(nicknames.info().responses_for("").result == 0);
    CHECK(nicknames.info().responses_for("").refusal == dpp::m_ephemeral);

    const latibot::commands::linkstats_command linkstats(all.reactions);
    const command_info& stats = linkstats.info();
    for (const char* board : {"top", "user", "emojis", "alias list"}) {
        INFO(board);
        CHECK(stats.responses_for(board).result == 0);
    }
    for (const char* private_answer : {"alias add", "alias remove", "recompute start", "recompute cancel"}) {
        INFO(private_answer);
        CHECK(stats.responses_for(private_answer).result == dpp::m_ephemeral);
    }
    CHECK(stats.responses_for("top").refusal == dpp::m_ephemeral);
    CHECK(stats.responses_for("recompute start").post == 0);

    const latibot::commands::trigger_command trigger(all.triggers);
    CHECK(trigger.info().responses_for("panel").result == dpp::m_ephemeral);

    // The room sees the bot come, go or stop, so it sees why; a refusal is
    // still only for whoever asked (plan §6).
    const latibot::commands::join_command join;
    const latibot::commands::leave_command leave;
    const latibot::commands::shutdown_command shutdown([] {});
    for (const command_info* voice : {&join.info(), &leave.info(), &shutdown.info()}) {
        INFO(voice->name);
        CHECK(voice->responses_for("").result == 0);
        CHECK(voice->responses_for("").refusal == dpp::m_ephemeral);
    }

    // The URL commands stay private, as they have been since the port.
    const latibot::commands::urltoggle_command urltoggle(all.url_rules);
    CHECK(urltoggle.info().responses_for("").result == dpp::m_ephemeral);
}

TEST_CASE("the URL dry run hides the previews of the links it shows", "[commands]") {
    stores all;
    const latibot::commands::urlrepl_command urlrepl(all.url_rules);

    CHECK(urlrepl.info().responses_for("test").result == (dpp::m_ephemeral | dpp::m_suppress_embeds));
    CHECK(urlrepl.info().responses_for("list").result == dpp::m_ephemeral);
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
