#pragma once

#include <stdexcept>
#include <string>

namespace latibot::db {

/// Thrown by every database operation that fails.
///
/// SQLite reports failures as result codes; carrying the code lets callers
/// distinguish, say, a constraint violation from a corrupt file, while the
/// message stays useful in a log.
class db_error : public std::runtime_error {
public:
    db_error(int code, const std::string& message);

    /// The SQLite result code, e.g. SQLITE_CONSTRAINT.
    [[nodiscard]] auto code() const noexcept -> int { return code_; }

private:
    int code_;
};

} // namespace latibot::db
