#pragma once

#include "core/commands/registry.hpp"
#include "core/events/triggers.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace latibot::commands {

/// How many triggers one page of `/trigger list` shows.
inline constexpr std::size_t triggers_per_page = 8;

/// The view name the paginator encodes into the ◀ / ▶ buttons.
inline constexpr std::string_view trigger_list_view = "triggers";

/// Parses the responses field of `/trigger add` and `/trigger edit`.
///
/// One response per line, with an optional leading weight: "3 | nice" is
/// three times as likely as "nice". Blank lines are skipped, and a line
/// without a weight gets 1.
///
/// Free text rather than separate options because Discord caps a command at
/// 25 options, and a trigger can have any number of responses.
[[nodiscard]] std::vector<events::weighted_response> parse_responses(std::string_view text);

/// The inverse, for showing an existing trigger back to whoever is editing it.
[[nodiscard]] std::string format_responses(std::span<const events::weighted_response> responses);

/// One line of `/trigger list`.
[[nodiscard]] std::string describe(const events::trigger& entry);

/// One page of the list, with its ◀ / ▶ row.
///
/// Shared by the command and the button handler, so a page reached by paging
/// is built the same way as the first one.
[[nodiscard]] dpp::message render_trigger_list(const events::trigger_store& store, dpp::snowflake guild_id, int page);

/// `/trigger add | edit | remove | list` (plan v4 §11).
class trigger_command final : public command {
public:
    explicit trigger_command(events::trigger_store& store);

    [[nodiscard]] const command_info& info() const override { return info_; }
    [[nodiscard]] dpp::slashcommand build(const std::string& name, dpp::snowflake application_id) const override;
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;

private:
    dpp::task<void> add(const dpp::slashcommand_t& event);
    dpp::task<void> edit(const dpp::slashcommand_t& event);
    dpp::task<void> remove(const dpp::slashcommand_t& event);
    dpp::task<void> list(const dpp::slashcommand_t& event, int page);

    command_info info_;
    events::trigger_store* store_;
};

} // namespace latibot::commands
