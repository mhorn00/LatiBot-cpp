#pragma once

#include "core/commands/registry.hpp"

#include <dpp/presence.h>
#include <dpp/snowflake.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace dpp {
class cluster;
}

namespace latibot::ports {
class clock;
}

namespace latibot::config {
class guild_settings;
}

namespace latibot::commands {

// --------------------------------------------------------------------------
// Decisions
//
// The interesting part of each command is a function from plain data to a
// decision (plan §17.3). The handler below resolves Discord's state into
// arguments, calls one of these, and carries the answer out.
// --------------------------------------------------------------------------

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

[[nodiscard]] join_decision plan_join(dpp::snowflake target_channel, dpp::snowflake bot_channel) noexcept;

/// What `/say` should do with the options it was given.
enum class say_action : std::uint8_t {
    /// Post the message in the channel the command came from.
    send,
    /// Post it as a reply to `reply_to`.
    reply,
    /// The `reply` option was not a message id.
    bad_reply_id,
    /// The message was empty or only whitespace. Discord's own length limit
    /// does not catch `"   "`, and sending that fails at the API instead.
    blank_message,
};

struct say_decision {
    say_action action = say_action::send;
    dpp::snowflake reply_to;
};

/// `reply_to` is the raw option text, empty when the option was not given.
[[nodiscard]] say_decision plan_say(std::string_view message, std::string_view reply_to);

/// Maps the `type` option of `/status` onto a DPP activity type.
///
/// Case-insensitive, and unknown or empty text falls back to "playing", which
/// is what the Java bot did when the option was omitted.
[[nodiscard]] dpp::activity_type parse_activity_type(std::string_view name);

/// Builds the activity for a presence update.
///
/// A custom status is the odd one out: Discord reads its text from `state`
/// rather than `name`, and expects the literal name "Custom Status".
[[nodiscard]] dpp::activity make_activity(dpp::activity_type type, const std::string& text);

// --------------------------------------------------------------------------
// Commands
// --------------------------------------------------------------------------

/// Round-trip time to Discord.
class ping_command final : public command {
public:
    explicit ping_command(ports::clock& clock);

    [[nodiscard]] const command_info& info() const override { return info_; }
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;

private:
    command_info info_;
    ports::clock* clock_;
};

/// Posts a message as the bot, optionally as a reply to an existing message.
class say_command final : public command {
public:
    explicit say_command(dpp::cluster& cluster);

    [[nodiscard]] const command_info& info() const override { return info_; }
    [[nodiscard]] dpp::slashcommand build(const std::string& name, dpp::snowflake application_id) const override;
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;

private:
    command_info info_;
    dpp::cluster* cluster_;
};

/// Sets the bot's presence.
class status_command final : public command {
public:
    explicit status_command(dpp::cluster& cluster);

    [[nodiscard]] const command_info& info() const override { return info_; }
    [[nodiscard]] dpp::slashcommand build(const std::string& name, dpp::snowflake application_id) const override;
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;

private:
    command_info info_;
    dpp::cluster* cluster_;
};

/// Joins the caller's voice channel, or another user's.
class join_command final : public command {
public:
    join_command();

    [[nodiscard]] const command_info& info() const override { return info_; }
    [[nodiscard]] dpp::slashcommand build(const std::string& name, dpp::snowflake application_id) const override;
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;

private:
    command_info info_;
};

/// Leaves the voice channel.
class leave_command final : public command {
public:
    leave_command();

    [[nodiscard]] const command_info& info() const override { return info_; }
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;

private:
    command_info info_;
};

/// Stops the bot.
class shutdown_command final : public command {
public:
    /// `request_shutdown` is called after the reply has been delivered, so the
    /// caller sees an answer rather than a failed interaction.
    explicit shutdown_command(std::function<void()> request_shutdown);

    [[nodiscard]] const command_info& info() const override { return info_; }
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;

private:
    command_info info_;
    std::function<void()> request_shutdown_;
};

/// Shows or changes the phrase that stops the bot (plan §6).
///
/// Separate from `/shutdown` because it edits a setting rather than acting on
/// it, and because the phrase is per guild while `/shutdown` is not.
class goodbye_command final : public command {
public:
    explicit goodbye_command(config::guild_settings& settings);

    [[nodiscard]] const command_info& info() const override { return info_; }
    [[nodiscard]] dpp::slashcommand build(const std::string& name, dpp::snowflake application_id) const override;
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;

private:
    command_info info_;
    config::guild_settings* settings_;
};

/// Adds all of the above to `registry`.
void add_basic_commands(registry& into, dpp::cluster& cluster, ports::clock& clock, config::guild_settings& settings,
                        std::function<void()> request_shutdown);

} // namespace latibot::commands
