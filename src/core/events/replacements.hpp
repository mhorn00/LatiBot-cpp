#pragma once

#include "core/events/url_rules.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::events {

/// Where a replacement message stands (plan §9.3, §9.4).
enum class replacement_state : std::uint8_t {
    /// Posted, previews not confirmed yet.
    pending,
    /// At least one link embedded.
    ok,
    /// Every mirror was tried and nothing embedded; the message carries Retry.
    failed,
    /// Somebody pressed Retry and it is running.
    retrying,
};

[[nodiscard]] std::string_view to_string(replacement_state state) noexcept;
[[nodiscard]] std::optional<replacement_state> replacement_state_from_string(std::string_view name);

/// A message the bot posted in place of somebody's links.
struct replacement_record {
    /// Ours.
    dpp::snowflake message_id;
    dpp::snowflake guild_id;
    dpp::snowflake channel_id;

    /// Nothing for an old message whose original could not be identified
    /// (plan §9.7).
    std::optional<dpp::snowflake> original_message_id;
    std::optional<dpp::snowflake> original_author_id;

    replacement_state state = replacement_state::pending;
    std::chrono::sys_seconds created_at;
    std::optional<std::chrono::sys_seconds> retried_at;

    /// The links it replaced. Mirrors are not stored: Retry uses the rule as
    /// it is at the time, which is the point of retrying later.
    std::vector<planned_link> links;
};

/// `replacement_messages` and `replacement_links`.
class replacement_store {
public:
    explicit replacement_store(db::database& db) : db_(&db) {}

    /// Writes a replacement and its links, replacing any earlier row for the
    /// same message.
    void record(const replacement_record& entry);

    [[nodiscard]] std::optional<replacement_record> find(dpp::snowflake message_id) const;

    /// Whether a message is one of ours. Asked on every reaction, so it reads
    /// one indexed row and nothing else.
    [[nodiscard]] bool contains(dpp::snowflake message_id) const;

    /// False when there is no such replacement.
    bool set_state(dpp::snowflake message_id, replacement_state state);

    /// Records a Retry that finished, whatever it found.
    bool mark_retried(dpp::snowflake message_id, replacement_state state, std::chrono::sys_seconds at);

    /// Every replacement still `pending` or `retrying`, oldest first. Read at
    /// startup, before anything is posted, these are the ones the last run
    /// was still watching when it stopped.
    [[nodiscard]] std::vector<replacement_record> unsettled() const;

private:
    db::database* db_;
};

} // namespace latibot::events
