#pragma once

#include <dpp/snowflake.h>

#include <chrono>
#include <map>
#include <mutex>
#include <string_view>
#include <utility>

namespace latibot::ports {
class clock;
}

namespace latibot::llm {

// Conversation mode (src/modules/llm/docs/Language_Model.md §2.10): once
// the bot has answered someone, it keeps an ear on the channel for a while,
// and joins in where a cheap check says a message is for it.

/// A guild's conversation settings, from `llm_settings`.
struct conversation_rules {
    /// Off until `/llm conversation on`: every check costs a little.
    bool enabled = false;

    /// How long a window stays open after the bot's latest reply.
    std::chrono::seconds quiet_after{180};

    /// The most replies it makes in one window without being addressed.
    int replies = 6;

    /// How long a window lasts at most, however lively.
    std::chrono::seconds longest{900};

    /// The longest it waits for somebody to stop typing before it checks.
    std::chrono::seconds typing_wait{6};
};

/// Whether `text` names the bot anywhere, as a whole word: "thanks latibot"
/// and "what do you think, LatiBot?", but not "latibots".
[[nodiscard]] auto names_bot(std::string_view text, std::string_view bot_name) -> bool;

/// The bot's conversations, at most one per channel. Thread-safe.
class conversation_windows {
public:
    explicit conversation_windows(ports::clock& clock) : clock_(&clock) {}

    /// The bot answered someone who addressed it, or named it: a fresh
    /// window, whatever was open before.
    auto opened(dpp::snowflake channel) -> void;

    /// The bot joined in without being addressed: the window stays open
    /// longer, and is one reply nearer its cap.
    auto joined_in(dpp::snowflake channel) -> void;

    /// Whether a window is open in `channel`: the guild has conversation
    /// mode on, the bot replied there recently enough, and neither of the
    /// window's limits is reached.
    [[nodiscard]] auto open(dpp::snowflake channel, const conversation_rules& rules) const -> bool;

private:
    struct window {
        std::chrono::steady_clock::time_point started;
        std::chrono::steady_clock::time_point last_reply;
        int unprompted = 0;
    };

    ports::clock* clock_;
    mutable std::mutex mutex_;
    std::map<dpp::snowflake, window> windows_;
};

/// Who is typing in each channel, and each person's latest message there.
/// Thread-safe.
///
/// Discord says when somebody starts typing, and not when they stop: it
/// shows them typing for ten seconds, or until they send something.
class channel_activity {
public:
    explicit channel_activity(ports::clock& clock) : clock_(&clock) {}

    auto started_typing(dpp::snowflake channel, dpp::snowflake user) -> void;

    /// `user` sent `message`: they have stopped typing, and that is their
    /// latest message.
    auto posted(dpp::snowflake channel, dpp::snowflake user, dpp::snowflake message) -> void;

    [[nodiscard]] auto anyone_typing(dpp::snowflake channel) const -> bool;

    /// Whether `message` is still `user`'s latest in `channel`. A newer one
    /// stands in for it, since it comes with it in the transcript.
    [[nodiscard]] auto is_latest(dpp::snowflake channel, dpp::snowflake user, dpp::snowflake message) const -> bool;

private:
    using key = std::pair<dpp::snowflake, dpp::snowflake>;

    ports::clock* clock_;
    mutable std::mutex mutex_;
    std::map<key, std::chrono::steady_clock::time_point> typing_;
    std::map<key, dpp::snowflake> latest_;
};

/// How long Discord shows somebody typing after they start.
inline constexpr std::chrono::seconds typing_shown_for{10};

} // namespace latibot::llm
