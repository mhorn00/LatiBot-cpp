#pragma once

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::llm {

/// One thing the model chose to remember (plan §14.5).
struct memory {
    std::int64_t id = 0;
    dpp::snowflake guild_id;

    /// Who it is about; nothing for the server in general.
    std::optional<dpp::snowflake> subject;

    std::string content;

    /// Whom the model was answering when it wrote this.
    std::optional<dpp::snowflake> created_by;

    std::chrono::sys_seconds created_at;
};

/// The longest memory kept, and how many a guild may hold. The model is
/// told both, and `remember` refuses past them rather than quietly
/// dropping the oldest: forgetting is the model's decision, or an admin's.
inline constexpr std::size_t memory_length_limit = 500;
inline constexpr std::size_t memories_per_guild = 500;

/// The words of `text` as an FTS5 query: each quoted, joined with OR, so
/// nothing in the text can be read as FTS5 syntax. Short and very common
/// words are left out, since they match everything. Empty when no word is
/// left.
[[nodiscard]] auto search_query(std::string_view text) -> std::string;

/// `llm_memory` and its full-text index.
class memory_store {
public:
    explicit memory_store(db::database& db) : db_(&db) {}

    /// Returns the new id.
    auto add(const memory& entry) -> std::int64_t;

    [[nodiscard]] auto find(std::int64_t id, dpp::snowflake guild_id) const -> std::optional<memory>;

    /// The guild's memories that best match `text`, best first. Nothing when
    /// the text has no words worth searching for.
    [[nodiscard]] auto search(dpp::snowflake guild_id, std::string_view text, std::size_t limit) const -> std::vector<memory>;

    /// Newest first, optionally only those about one person.
    [[nodiscard]] auto list(dpp::snowflake guild_id, std::optional<dpp::snowflake> subject, std::size_t offset, std::size_t limit) const
        -> std::vector<memory>;

    [[nodiscard]] auto count(dpp::snowflake guild_id, std::optional<dpp::snowflake> subject = std::nullopt) const -> std::size_t;

    auto remove(std::int64_t id, dpp::snowflake guild_id) -> bool;

    /// Everything in a guild, or everything about one person. Returns how
    /// many went.
    auto clear(dpp::snowflake guild_id, std::optional<dpp::snowflake> subject = std::nullopt) -> int;

private:
    db::database* db_;
};

/// What the model is shown before it answers: memories about whoever it is
/// answering, then the ones that match what they said, without repeats, at
/// most `limit` (plan §14.5). Common facts then need no tool call.
[[nodiscard]] auto relevant_memories(const memory_store& store, dpp::snowflake guild_id, dpp::snowflake author, std::string_view text,
                                     std::size_t limit) -> std::vector<memory>;

} // namespace latibot::llm
