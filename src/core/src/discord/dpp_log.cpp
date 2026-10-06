#include "core/discord/dpp_log.hpp"

namespace latibot::discord {

auto log_level_of(dpp::loglevel level) -> util::log_level {
    switch (level) {
    case dpp::ll_trace:
        return util::log_level::trace;
    case dpp::ll_debug:
        return util::log_level::debug;
    case dpp::ll_info:
        return util::log_level::info;
    case dpp::ll_warning:
        return util::log_level::warn;
    default:
        return util::log_level::error;
    }
}

} // namespace latibot::discord
