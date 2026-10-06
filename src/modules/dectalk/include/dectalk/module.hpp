#pragma once

#include "core/modules/module.hpp"

#include <memory>

namespace latibot::modules {
class host;
}

namespace latibot::dectalk {

/// The dectalk module (README.md, docs/features/Speech.md): `/speak`, `/tts`,
/// `/chat`, custom voices, and the speech capability the language model
/// speaks through. Requires voice.
[[nodiscard]] auto make_module(modules::host& bot) -> std::unique_ptr<modules::module>;

} // namespace latibot::dectalk
