#pragma once

#include "core/discord/message_flags.hpp"

#include <dpp/coro/task.h>
#include <dpp/snowflake.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace latibot::events {

/// A message, reduced to what a stage needs in order to decide.
///
/// Deliberately not `dpp::message`: a stage that takes plain data can be
/// tested without a gateway, and the shell is the only place that has to know
/// how Discord spells any of this.
struct incoming_message {
    dpp::snowflake guild_id;
    dpp::snowflake channel_id;
    dpp::snowflake message_id;
    dpp::snowflake author_id;

    bool from_self = false;
    bool from_bot = false;

    /// Whether this guild allows LatiBot to hear this bot
    /// (src/core/docs/Message_Pipeline.md §2.1). Only meaningful with
    /// `from_bot`; resolved by the shell from `bot_allowlist`.
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

    /// The author's roles here, for the model's blacklist
    /// (src/modules/llm/docs/Language_Model.md §2.2).
    std::vector<dpp::snowflake> author_roles;

    /// Whether the message @mentions LatiBot, or replies to one of its
    /// messages: two of the three ways to address it
    /// (src/modules/llm/docs/Language_Model.md §2.1).
    ///
    /// A reply to a slash command's result or refusal is not a reply to the
    /// bot, and a reply's ping is not a mention: only one written in the
    /// message is.
    bool mentions_bot = false;
    bool replies_to_bot = false;

    /// The message this one replies to, whoever wrote it; 0 for a message
    /// that is not a reply.
    dpp::snowflake reply_to;

    /// Whether it @mentions somebody other than the bot and its author: a
    /// sign it is meant for them (src/modules/llm/docs/Language_Model.md
    /// §2.10).
    bool mentions_others = false;

    /// Set by the pipeline once a stage has answered the message, so a later
    /// stage can stand back: the simple trigger wins over an advanced one
    /// (src/core/docs/Message_Pipeline.md §2.2).
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

/// Work a stage hands off, to be done without holding the pipeline up:
/// posting a replacement, answering with the model.
///
/// A stage with work of its own decides it as its own type, which its tests
/// read, and `carried_out_by` turns each into one of these when the stage is
/// added to the pipeline. So the core never needs to know a feature's
/// actions.
struct background_task {
    /// What it is, for the log line if it fails: "posting a replacement".
    std::string what;

    std::move_only_function<dpp::task<void>()> run;
};

/// Something a stage wants done.
///
/// Stages return actions rather than performing them, which is what keeps
/// them pure: a test reads the actions, and the shell is the only code that
/// touches Discord.
using action = std::variant<send_message, stop_bot, background_task>;

/// What a stage decided: its actions, and what that means for the rest.
///
/// `Action` is `action` for the pipeline. A stage with work of its own
/// decides `own_action<Own>` instead, and `carried_out_by` makes it an
/// `action` (src/core/docs/Message_Pipeline.md §3).
template <typename Action>
struct stage_decision {
    std::vector<Action> actions;

    /// Whether later stages should be skipped. A trigger response and a URL
    /// replacement can both fire on one message; an LLM reply should not
    /// follow a goodbye (src/core/docs/Message_Pipeline.md §2.2).
    bool consumed = false;

    /// Whether this stage answered the message, which later stages see as
    /// `incoming_message::answered`.
    bool answered = false;
};

using stage_result = stage_decision<action>;

/// The core's actions, or a stage's own.
template <typename Own>
using own_action = std::variant<send_message, stop_bot, Own>;

/// What a stage with actions of its own decides.
template <typename Own>
using own_stage_result = stage_decision<own_action<Own>>;

/// A pipeline stage made of `stage`, which decides `own_stage_result<Own>`:
/// each `Own` becomes a `background_task` named `what` that runs
/// `carry_out(own)`, and everything else passes through as it was.
template <typename Own, typename Stage, typename CarryOut>
[[nodiscard]] auto carried_out_by(Stage stage, std::string what, CarryOut carry_out)
    -> std::function<stage_result(const incoming_message&)> {
    return [stage = std::move(stage), what = std::move(what), carry_out = std::move(carry_out)](const incoming_message& message) mutable {
        own_stage_result<Own> decided = stage(message);
        stage_result result{.actions = {}, .consumed = decided.consumed, .answered = decided.answered};
        result.actions.reserve(decided.actions.size());
        for (own_action<Own>& wanted : decided.actions) {
            std::visit(
                [&](auto& step) {
                    if constexpr (std::is_same_v<std::decay_t<decltype(step)>, Own>) {
                        result.actions.emplace_back(background_task{
                            .what = what, .run = [carry_out, own = std::move(step)]() mutable { return carry_out(std::move(own)); }});
                    } else {
                        result.actions.emplace_back(std::move(step));
                    }
                },
                wanted);
        }
        return result;
    };
}

/// The ordered stages a message passes through.
///
/// Each stage runs at a position, lowest first, rather than in the order it
/// was added, so the order does not depend on which module started first
/// (`stage_order`, src/core/docs/Message_Pipeline.md §2.2).
class pipeline {
public:
    using stage_fn = std::function<stage_result(const incoming_message&)>;

    /// Adds a stage at `position`, one of `stage_order`'s. The name is for
    /// logging and for reading the order back in a test. Throws
    /// std::logic_error when another stage holds the position: which of two
    /// would run first is exactly what the positions exist to say.
    auto add(int position, std::string name, stage_fn handler) -> void;

    /// Everything the stages asked for, in order.
    ///
    /// Our own messages produce nothing, and so do other bots' unless this
    /// guild allows that one (src/core/docs/Message_Pipeline.md §2.1).
    /// Reaching the stages is only permission to be considered: a stage still
    /// decides for itself whether it answers a bot.
    [[nodiscard]] auto run(const incoming_message& message) const -> std::vector<action>;

    [[nodiscard]] auto stage_names() const -> std::vector<std::string_view>;
    [[nodiscard]] auto size() const noexcept -> std::size_t { return stages_.size(); }

private:
    struct stage {
        int position = 0;
        std::string name;
        stage_fn handler;
    };

    std::vector<stage> stages_;
};

} // namespace latibot::events
