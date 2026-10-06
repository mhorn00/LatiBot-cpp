#pragma once

#include "core/ui/paginator.hpp"

#include <dpp/dpp.h>

#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::ui {

/// Which panel answers which view (docs/modules/Module_Plan_Final.md §4.6).
///
/// Every button, select menu and form carries its view's name in its
/// custom_id. A panel claims its view names here, and the shell hands each
/// interaction to whoever claimed its view. A name claimed twice stops
/// startup: two panels answering one button is a bug that would otherwise
/// show only as whichever of them was asked first.
///
/// A panel is anything with the panels' shape, `on_component` and
/// optionally `on_form`, each returning whether it handled the view
/// (docs/features/Commands_and_Panels.md §4). There is no base class to
/// derive from. Filled while the bot starts, read-only afterwards.
class panel_routes {
public:
    using component_handler = std::function<bool(const dpp::interaction_create_t&, const page_state&, const std::string&)>;
    using form_handler = std::function<bool(const dpp::form_submit_t&, const page_state&)>;

    struct handlers {
        component_handler on_component;
        /// Empty for a panel with no forms.
        form_handler on_form;
    };

    /// Claims `views` for `panel`, which must outlive the routes. `owner`
    /// names it when a view is claimed twice.
    template <typename Panel>
    auto add(Panel& panel, std::initializer_list<std::string_view> views, std::string_view owner) -> void {
        handlers routed{.on_component = [&panel](const dpp::interaction_create_t& event, const page_state& state,
                                                 const std::string& chosen) { return panel.on_component(event, state, chosen); },
                        .on_form = {}};
        if constexpr (requires(const dpp::form_submit_t& event, const page_state& state) { panel.on_form(event, state); }) {
            routed.on_form = [&panel](const dpp::form_submit_t& event, const page_state& state) { return panel.on_form(event, state); };
        }
        add(views, std::move(routed), owner);
    }

    /// Claims `views` for handlers that are not a panel object, such as a
    /// free function. Throws std::logic_error when one is already claimed,
    /// naming both owners, and claims none of them then.
    auto add(std::initializer_list<std::string_view> views, handlers routed, std::string_view owner) -> void;

    /// Hands a component to the panel that claimed its view. False when no
    /// panel claimed it, or the panel did not handle it.
    [[nodiscard]] auto on_component(const dpp::interaction_create_t& event, const page_state& state, const std::string& chosen) const
        -> bool;

    /// The same for a form; false too when the panel has no forms.
    [[nodiscard]] auto on_form(const dpp::form_submit_t& event, const page_state& state) const -> bool;

    [[nodiscard]] auto claimed(std::string_view view) const -> bool;

    /// Every claimed view, in name order, for a module's README check.
    [[nodiscard]] auto views() const -> std::vector<std::string_view>;

private:
    struct route {
        /// Shared by every view of one panel.
        std::size_t panel = 0;
        std::string owner;
    };

    std::vector<handlers> panels_;
    std::map<std::string, route, std::less<>> routes_;
};

} // namespace latibot::ui
