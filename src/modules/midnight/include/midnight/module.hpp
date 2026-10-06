#pragma once

#include "core/db/schema_versions.hpp"
#include "core/modules/module.hpp"

#include <memory>

namespace latibot::modules {
class host;
}

namespace latibot::events {

/// The midnight module's schema: `midnight_messages`
/// (docs/modules/Module_Plan_Final.md §7.1).
[[nodiscard]] auto midnight_schema() noexcept -> db::module_schema;

/// The midnight module (docs/features/Midnight.md): `/midnight`, and a
/// timer that posts each entry once per local day.
[[nodiscard]] auto make_midnight_module(modules::host& bot) -> std::unique_ptr<modules::module>;

} // namespace latibot::events
