#pragma once

#include "core/commands/registry.hpp"
#include "core/events/midnight.hpp"

#include <dpp/appcommand.h>

#include <cstddef>
#include <span>
#include <string>

namespace latibot::ports {
class clock;
}

namespace latibot::commands {

/// Discord's ceiling on autocomplete choices.
inline constexpr std::size_t autocomplete_limit = 25;

/// One line of `/midnight list`.
[[nodiscard]] auto describe(const events::midnight_entry& entry) -> std::string;

/// The body of `/midnight list`, as its own function so it can be tested
/// without an interaction.
[[nodiscard]] auto render_midnight_list(std::span<const events::midnight_entry> entries) -> std::string;

/// `/midnight list | add | edit | remove | toggle` (plan §10).
class midnight_command final : public command {
public:
    midnight_command(events::midnight_store& store, ports::clock& clock);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;
    auto autocomplete(const dpp::autocomplete_t& event) const -> void override;

private:
    auto add(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto edit(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto remove(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto toggle(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto list(const dpp::slashcommand_t& event) -> dpp::task<void>;

    command_info info_;
    events::midnight_store* store_;
    ports::clock* clock_;
};

} // namespace latibot::commands
