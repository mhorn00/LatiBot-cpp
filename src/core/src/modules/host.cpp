#include "core/modules/host.hpp"

#include "core/util/log.hpp"

namespace latibot::modules {

auto report_failure(std::string_view what, const std::exception* error) -> void {
    if (error != nullptr) {
        util::log().error("{} failed: {}", what, error->what());
    } else {
        util::log().error("{} failed with an unknown exception", what);
    }
}

} // namespace latibot::modules
