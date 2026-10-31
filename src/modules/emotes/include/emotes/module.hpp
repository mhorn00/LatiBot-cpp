#pragma once

#include "core/modules/module.hpp"

#include <memory>

namespace latibot::modules {
class host;
}

namespace latibot::emotes {

/// The emotes module (README.md, src/modules/emotes/docs/Steal_Emote.md):
/// Steal this emote, a message's Apps menu command.
[[nodiscard]] auto make_module(modules::host& bot) -> std::unique_ptr<modules::module>;

} // namespace latibot::emotes
