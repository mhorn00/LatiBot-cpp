#pragma once

#include "core/commands/registry.hpp"
#include "events/ignored_channels.hpp"
#include "events/log_channel.hpp"

#include <dpp/appcommand.h>

#include <span>
#include <string>
#include <utility>

namespace latibot::commands {

/// What `/channels ignore` says it did: "ok, i'll ignore everything in
/// <#5>", or that nothing changed.
[[nodiscard]] auto describe_ignored(dpp::snowflake channel, events::channel_scope scope, bool changed) -> std::string;

/// The body of `/channels list`, as its own function so it can be tested
/// without an interaction. `log_channel` is the bot's log channel when it is
/// in this server, which is always treated as `bots`; 0 otherwise.
[[nodiscard]] auto render_ignored_channels(std::span<const std::pair<dpp::snowflake, events::channel_scope>> listed,
                                           dpp::snowflake log_channel) -> std::string;

/// `/channels ignore | hear | list` (src/core/docs/Message_Pipeline.md §2.4).
///
/// Channels the bot pays no attention to, or whose bots it does not hear, or
/// where the language model stands back.
class channels_command final : public command {
public:
    channels_command(events::ignored_channels& ignored, const events::log_channel& log);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    auto ignore(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto hear(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto list(const dpp::slashcommand_t& event) -> dpp::task<void>;

    command_info info_;
    events::ignored_channels* ignored_;
    const events::log_channel* log_;
};

} // namespace latibot::commands
