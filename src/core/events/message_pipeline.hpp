#pragma once

#include "core/discord/message_flags.hpp"
#include "core/events/url_rules.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstdint>
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
/// how Discord spells any of this (plan §17.3).
struct incoming_message {
    dpp::snowflake guild_id;
    dpp::snowflake channel_id;
    dpp::snowflake message_id;
    dpp::snowflake author_id;

    bool from_self = false;
    bool from_bot = false;

    /// Whether this guild allows LatiBot to hear this bot (plan §14.4).
    /// Only meaningful with `from_bot`; resolved by the shell from
    /// `bot_allowlist`.
    bool author_is_allowed_bot = false;

    /// Whether the author has Administrator in this guild. Resolved by the
    /// shell, since it depends on roles and overwrites.
    bool author_is_administrator = false;

    /// The author already turned this message's link previews off.
    bool embeds_suppressed = false;

    std::string content;

    /// How the author is shown to people: their server nickname, display
    /// name or username, whichever they have.
    std::string author_name;

    /// The author's roles here, for the model's blacklist (plan §14.6).
    std::vector<dpp::snowflake> author_roles;

    /// Whether the message @mentions LatiBot, or replies to one of its
    /// messages: two of the three ways to address it (plan §14.3).
    bool mentions_bot = false;
    bool replies_to_bot = false;

    /// Set by the pipeline once a stage has answered the message, so a later
    /// stage can stand back: the simple trigger wins over an advanced one
    /// (plan §14.3).
    bool answered = false;
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

    /// What this is, for the log line saying whether it was posted: "trigger
    /// 3's reply", "midnight message 2".
    std::string what = "a message";
};

/// Stop the bot, after a pause long enough for the goodbye to be delivered.
struct stop_bot {
    std::chrono::milliseconds after{0};
};

/// Post working previews for links a URL rule covers (plan §9.2).
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

/// Ask the model to answer a message (plan §14).
///
/// An action of its own because answering takes a model call, maybe several,
/// and the stage that decides to answer cannot wait for them: it only
/// decides, and the shell hands this to the responder.
struct ask_llm {
    dpp::snowflake guild_id;
    dpp::snowflake channel_id;
    dpp::snowflake message_id;
    dpp::snowflake author_id;
    std::string author_name;
    std::string content;
    bool author_is_bot = false;

    /// Set when an advanced trigger fired rather than someone addressing the
    /// bot: which one, and what it asks the model to say.
    std::int64_t trigger_id = 0;
    std::string context_prompt;

    /// Also say the reply in the voice session this channel belongs to
    /// (plan §14.2).
    bool speak = false;

    /// How long to wait before answering: bot-to-bot pacing (plan §14.4).
    std::chrono::seconds wait{0};
};

/// Something a stage wants done.
///
/// Stages return actions rather than performing them, which is what keeps
/// them pure: a test reads the actions, and the shell is the only code that
/// touches Discord.
using action = std::variant<send_message, stop_bot, replace_links, ask_llm>;

struct stage_result {
    std::vector<action> actions;

    /// Whether later stages should be skipped. A trigger response and a URL
    /// replacement can both fire on one message; an LLM reply should not
    /// follow a goodbye (plan §5.4).
    bool consumed = false;

    /// Whether this stage answered the message, which later stages see as
    /// `incoming_message::answered`.
    bool answered = false;
};

/// The ordered stages a message passes through.
///
/// The order is a list rather than a chain of calls, so changing it is a
/// matter of moving one line (plan §5.4).
class pipeline {
public:
    using stage_fn = std::function<stage_result(const incoming_message&)>;

    /// The name is for logging and for reading the order back in a test.
    auto add(std::string name, stage_fn handler) -> void;

    /// Everything the stages asked for, in order.
    ///
    /// Our own messages produce nothing, and so do other bots' unless this
    /// guild allows that one (plan §5.4). Reaching the stages is only
    /// permission to be considered: a stage still decides for itself whether
    /// it answers a bot.
    [[nodiscard]] auto run(const incoming_message& message) const -> std::vector<action>;

    [[nodiscard]] auto stage_names() const -> std::vector<std::string_view>;
    [[nodiscard]] auto size() const noexcept -> std::size_t { return stages_.size(); }

private:
    struct stage {
        std::string name;
        stage_fn handler;
    };

    std::vector<stage> stages_;
};

} // namespace latibot::events
