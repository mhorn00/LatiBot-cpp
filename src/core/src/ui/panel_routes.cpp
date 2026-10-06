#include "core/ui/panel_routes.hpp"

#include <format>
#include <stdexcept>
#include <utility>

namespace latibot::ui {

auto panel_routes::add(std::initializer_list<std::string_view> views, handlers routed, std::string_view owner) -> void {
    // Every name is checked before any is claimed, so a refused panel leaves
    // nothing half-registered behind it.
    for (const std::string_view view : views) {
        if (const auto taken = routes_.find(view); taken != routes_.end()) {
            throw std::logic_error(std::format("the panel view \"{}\" is claimed by both {} and {}", view, taken->second.owner, owner));
        }
    }
    const std::size_t index = panels_.size();
    panels_.push_back(std::move(routed));
    for (const std::string_view view : views) {
        routes_.emplace(std::string(view), route{.panel = index, .owner = std::string(owner)});
    }
}

auto panel_routes::on_component(const dpp::interaction_create_t& event, const page_state& state, const std::string& chosen) const -> bool {
    const auto found = routes_.find(state.view);
    if (found == routes_.end()) return false;
    return panels_[found->second.panel].on_component(event, state, chosen);
}

auto panel_routes::on_form(const dpp::form_submit_t& event, const page_state& state) const -> bool {
    const auto found = routes_.find(state.view);
    if (found == routes_.end()) return false;
    const handlers& panel = panels_[found->second.panel];
    return panel.on_form && panel.on_form(event, state);
}

auto panel_routes::claimed(std::string_view view) const -> bool {
    return routes_.contains(view);
}

auto panel_routes::views() const -> std::vector<std::string_view> {
    std::vector<std::string_view> names;
    names.reserve(routes_.size());
    for (const auto& [view, owner] : routes_) {
        names.emplace_back(view);
    }
    return names;
}

} // namespace latibot::ui
