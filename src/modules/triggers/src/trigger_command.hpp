#pragma once

#include "core/capabilities/model_replies.hpp"
#include "core/commands/registry.hpp"
#include "core/ui/paginator.hpp"
#include "triggers.hpp"

#include <dpp/appcommand.h>
#include <dpp/dispatcher.h>

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

/// One line of `/trigger list`. A model trigger shows its chance, who
/// answers it of `personas`, and its prompt.
[[nodiscard]] auto describe(const events::trigger& entry, std::span<const capabilities::persona_name> personas = {}) -> std::string;

/// What `add` and `edit` answer: the line, then every response or the
/// prompt, in full, cut only at Discord's 2,000 characters.
[[nodiscard]] auto describe_in_full(std::string_view heading, const events::trigger& entry,
                                    std::span<const capabilities::persona_name> personas = {}) -> std::string;

/// One page of the list, with its ◀ / ▶ row.
///
/// Shared by the command and the button handler, so a page reached by paging
/// is built the same way as the first one. `model` names the personas, and
/// is null without the language model.
[[nodiscard]] auto render_trigger_list(const events::trigger_store& store, dpp::snowflake guild_id, int page,
                                       const capabilities::model_replies* model = nullptr) -> dpp::message;

// --------------------------------------------------------------------------
// The panel (src/modules/triggers/docs/Triggers.md §2.5)
//
// The interactions are distinguished by the view name in the custom_id, which
// the paginator already encodes and decodes. There is no generic "panel" base:
// what the panels turned out to share is a shape, a class with `on_component`
// and `on_form` that routes its own views
// (src/core/docs/Commands_and_Panels.md §4).
// --------------------------------------------------------------------------

inline constexpr std::string_view trigger_panel_view = "trigpanel";
inline constexpr std::string_view trigger_pick_view = "trigpick";
inline constexpr std::string_view trigger_edit_view = "trigedit";
inline constexpr std::string_view trigger_delete_view = "trigdel";
inline constexpr std::string_view trigger_confirm_view = "trigyes";
inline constexpr std::string_view trigger_add_view = "trigadd";
inline constexpr std::string_view trigger_add_model_view = "trigaddmodel";
inline constexpr std::string_view trigger_form_view = "trigform";
/// The form `/trigger edit` opens, which answers privately rather than
/// updating a panel it does not have.
inline constexpr std::string_view trigger_command_form_view = "trigcmdform";
inline constexpr std::string_view trigger_toggle_view = "trigonoff";
inline constexpr std::string_view trigger_bots_view = "trigbots";
inline constexpr std::string_view trigger_silent_view = "trigsilent";
inline constexpr std::string_view trigger_previews_view = "trigprev";
inline constexpr std::string_view trigger_reply_view = "trigreply";

/// What one of the panel's on/off buttons changes on a trigger, returning the
/// change as the log names it: "disabled", "set to reply silently".
using trigger_toggle = std::string_view (*)(events::trigger&);

/// The change a toggle view makes, or null for a view that is not a toggle.
[[nodiscard]] auto toggle_for(std::string_view view) -> trigger_toggle;

/// What the edit and add modals collect. Everything is free text, because a
/// modal has no other kind of input. A model trigger's form has `prompt`
/// and `chance` where another has `responses`.
struct form_fields {
    std::string pattern;
    std::string responses;
    std::string mode;
    std::string cooldown;
    std::string prompt;
    std::string chance;
    std::string question;

    /// Which form it was, and so which fields it had.
    events::trigger_kind kind = events::trigger_kind::responses;
};

/// What applying the modal came to.
struct form_outcome {
    /// Why nothing was applied, when the trigger would be unusable.
    std::optional<std::string> refused;

    /// The optional fields that could not be read and were left as they
    /// were, said in a line for the panel. Empty when everything read.
    std::string note;
};

/// Applies the modal's fields to a trigger.
///
/// Nothing is applied when the result would be unusable. Unreadable optional
/// fields are left alone rather than reset, and said so in the note:
/// someone typing "thirty" into the cooldown box should neither lose the
/// cooldown they had nor think it changed.
[[nodiscard]] auto apply_form(events::trigger& entry, const form_fields& fields) -> form_outcome;

/// The panel, at `page`.
///
/// `selected` is the trigger the select menu is pointing at, 0 for none, and
/// `confirming_delete` swaps the Edit/Delete row for a confirmation. Both ride
/// in the buttons' custom_ids, so the panel needs no server-side state and
/// keeps working after a restart. A selected trigger on another page is
/// followed there, and one that no longer exists is not selected. `note` is
/// a line under the list saying what the last action came to.
[[nodiscard]] auto render_trigger_panel(const events::trigger_store& store, dpp::snowflake guild_id, int page, std::int64_t selected = 0,
                                        bool confirming_delete = false, std::string_view note = {},
                                        const capabilities::model_replies* model = nullptr) -> dpp::message;

/// The add or edit modal, pre-filled when editing, with its kind's fields.
/// `entry` is null for add, and then `kind` says which kind to add. `view`
/// is the panel's form, or the command's.
[[nodiscard]] auto trigger_form(int page, const events::trigger* entry, events::trigger_kind kind = events::trigger_kind::responses,
                                std::string_view view = trigger_form_view) -> dpp::interaction_modal_response;

/// The panel's buttons, menus and forms, and the list's paging.
///
/// Routes its own, as the voice lab does, rather than living in the shell:
/// that keeps it where its tests can reach it.
class trigger_panel {
public:
    /// `model` is null without the language model, and then the panel offers
    /// no model triggers.
    explicit trigger_panel(events::trigger_store& store, const capabilities::model_replies* model = nullptr);

    /// False when the view is not one of the panel's.
    auto on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool;

    /// False when the form is not the panel's.
    auto on_form(const dpp::form_submit_t& event, const ui::page_state& state) -> bool;

private:
    auto toggle(const dpp::interaction_create_t& event, std::int64_t id, trigger_toggle change) -> void;

    /// The panel as it now is, with `note` under the list.
    [[nodiscard]] auto render(dpp::snowflake guild, int page, std::int64_t selected = 0, bool confirming_delete = false,
                              std::string_view note = {}) const -> dpp::message;

    events::trigger_store* store_;
    const capabilities::model_replies* model_;
};

/// `/trigger add | edit | remove | list | panel`
/// (src/modules/triggers/docs/Triggers.md §2.4). With `model`, the language
/// model's, `add` and `edit` take a prompt for it, a chance and a persona
/// (§2.6).
class trigger_command final : public command {
public:
    explicit trigger_command(events::trigger_store& store, const capabilities::model_replies* model = nullptr);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;
    auto autocomplete(const dpp::autocomplete_t& event) const -> void override;

private:
    auto add(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto edit(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto remove(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto list(const dpp::slashcommand_t& event, int page) -> dpp::task<void>;
    auto panel(const dpp::slashcommand_t& event) -> dpp::task<void>;

    /// Sets who answers a model trigger from the `persona` option, if given.
    /// Why not, when it names nobody.
    [[nodiscard]] auto apply_persona(const dpp::slashcommand_t& event, events::trigger& entry) const -> std::optional<std::string>;

    /// The personas, to name who answers; none without the model.
    [[nodiscard]] auto personas(dpp::snowflake guild) const -> std::vector<capabilities::persona_name>;

    command_info info_;
    events::trigger_store* store_;
    const capabilities::model_replies* model_;
};

} // namespace latibot::commands
