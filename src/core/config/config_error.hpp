#pragma once

#include <stdexcept>

namespace latibot::config {

/// A malformed config file or a missing secret. Always fatal at startup: the
/// bot should say what is wrong and stop, not run half-configured.
class config_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace latibot::config
