#pragma once

#include "core/modules/module.hpp"

#include <memory>

namespace latibot::modules {
class host;
}

namespace latibot::voice {

/// The voice module (README.md, docs/features/Voice_Channels.md): `/join`,
/// `/leave`, `/voice`, the mixer and the sessions. Built when dectalk or music
/// is, which require it; it offers them `voice::services`.
[[nodiscard]] auto make_module(modules::host& bot) -> std::unique_ptr<modules::module>;

} // namespace latibot::voice
