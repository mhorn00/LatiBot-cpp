#include "core/events/midnight_module.hpp"
#include "core/modules/module.hpp"

namespace latibot::modules {

// Every module this build includes, in dependency order: each one's factory,
// called with the host (docs/modules/Module_Plan_Final.md §4.8). CMake will
// write this file from the LATIBOT_WITH_* switches; until then it is written
// by hand.
auto enabled_modules(host& bot) -> module_list {
    module_list modules;
    modules.push_back(events::make_midnight_module(bot));
    return modules;
}

} // namespace latibot::modules
