#include "core/module/module.hpp"

#include "core/module/capability_registry.hpp"
#include "core/module/host.hpp"

namespace latibot::module {

auto start_modules(const module_factory& make, host& bot, capability_registry& offered) -> module_list {
    module_list modules = make(bot);
    // Every offer before any start, so a module finds what another offers
    // whichever of them comes first in the list.
    for (const auto& each : modules) {
        each->offer(offered);
    }
    for (const auto& each : modules) {
        each->start(bot);
    }
    return modules;
}

} // namespace latibot::module
