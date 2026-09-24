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
[[nodiscard]] std::string describe(const events::midnight_entry& entry);

/// The body of `/midnight list`, as its own function so it can be tested
/// without an interaction.
[[nodiscard]] std::string render_midnight_list(std::span<const events::midnight_entry> entries);

/// `/midnight list | add | edit | remove | toggle` (plan v4 §10).
class midnight_command final : public command {
public:
    midnight_command(events::midnight_store& store, ports::clock& clock);

    [[nodiscard]] const command_info& info() const override { return info_; }
    [[nodiscard]] dpp::slashcommand build(const std::string& name, dpp::snowflake application_id) const override;
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;
    void autocomplete(const dpp::autocomplete_t& event) const override;

private:
    dpp::task<void> add(const dpp::slashcommand_t& event);
    dpp::task<void> edit(const dpp::slashcommand_t& event);
    dpp::task<void> remove(const dpp::slashcommand_t& event);
    dpp::task<void> toggle(const dpp::slashcommand_t& event);
    dpp::task<void> list(const dpp::slashcommand_t& event);

    command_info info_;
    events::midnight_store* store_;
    ports::clock* clock_;
};

} // namespace latibot::commands
