#include "core/db/error.hpp"

namespace latibot::db {

// std::runtime_error only takes its message by const reference, so there is
// nothing to move here.
db_error::db_error(int code, const std::string& message)
    : std::runtime_error(message), code_(code) {}

} // namespace latibot::db
