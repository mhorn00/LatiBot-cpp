// The flags the bot chooses for what it posts, and the helpers that keep a
// message to the ones it is allowed.

#include "core/discord/message_flags.hpp"

#include <catch2/catch_test_macros.hpp>

using latibot::discord::apply_flags;
using latibot::discord::channel_flags;
using latibot::discord::describe_flags;

TEST_CASE("applying flags replaces the choosable ones and leaves the rest", "[discord]") {
    dpp::message message("hello");
    message.flags = dpp::m_ephemeral | dpp::m_crossposted;

    apply_flags(message, dpp::m_suppress_embeds);
    CHECK(message.flags == (dpp::m_suppress_embeds | dpp::m_crossposted));

    // Outside the choosable set, a wanted flag is dropped rather than applied.
    apply_flags(message, dpp::m_ephemeral | dpp::m_suppress_notifications, latibot::discord::channel_message_flags);
    CHECK(message.flags == (dpp::m_suppress_notifications | dpp::m_crossposted));
}

TEST_CASE("stored message flags are narrowed to what a channel message may carry", "[discord]") {
    // A hand-edited row, or one from a build that allowed more, cannot bring
    // in ephemeral or anything else unexpected.
    CHECK(channel_flags(dpp::m_suppress_notifications | dpp::m_ephemeral | dpp::m_urgent) == dpp::m_suppress_notifications);
    CHECK(channel_flags(dpp::m_suppress_embeds) == dpp::m_suppress_embeds);
    CHECK(channel_flags(-1) == latibot::discord::channel_message_flags);
}

TEST_CASE("flags are named for the log", "[discord]") {
    CHECK(describe_flags(0) == "none");
    CHECK(describe_flags(dpp::m_ephemeral | dpp::m_suppress_notifications | dpp::m_suppress_embeds) == "ephemeral, silent, no previews");
    CHECK(describe_flags(dpp::m_crossposted) == "0x1");
}
