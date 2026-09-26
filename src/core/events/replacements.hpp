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

[[nodiscard]] auto to_string(replacement_state state) noexcept -> std::string_view;
[[nodiscard]] auto replacement_state_from_string(std::string_view name) -> std::optional<replacement_state>;

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
    auto record(const replacement_record& entry) -> void;

    [[nodiscard]] auto find(dpp::snowflake message_id) const -> std::optional<replacement_record>;

    /// Whether a message is one of ours. Asked on every reaction, so it reads
    /// one indexed row and nothing else.
    [[nodiscard]] auto contains(dpp::snowflake message_id) const -> bool;

    /// False when there is no such replacement.
    auto set_state(dpp::snowflake message_id, replacement_state state) -> bool;

    /// Records a Retry that finished, whatever it found.
    auto mark_retried(dpp::snowflake message_id, replacement_state state, std::chrono::sys_seconds at) -> bool;

    /// Every replacement still `pending` or `retrying`, oldest first. Read at
    /// startup, before anything is posted, these are the ones the last run
    /// was still watching when it stopped.
    [[nodiscard]] auto unsettled() const -> std::vector<replacement_record>;

private:
    db::database* db_;
};

} // namespace latibot::events
