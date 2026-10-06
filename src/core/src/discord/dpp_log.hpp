#pragma once

#include "core/util/log.hpp"

#include <dpp/misc-enum.h>

namespace latibot::discord {

/// DPP's severity as ours, for sending its log lines through our logger.
/// Errors and criticals both become errors: they matter enough to surface the
/// same way.
[[nodiscard]] auto log_level_of(dpp::loglevel level) -> util::log_level;

} // namespace latibot::discord
