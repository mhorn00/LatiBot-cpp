#pragma once

#include "core/discord/message_flags.hpp"
#include "core/events/url_rules.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace latibot::events {

/// A message, reduced to what a stage needs in order to decide.
///
/// Deliberately not `dpp::message`: a stage that takes plain data can be
/// tested without a gateway, and the shell is the only place that has to know
/// how Discord spells any of this (plan v4 §17.3).
struct incoming_message {
    dpp::snowflake guild_id;
    dpp::snowflake channel_id;
    dpp::snowflake message_id;
    dpp::snowflake author_id;

    bool from_self = false;
    bool from_bot = false;

    /// Whether this guild allows LatiBot to hear this bot (plan v4 §14.4).
    /// Only meaningful with `from_bot`; resolved by the shell from
    /// `bot_allowlist`.
    bool author_is_allowed_bot = false;

    /// Whether the author has Administrator in this guild. Resolved by the
    /// shell, since it depends on roles and overwrites.
    bool author_is_administrator = false;

    /// The author already turned this message's link previews off.
    bool embeds_suppressed = false;

    std::string content;
};

/// Post a message in a channel.
struct send_message {
    dpp::snowflake channel_id;
    std::string content;

    /// Silent and without previews, or not, as whatever asked for the
    /// message decided. Only `discord::channel_message_flags` are applied.
    /// Silent unless said otherwise: these are jokes, acknowledgements and
    /// scheduled posts, not things to be pinged for, as in the Java bot.
    discord::message_flags flags = dpp::m_suppress_notifications;
};

/// Stop the bot, after a pause long enough for the goodbye to be delivered.
struct stop_bot {
    std::chrono::milliseconds after{0};
};

/// Post working previews for links a URL rule covers (plan v4 §9.2).
///
/// Carrying this out takes several calls and then some waiting, which is why
/// it is an action of its own rather than a `send_message`: what gets posted
/// next depends on whether Discord manages to embed the first attempt.
struct replace_links {
    dpp::snowflake guild_id;
    dpp::snowflake channel_id;

    /// The message the links were in, whose own previews get turned off.
    dpp::snowflake message_id;

    /// Who posted them, which is who the reaction statistics credit.
    dpp::snowflake author_id;

    std::vector<planned_link> links;
};

/// Something a stage wants done.
///
/// Stages return actions rather than performing them, which is what keeps
/// them pure: a test reads the actions, and the shell is the only code that
/// touches Discord.
using action = std::variant<send_message, stop_bot, replace_links>;

struct stage_result {
    std::vector<action> actions;

    /// Whether later stages should be skipped. A trigger response and a URL
    /// replacement can both fire on one message; an LLM reply should not
    /// follow a goodbye (plan v4 §5.4).
    bool consumed = false;
};

/// The ordered stages a message passes through.
///
/// The order is a list rather than a chain of calls, so changing it is a
/// matter of moving one line (plan v4 §5.4).
class pipeline {
public:
    using stage_fn = std::function<stage_result(const incoming_message&)>;

    /// The name is for logging and for reading the order back in a test.
    void add(std::string name, stage_fn handler);

    /// Everything the stages asked for, in order.
    ///
    /// Our own messages produce nothing, and so do other bots' unless this
    /// guild allows that one (plan v4 §5.4). Reaching the stages is only
    /// permission to be considered: a stage still decides for itself whether
    /// it answers a bot.
    [[nodiscard]] std::vector<action> run(const incoming_message& message) const;

    [[nodiscard]] std::vector<std::string_view> stage_names() const;
    [[nodiscard]] std::size_t size() const noexcept { return stages_.size(); }

private:
    struct stage {
        std::string name;
        stage_fn handler;
    };

    std::vector<stage> stages_;
};

} // namespace latibot::events
