#pragma once

#include "core/modules/module.hpp"

#include <memory>

namespace latibot::modules {
class host;
}

namespace latibot::triggers {

/// The triggers module (README.md, src/modules/triggers/docs/Triggers.md): `/trigger`,
/// its panel, and the replies to messages that match.
[[nodiscard]] auto make_module(modules::host& bot) -> std::unique_ptr<modules::module>;

} // namespace latibot::triggers
