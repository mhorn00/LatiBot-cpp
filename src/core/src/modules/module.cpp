#include "core/modules/module.hpp"

#include "core/db/schema_versions.hpp"
#include "core/modules/capability_registry.hpp"
#include "core/modules/host.hpp"

namespace latibot::modules {

auto start_modules(const module_factory& make, host& bot, capability_registry& offered) -> module_list {
    module_list modules = make(bot);
    // In list order, so a module's tables come after those of the modules it
    // requires, which its may refer to.
    for (const auto& each : modules) {
        if (!each->schema().empty()) db::apply_schema(bot.database(), {.module = each->name(), .steps = each->schema()});
    }
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

} // namespace latibot::modules
