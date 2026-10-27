#pragma once

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::llm {

/// A reminder the model set with `set_reminder`
/// (src/modules/llm/docs/Language_Model.md §3.4): posted in its channel when it
/// is due, by whoever set it, LatiBot or a persona, mentioning whom it is
/// for.
struct reminder {
    std::int64_t id = 0;
    dpp::snowflake guild_id;
    dpp::snowflake channel_id;

    /// Whom it is for, mentioned when it is posted.
    dpp::snowflake user_id;

    /// Who asked for it.
    dpp::snowflake set_by;

    /// Who posts it: a persona, or 0 for LatiBot.
    std::int64_t persona_id = 0;

    std::chrono::sys_seconds due_at;

    /// What to say, with names already put back.
    std::string text;
};

/// How many reminders a server may have waiting, and how long one may be.
inline constexpr std::size_t reminders_per_guild = 100;
inline constexpr std::size_t reminder_length_limit = 1000;

/// The furthest ahead a reminder may be set.
inline constexpr std::chrono::days longest_reminder{366};

/// `llm_reminders`. Thread-safe, as the database is.
class reminder_store {
public:
    explicit reminder_store(db::database& db) : db_(&db) {}

    /// Returns the new id.
    auto add(const reminder& entry) -> std::int64_t;

    /// Those due by `now`, oldest first.
    [[nodiscard]] auto due(std::chrono::sys_seconds now) const -> std::vector<reminder>;

    [[nodiscard]] auto count(dpp::snowflake guild_id) const -> std::size_t;

    auto remove(std::int64_t id) -> bool;

private:
    db::database* db_;
};

} // namespace latibot::llm
