#pragma once

#include "core/ports/result.hpp"

#include <dpp/coro/task.h>
#include <dpp/snowflake.h>

#include <optional>
#include <string>

namespace latibot::capabilities {

/// Changing a nickname on the record, as `/nickname` does: who asked for it
/// is kept in the nickname history (src/modules/nicknames/docs/Nicknames.md
/// §2.2). Offered by the nicknames module, for the language model's
/// `set_nickname` tool; without it, the change is made but not recorded.
class nickname_changes {
public:
    virtual ~nickname_changes() = default;

    nickname_changes() = default;
    nickname_changes(const nickname_changes&) = delete;
    auto operator=(const nickname_changes&) -> nickname_changes& = delete;

    /// Sets `user_id`'s nickname, or clears it, recorded as asked for by
    /// `asked_by`. A failure is Discord's, explained.
    virtual auto change(dpp::snowflake guild_id, dpp::snowflake user_id, std::optional<std::string> nickname, dpp::snowflake asked_by)
        -> dpp::task<ports::result<void>> = 0;
};

} // namespace latibot::capabilities
