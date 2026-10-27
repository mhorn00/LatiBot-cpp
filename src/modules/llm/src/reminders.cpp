#include "reminders.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"

namespace latibot::llm {

auto reminder_store::add(const reminder& entry) -> std::int64_t {
    const auto guard = db_->lock();
    db_->prepare("INSERT INTO llm_reminders (guild_id, channel_id, user_id, set_by, persona_id, due_at, text) VALUES (?, ?, ?, ?, ?, ?, ?)",
                 entry.guild_id, entry.channel_id, entry.user_id, entry.set_by, entry.persona_id, entry.due_at, entry.text)
        .run();
    return db_->last_insert_rowid();
}

auto reminder_store::due(std::chrono::sys_seconds now) const -> std::vector<reminder> {
    std::vector<reminder> found;
    auto query = db_->prepare(
        "SELECT id, guild_id, channel_id, user_id, set_by, persona_id, due_at, text FROM llm_reminders WHERE due_at <= ? "
        "ORDER BY due_at, id",
        now);
    while (query.step()) {
        found.push_back({.id = query.get<std::int64_t>(0),
                         .guild_id = query.get<dpp::snowflake>(1),
                         .channel_id = query.get<dpp::snowflake>(2),
                         .user_id = query.get<dpp::snowflake>(3),
                         .set_by = query.get<dpp::snowflake>(4),
                         .persona_id = query.get<std::int64_t>(5),
                         .due_at = query.get<std::chrono::sys_seconds>(6),
                         .text = query.get<std::string>(7)});
    }
    return found;
}

auto reminder_store::count(dpp::snowflake guild_id) const -> std::size_t {
    auto query = db_->prepare("SELECT COUNT(*) FROM llm_reminders WHERE guild_id = ?", guild_id);
    return query.step() ? static_cast<std::size_t>(query.get<std::int64_t>(0)) : 0;
}

auto reminder_store::remove(std::int64_t id) -> bool {
    const auto guard = db_->lock();
    db_->prepare("DELETE FROM llm_reminders WHERE id = ?", id).run();
    return db_->changes() > 0;
}

} // namespace latibot::llm
