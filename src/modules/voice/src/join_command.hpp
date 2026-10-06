#pragma once

#include "core/commands/registry.hpp"

#include <dpp/snowflake.h>

#include <cstdint>
#include <string>

namespace latibot::commands {

// `/join` and `/leave` (docs/features/Voice_Channels.md §2.1): the voice
// module's, since joining is only worth anything to speech and music.

/// What `/join` should do. Voice channel ids are `dpp::snowflake`, where 0
/// means "not in a voice channel", matching how DPP reports an absent id.
enum class join_action : std::uint8_t {
    /// The bot is not connected in this guild; connect.
    connect,
    /// The bot is connected to a different channel in this guild; move.
    move,
    /// The bot is already where it was asked to be.
    already_there,
    /// Nobody to follow: the target is not in a voice channel.
    target_not_in_voice,
};

struct join_decision {
    join_action action = join_action::target_not_in_voice;

    /// The channel to connect to. 0 unless the action is `connect` or `move`.
    dpp::snowflake channel_id;
};

[[nodiscard]] auto plan_join(dpp::snowflake target_channel, dpp::snowflake bot_channel) noexcept -> join_decision;

/// What `/join` says in the channel once it goes: "ok joining <@id>" or "ok
/// moving to <@id>", naming whom it followed, as the Java bot did. The reply
/// is public, since the room sees the bot arrive anyway, and is sent with
/// mentions off, so the name shows without pinging anyone.
[[nodiscard]] auto describe_join(join_action action, dpp::snowflake followed) -> std::string;

/// Joins the caller's voice channel, or another user's.
class join_command final : public command {
public:
    join_command();

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    command_info info_;
};

/// Leaves the voice channel.
class leave_command final : public command {
public:
    leave_command();

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    command_info info_;
};

} // namespace latibot::commands
