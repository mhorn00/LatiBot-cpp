#include "core/module/capability_registry.hpp"

#include <format>
#include <stdexcept>
#include <utility>

namespace latibot::module {

auto capability_registry::add(const std::type_info& wanted, void* implementation, std::string_view by) -> void {
    const auto [where, added] = offered_.try_emplace(wanted, offering{.implementation = implementation, .by = std::string(by)});
    if (!added) {
        throw std::logic_error(std::format("{} and {} both offer the capability {}", where->second.by, by, wanted.name()));
    }
}

auto capability_registry::lookup(const std::type_info& wanted) const -> void* {
    const auto found = offered_.find(wanted);
    return found == offered_.end() ? nullptr : found->second.implementation;
}

auto capability_registry::owner(const std::type_info& wanted) const -> std::string_view {
    const auto found = offered_.find(wanted);
    return found == offered_.end() ? std::string_view{} : std::string_view{found->second.by};
}

} // namespace latibot::module
