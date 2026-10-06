#pragma once

#include "core/modules/module.hpp"

#include <memory>

namespace latibot::modules {
class host;
}

namespace latibot::midnight {

/// The midnight module (README.md, docs/features/Midnight.md): `/midnight`,
/// and a timer that posts each entry once per local day.
[[nodiscard]] auto make_module(modules::host& bot) -> std::unique_ptr<modules::module>;

} // namespace latibot::midnight
