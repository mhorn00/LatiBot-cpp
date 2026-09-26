#pragma once

#include "core/commands/registry.hpp"
#include "core/events/triggers.hpp"

#include <dpp/appcommand.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
[[nodiscard]] auto parse_responses(std::string_view text) -> std::vector<events::weighted_response>;

/// The inverse, for showing an existing trigger back to whoever is editing it.
[[nodiscard]] auto format_responses(std::span<const events::weighted_response> responses) -> std::string;

/// One line of `/trigger list`.
[[nodiscard]] auto describe(const events::trigger& entry) -> std::string;

/// One page of the list, with its ◀ / ▶ row.
///
/// Shared by the command and the button handler, so a page reached by paging
/// is built the same way as the first one.
[[nodiscard]] auto render_trigger_list(const events::trigger_store& store, dpp::snowflake guild_id, int page) -> dpp::message;

// --------------------------------------------------------------------------
// The panel (plan §11)
//
// The interactions are distinguished by the view name in the custom_id, which
// the paginator already encodes and decodes. There is no generic "panel"
// abstraction yet on purpose: `/urlrepl`'s panel is the second and
// `/llm settings` will be the third, and what they have in common is better
// read off three examples than guessed at from two (plan §21.5).
// --------------------------------------------------------------------------

inline constexpr std::string_view trigger_panel_view = "trigpanel";
inline constexpr std::string_view trigger_pick_view = "trigpick";
inline constexpr std::string_view trigger_edit_view = "trigedit";
inline constexpr std::string_view trigger_delete_view = "trigdel";
inline constexpr std::string_view trigger_confirm_view = "trigyes";
inline constexpr std::string_view trigger_add_view = "trigadd";
inline constexpr std::string_view trigger_form_view = "trigform";
inline constexpr std::string_view trigger_toggle_view = "trigonoff";
inline constexpr std::string_view trigger_bots_view = "trigbots";
inline constexpr std::string_view trigger_silent_view = "trigsilent";
inline constexpr std::string_view trigger_previews_view = "trigprev";

/// What one of the panel's on/off buttons changes on a trigger, returning the
/// change as the log names it: "disabled", "set to reply silently".
using trigger_toggle = std::string_view (*)(events::trigger&);

/// The change a toggle view makes, or null for a view that is not a toggle.
[[nodiscard]] auto toggle_for(std::string_view view) -> trigger_toggle;

/// What the edit and add modals collect. Everything is free text, because a
/// modal has no other kind of input.
struct form_fields {
    std::string pattern;
    std::string responses;
    std::string mode;
    std::string cooldown;
};

/// Applies the modal's fields to a trigger.
///
/// Returns a message to show the user when the result would be unusable, and
/// nothing when `entry` was updated. Unparseable optional fields are left
/// alone rather than reset: someone typing "thirty" into the cooldown box
/// should not silently lose the cooldown they had.
[[nodiscard]] auto apply_form(events::trigger& entry, const form_fields& fields) -> std::optional<std::string>;

/// The panel, at `page`.
///
/// `selected` is the trigger the select menu is pointing at, 0 for none, and
/// `confirming_delete` swaps the Edit/Delete row for a confirmation. Both ride
/// in the buttons' custom_ids, so the panel needs no server-side state and
/// keeps working after a restart.
[[nodiscard]] auto render_trigger_panel(const events::trigger_store& store, dpp::snowflake guild_id, int page, std::int64_t selected = 0,
                                        bool confirming_delete = false) -> dpp::message;

/// The add or edit modal. `entry` is null for add.
[[nodiscard]] auto trigger_form(int page, const events::trigger* entry) -> dpp::interaction_modal_response;

/// `/trigger add | edit | remove | list | panel` (plan §11).
class trigger_command final : public command {
public:
    explicit trigger_command(events::trigger_store& store);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    auto add(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto edit(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto remove(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto list(const dpp::slashcommand_t& event, int page) -> dpp::task<void>;
    auto panel(const dpp::slashcommand_t& event) -> dpp::task<void>;

    command_info info_;
    events::trigger_store* store_;
};

} // namespace latibot::commands
