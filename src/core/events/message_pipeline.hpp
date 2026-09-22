#pragma once

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
    dpp::snowflake author_id;

    bool from_self = false;
    bool from_bot = false;

    /// Whether the author has Administrator in this guild. Resolved by the
    /// shell, since it depends on roles and overwrites.
    bool author_is_administrator = false;

    std::string content;
};

/// Post a message in a channel.
struct send_message {
    dpp::snowflake channel_id;
    std::string content;
};

/// Stop the bot, after a pause long enough for the goodbye to be delivered.
struct stop_bot {
    std::chrono::milliseconds after{0};
};

/// Something a stage wants done.
///
/// Stages return actions rather than performing them, which is what keeps
/// them pure: a test reads the actions, and the shell is the only code that
/// touches Discord.
using action = std::variant<send_message, stop_bot>;

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
    /// Messages from the bot itself and from other bots produce nothing: an
    /// answer to our own message is a loop, and bot-to-bot conversation is
    /// opt-in and does not arrive until plan v4 §14.4.
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
