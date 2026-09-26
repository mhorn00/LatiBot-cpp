#include "core/events/replacements.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"

#include <array>
#include <utility>

namespace latibot::events {
namespace {

constexpr std::array<std::string_view, 4> state_names{"pending", "ok", "failed", "retrying"};

} // namespace

auto to_string(replacement_state state) noexcept -> std::string_view {
    const auto index = static_cast<std::size_t>(state);
    return index < state_names.size() ? state_names[index] : "pending";
}

auto replacement_state_from_string(std::string_view name) -> std::optional<replacement_state> {
    for (std::size_t index = 0; index < state_names.size(); ++index) {
        if (state_names[index] == name) {
            return static_cast<replacement_state>(index);
        }
    }
    return std::nullopt;
}

auto replacement_store::record(const replacement_record& entry) -> void {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    // An upsert rather than INSERT OR REPLACE: replacing would delete the row
    // first, and the reactions that hang off it with it.
    db_->prepare(
           "INSERT INTO replacement_messages (message_id, guild_id, channel_id, original_message_id, original_author_id, state, "
           "created_at, retried_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
           "ON CONFLICT (message_id) DO UPDATE SET guild_id = excluded.guild_id, channel_id = excluded.channel_id, "
           "original_message_id = excluded.original_message_id, original_author_id = excluded.original_author_id, "
           "state = excluded.state, created_at = excluded.created_at, retried_at = excluded.retried_at",
           entry.message_id, entry.guild_id, entry.channel_id, entry.original_message_id, entry.original_author_id, to_string(entry.state),
           entry.created_at, entry.retried_at)
        .run();

    db_->prepare("DELETE FROM replacement_links WHERE message_id = ?", entry.message_id).run();
    int position = 0;
    for (const planned_link& link : entry.links) {
        db_->prepare("INSERT INTO replacement_links (message_id, position, original_url, domain, spoilered) VALUES (?, ?, ?, ?, ?)",
                     entry.message_id, position++, link.original_url, link.domain, link.spoilered)
            .run();
    }

    tx.commit();
}

auto replacement_store::find(dpp::snowflake message_id) const -> std::optional<replacement_record> {
    const auto guard = db_->lock();

    replacement_record entry;
    {
        auto query = db_->prepare(
            "SELECT guild_id, channel_id, original_message_id, original_author_id, state, created_at, retried_at "
            "FROM replacement_messages WHERE message_id = ?",
            message_id);
        if (!query.step()) {
            return std::nullopt;
        }

        entry.message_id = message_id;
        entry.guild_id = query.get<dpp::snowflake>(0);
        entry.channel_id = query.get<dpp::snowflake>(1);
        entry.original_message_id = query.get<std::optional<dpp::snowflake>>(2);
        entry.original_author_id = query.get<std::optional<dpp::snowflake>>(3);
        entry.state = replacement_state_from_string(query.get<std::string>(4)).value_or(replacement_state::ok);
        entry.created_at = query.get<std::chrono::sys_seconds>(5);
        entry.retried_at = query.get<std::optional<std::chrono::sys_seconds>>(6);
    }

    auto links =
        db_->prepare("SELECT original_url, domain, spoilered FROM replacement_links WHERE message_id = ? ORDER BY position", message_id);
    while (links.step()) {
        entry.links.push_back({.original_url = links.get<std::string>(0),
                               .domain = links.get<std::string>(1),
                               .spoilered = links.get<bool>(2),
                               .mirrors = {}});
    }

    return entry;
}

auto replacement_store::contains(dpp::snowflake message_id) const -> bool {
    auto query = db_->prepare("SELECT 1 FROM replacement_messages WHERE message_id = ?", message_id);
    return query.step();
}

auto replacement_store::set_state(dpp::snowflake message_id, replacement_state state) -> bool {
    const auto guard = db_->lock();
    db_->prepare("UPDATE replacement_messages SET state = ? WHERE message_id = ?", to_string(state), message_id).run();
    return db_->changes() > 0;
}

auto replacement_store::mark_retried(dpp::snowflake message_id, replacement_state state, std::chrono::sys_seconds at) -> bool {
    const auto guard = db_->lock();
    db_->prepare("UPDATE replacement_messages SET state = ?, retried_at = ? WHERE message_id = ?", to_string(state), at, message_id).run();
    return db_->changes() > 0;
}

auto replacement_store::unsettled() const -> std::vector<replacement_record> {
    const auto guard = db_->lock();

    std::vector<dpp::snowflake> ids;
    {
        auto query = db_->prepare("SELECT message_id FROM replacement_messages WHERE state IN (?, ?) ORDER BY message_id",
                                  to_string(replacement_state::pending), to_string(replacement_state::retrying));
        while (query.step()) {
            ids.push_back(query.get<dpp::snowflake>(0));
        }
    }

    std::vector<replacement_record> found;
    found.reserve(ids.size());
    for (const dpp::snowflake id : ids) {
        if (auto entry = find(id)) {
            found.push_back(std::move(*entry));
        }
    }
    return found;
}

} // namespace latibot::events
