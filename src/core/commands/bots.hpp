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
[[nodiscard]] std::string render_allowed_bots(std::span<const std::pair<dpp::snowflake, std::string>> known);

/// `/bots allow | deny | list` (plan v4 §5.4, §14.4).
///
/// Which other bots LatiBot may hear at all. Answering them is a separate
/// decision per feature: for triggers it is `/trigger add bots:true`.
class bots_command final : public command {
public:
    explicit bots_command(events::bot_allowlist& allowlist);

    [[nodiscard]] const command_info& info() const override { return info_; }
    [[nodiscard]] dpp::slashcommand build(const std::string& name, dpp::snowflake application_id) const override;
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;

private:
    dpp::task<void> allow(const dpp::slashcommand_t& event);
    dpp::task<void> deny(const dpp::slashcommand_t& event);
    dpp::task<void> list(const dpp::slashcommand_t& event);

    command_info info_;
    events::bot_allowlist* allowlist_;
};

} // namespace latibot::commands
