#pragma once

#include "core/commands/registry.hpp"
#include "core/events/url_rules.hpp"

#include <dpp/appcommand.h>

#include <cstddef>
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

// The panel (plan §9.5). As with the trigger panel, the view name in the
// custom_id says what a button does and the argument carries the domain, so
// the panel keeps no state and survives a restart.
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
[[nodiscard]] std::string describe_state(bool enabled);

/// Turns URL replacement on or off in a guild and logs who did it, from
/// where. False when it was already that way.
bool switch_url_replacement(events::url_rule_store& store, dpp::snowflake guild_id, bool enabled, const user_label& who,
                            std::string_view from);

/// The answer to `/urlrepl enable` or `/urlrepl disable`.
[[nodiscard]] std::string render_switch(bool changed, bool enabled, std::size_t rule_count);

/// "fxtwitter.com/en, vxtwitter.com": the mirrors as they are typed.
[[nodiscard]] std::string describe_mirrors(std::span<const events::mirror> mirrors);

/// One line of the list: "**x.com** → fxtwitter.com/en, vxtwitter.com".
[[nodiscard]] std::string describe(const events::url_rule& rule);

/// A rule from what somebody typed, or why it cannot be one.
///
/// Mirrors may be one per line, which is what the modal gives, or separated
/// by commas or spaces, which is what fits in a slash command. Their order is
/// the order they are tried in. A mirror that is the site itself would
/// "replace" a link with the same link, so it is refused.
[[nodiscard]] std::variant<events::url_rule, std::string> build_rule(std::string_view domain, std::string_view mirrors);

/// The dry run behind `/urlrepl test`: what would be posted for `content`,
/// and what happened to every link in it (plan §9.5). It works while
/// replacement is off, so rules can be tried before anyone sees them, and
/// says so when it is.
[[nodiscard]] std::string render_test(std::string_view content, std::span<const events::url_rule> rules, bool opted_out, bool enabled);

/// One page of `/urlrepl list`.
[[nodiscard]] dpp::message render_url_rule_list(const events::url_rule_store& store, dpp::snowflake guild_id, int page);

/// The panel at `page`. `selected` is the domain the select menu points at,
/// empty for none; `confirming_delete` swaps Edit/Delete for a confirmation.
[[nodiscard]] dpp::message render_url_panel(const events::url_rule_store& store, dpp::snowflake guild_id, int page,
                                            std::string_view selected = {}, bool confirming_delete = false);

/// The add or edit modal. `rule` is null for add.
[[nodiscard]] dpp::interaction_modal_response url_rule_form(int page, const events::url_rule* rule);

/// `/urlrepl enable | disable | list | set | remove | test | panel` (plan
/// §9.5).
class urlrepl_command final : public command {
public:
    explicit urlrepl_command(events::url_rule_store& store);

    [[nodiscard]] const command_info& info() const override { return info_; }
    [[nodiscard]] dpp::slashcommand build(const std::string& name, dpp::snowflake application_id) const override;
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;

    /// Domains that already have a rule, so nobody has to remember how they
    /// spelled one.
    void autocomplete(const dpp::autocomplete_t& event) const override;

private:
    dpp::task<void> turn(const dpp::slashcommand_t& event, bool enabled);
    dpp::task<void> set(const dpp::slashcommand_t& event);
    dpp::task<void> remove(const dpp::slashcommand_t& event);
    dpp::task<void> test(const dpp::slashcommand_t& event);

    command_info info_;
    events::url_rule_store* store_;
};

/// `/urltoggle [user]`: leave somebody's links alone, or stop doing so.
///
/// Anyone can toggle themselves. Toggling somebody else needs Manage Server,
/// the permission that manages the rules, which the Java `/toggle` did not
/// ask for at all. The choice is kept, per guild; the Java list was in memory
/// and gone at the next restart.
class urltoggle_command final : public command {
public:
    explicit urltoggle_command(events::url_rule_store& store);

    [[nodiscard]] const command_info& info() const override { return info_; }
    [[nodiscard]] dpp::slashcommand build(const std::string& name, dpp::snowflake application_id) const override;
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;

private:
    command_info info_;
    events::url_rule_store* store_;
};

} // namespace latibot::commands
