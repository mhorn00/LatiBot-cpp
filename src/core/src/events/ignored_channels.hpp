#pragma once

#include "core/events/message_pipeline.hpp"

#include <dpp/snowflake.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::events {

/// What `/channels ignore` leaves out of a channel
/// (src/core/docs/Message_Pipeline.md §2.4).
enum class channel_scope : std::uint8_t {
    /// Every message: the bot pays the channel no attention.
    everything,
    /// The language model's part: it neither answers nor reads along there.
    model,
    /// Every bot's message, LatiBot's own and webhooks' included, as in a log
    /// channel, whose lines are full of trigger words.
    bots,
};

/// As stored and as the command's choices: "everything", "llm", "bots".
[[nodiscard]] auto to_string(channel_scope scope) noexcept -> std::string_view;
[[nodiscard]] auto channel_scope_from_string(std::string_view text) noexcept -> std::optional<channel_scope>;

/// Adds what `scope` leaves out to `rules`.
auto apply(channel_rules& rules, channel_scope scope) noexcept -> void;

/// `ignored_channels`: one rule per channel, per guild. A thread follows its
/// parent channel's rule, and its own when it has one too.
class ignored_channels {
public:
    explicit ignored_channels(db::database& db) : db_(&db) {}

    /// Sets the channel's rule, replacing any it had. False when it already
    /// had this one, which the command reports rather than claiming a change.
    auto ignore(dpp::snowflake guild_id, dpp::snowflake channel_id, channel_scope scope) -> bool;

    /// False when the channel had no rule.
    auto hear(dpp::snowflake guild_id, dpp::snowflake channel_id) -> bool;

    [[nodiscard]] auto find(dpp::snowflake guild_id, dpp::snowflake channel_id) const -> std::optional<channel_scope>;
    [[nodiscard]] auto for_guild(dpp::snowflake guild_id) const -> std::vector<std::pair<dpp::snowflake, channel_scope>>;

    /// What a message in `channel_id` is spared, with `parent` the channel a
    /// thread belongs to, or 0.
    [[nodiscard]] auto rules(dpp::snowflake guild_id, dpp::snowflake channel_id, dpp::snowflake parent) const -> channel_rules;

private:
    db::database* db_;
};

} // namespace latibot::events
