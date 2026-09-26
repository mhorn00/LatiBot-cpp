#pragma once

#include "core/commands/registry.hpp"
#include "core/events/bot_allowlist.hpp"

#include <dpp/appcommand.h>

#include <cstddef>
#include <span>
#include <string>

namespace latibot::commands {

/// The body of `/bots list`, as its own function so it can be tested without
/// an interaction. `known` names the bots that are still in the guild; ones
/// that are not are listed by id, because a bot that left is still listed and
/// saying so beats showing a bare number with no explanation.
[[nodiscard]] auto render_allowed_bots(std::span<const std::pair<dpp::snowflake, std::string>> known) -> std::string;

/// `/bots allow | deny | list` (plan §5.4, §14.4).
///
/// Which other bots LatiBot may hear at all. Answering them is a separate
/// decision per feature: for triggers it is `/trigger add bots:true`.
class bots_command final : public command {
public:
    explicit bots_command(events::bot_allowlist& allowlist);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    auto allow(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto deny(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto list(const dpp::slashcommand_t& event) -> dpp::task<void>;

    command_info info_;
    events::bot_allowlist* allowlist_;
};

} // namespace latibot::commands
