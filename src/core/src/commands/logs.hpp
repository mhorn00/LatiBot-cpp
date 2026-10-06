#pragma once

#include "core/commands/registry.hpp"
#include "core/config/bootstrap.hpp"
#include "core/events/log_channel.hpp"
#include "core/ports/discord_gateway.hpp"

#include <dpp/appcommand.h>

#include <optional>
#include <string>

namespace latibot::commands {

/// Whether this user may choose where the log goes.
///
/// Only the users in `trusted_users`: the log covers every server the bot is
/// in, so an administrator of one of them, even a trusted one, could
/// otherwise read the others.
[[nodiscard]] auto may_configure_logs(const config::bootstrap& settings, dpp::snowflake user_id) -> bool;

/// The body of `/logs show`, as its own function so it can be tested.
/// `here` is the guild asking, since a channel in another server cannot be
/// shown as a link that works.
[[nodiscard]] auto render_log_channel(const events::log_channel_status& status, dpp::snowflake here) -> std::string;

/// `/logs set | level | off | show`: which one channel the bot's own log is
/// posted to, and from which level up.
class logs_command final : public command {
public:
    logs_command(const config::bootstrap& settings, events::log_destination_store& store, events::log_channel& channel,
                 ports::discord_gateway& discord);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    auto set(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto level(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto off(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto show(const dpp::slashcommand_t& event) -> dpp::task<void>;

    command_info info_;
    const config::bootstrap* settings_;
    events::log_destination_store* store_;
    events::log_channel* channel_;
    ports::discord_gateway* discord_;
};

} // namespace latibot::commands
