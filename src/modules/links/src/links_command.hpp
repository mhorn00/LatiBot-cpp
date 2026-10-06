#pragma once

#include "core/commands/registry.hpp"
#include "core/ui/paginator.hpp"
#include "links/url_rules.hpp"

#include <dpp/appcommand.h>
#include <dpp/dispatcher.h>
#include <dpp/permissions.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace latibot::commands {

/// Rules per page of the list and the panel. The panel's select menu, its
/// Edit/Delete row and its footer take three of a message's five rows, and
/// five rules keep a page short enough to read at a glance.
inline constexpr std::size_t url_rules_per_page = 5;

/// The most mirrors one rule takes. More than this is a list nobody will
/// wait for: each gets two tries of six seconds.
inline constexpr std::size_t max_mirrors_per_rule = 8;

inline constexpr std::string_view url_list_view = "urllist";

// The panel (docs/features/Url_Replacement.md §3.5). As with the trigger
// panel, the view name in the custom_id says what a button does and the
// argument carries the domain, so the panel keeps no state and survives a
// restart.
inline constexpr std::string_view url_panel_view = "urlpanel";
inline constexpr std::string_view url_pick_view = "urlpick";
inline constexpr std::string_view url_edit_view = "urledit";
inline constexpr std::string_view url_delete_view = "urldel";
inline constexpr std::string_view url_confirm_view = "urlyes";
inline constexpr std::string_view url_add_view = "urladd";
inline constexpr std::string_view url_form_view = "urlform";

/// The panel's on/off button. Its argument is the state it asks for, "on" or
/// "off", so pressing it twice on a stale panel does not flip it back.
inline constexpr std::string_view url_switch_view = "urlswitch";

/// "Link replacement is **on** in this server.", or off: the line the list
/// and the panel open with.
[[nodiscard]] auto describe_state(bool enabled) -> std::string;

/// Turns URL replacement on or off in a guild and logs who did it, from
/// where. False when it was already that way.
auto switch_url_replacement(events::url_rule_store& store, dpp::snowflake guild_id, bool enabled, const user_label& who,
                            std::string_view from) -> bool;

/// The answer to `/links enable` or `/links disable`.
[[nodiscard]] auto render_switch(bool changed, bool enabled, std::size_t rule_count) -> std::string;

/// "fxtwitter.com/en, vxtwitter.com": the mirrors as they are typed.
[[nodiscard]] auto describe_mirrors(std::span<const events::mirror> mirrors) -> std::string;

/// One line of the list: "**x.com** → fxtwitter.com/en, vxtwitter.com".
[[nodiscard]] auto describe(const events::url_rule& rule) -> std::string;

/// A rule from what somebody typed, or why it cannot be one.
///
/// Mirrors may be one per line, which is what the modal gives, or separated
/// by commas or spaces, which is what fits in a slash command. Their order is
/// the order they are tried in. A mirror that is the site itself would
/// "replace" a link with the same link, so it is refused.
[[nodiscard]] auto build_rule(std::string_view domain, std::string_view mirrors) -> std::variant<events::url_rule, std::string>;

/// The dry run behind `/links test`: what would be posted for `content`,
/// and what happened to every link in it
/// (docs/features/Url_Replacement.md §2.6). It works while replacement is
/// off, so rules can be tried before anyone sees them, and says so when it
/// is.
[[nodiscard]] auto render_test(std::string_view content, std::span<const events::url_rule> rules, bool opted_out, bool enabled)
    -> std::string;

/// One page of `/links list`.
[[nodiscard]] auto render_url_rule_list(const events::url_rule_store& store, dpp::snowflake guild_id, int page) -> dpp::message;

/// The panel at `page`. `selected` is the domain the select menu points at,
/// empty for none; `confirming_delete` swaps Edit/Delete for a confirmation.
/// `note` is a line under the rules saying what the last action came to.
[[nodiscard]] auto render_url_panel(const events::url_rule_store& store, dpp::snowflake guild_id, int page, std::string_view selected = {},
                                    bool confirming_delete = false, std::string_view note = {}) -> dpp::message;

/// The add or edit modal. `rule` is null for add.
[[nodiscard]] auto url_rule_form(int page, const events::url_rule* rule) -> dpp::interaction_modal_response;

/// Why the panel's form may not save `rule`, or nothing when it may.
/// `previous` is the site the form was opened for, empty when adding.
///
/// Saving over another site's rule would lose it without a word: adding a
/// site that already has one, or renaming a rule onto one.
[[nodiscard]] auto url_form_refusal(const events::url_rule_store& store, dpp::snowflake guild_id, std::string_view previous,
                                    const events::url_rule& rule) -> std::optional<std::string>;

/// The panel's buttons, menus and forms, and the list's paging.
///
/// Routes its own, as the voice lab does, rather than living in the shell:
/// that keeps it where its tests can reach it.
class url_panel {
public:
    explicit url_panel(events::url_rule_store& store);

    /// False when the view is not one of the panel's.
    auto on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool;

    /// False when the form is not the panel's.
    auto on_form(const dpp::form_submit_t& event, const ui::page_state& state) -> bool;

private:
    events::url_rule_store* store_;
};

/// `/links enable | disable | list | set | remove | test | panel`
/// (docs/features/Url_Replacement.md §2.6).
class links_command final : public command {
public:
    explicit links_command(events::url_rule_store& store);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

    /// Domains that already have a rule, so nobody has to remember how they
    /// spelled one.
    auto autocomplete(const dpp::autocomplete_t& event) const -> void override;

private:
    auto turn(const dpp::slashcommand_t& event, bool enabled) -> dpp::task<void>;
    auto set(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto remove(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto test(const dpp::slashcommand_t& event) -> dpp::task<void>;

    command_info info_;
    events::url_rule_store* store_;
};

/// Why `invoker` may not change whether `target`'s links are replaced, or
/// nothing when they may.
///
/// Anyone can toggle themselves. Toggling somebody else needs Manage Server,
/// the permission that manages the rules, which the Java `/toggle` did not
/// ask for at all. Discord cannot check this for us: everyone may run
/// `/urltoggle`, and the difference is in an option
/// (docs/features/Commands_and_Panels.md §2.1).
[[nodiscard]] auto urltoggle_refusal(dpp::snowflake invoker, dpp::snowflake target, dpp::permission permissions)
    -> std::optional<std::string>;

/// `/urltoggle [user]`: leave somebody's links alone, or stop doing so.
///
/// Who may do it for whom is `urltoggle_refusal`. The choice is kept, per
/// guild; the Java list was in memory and gone at the next restart.
class urltoggle_command final : public command {
public:
    explicit urltoggle_command(events::url_rule_store& store);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    command_info info_;
    events::url_rule_store* store_;
};

} // namespace latibot::commands
