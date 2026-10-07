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

/// The three documents each guild has (src/modules/llm/docs/Language_Model.md §3.5).
enum class document_kind : std::uint8_t {
    /// How the bot comes across. Anyone may edit it by default.
    personality,
    /// Instructions that are not up for negotiation. Admins only.
    system,
    /// How advanced-trigger replies are written. Admins only.
    trigger_style,
};

[[nodiscard]] auto to_string(document_kind kind) noexcept -> std::string_view;
[[nodiscard]] auto document_kind_from_string(std::string_view name) -> std::optional<document_kind>;

/// What a guild's document says before anyone has edited it: version 0,
/// which is never stored.
[[nodiscard]] auto default_document(document_kind kind) noexcept -> std::string_view;

/// The longest document the bot keeps. Every one of them is sent with every
/// request, so length costs money on each message.
inline constexpr std::size_t document_length_limit = 20000;

/// A rough token count, four characters to a token: close enough to warn
/// that a document is getting expensive, without a tokenizer.
[[nodiscard]] auto estimate_tokens(std::string_view text) noexcept -> std::size_t;

/// Past this, saving a document warns that it is large
/// (src/modules/llm/docs/Language_Model.md §3.5).
inline constexpr std::size_t large_document_tokens = 1500;

struct document_version {
    int version = 0;
    std::string content;
    dpp::snowflake edited_by;
    std::chrono::sys_seconds edited_at;
    std::string note;
};

/// Every version of every guild's documents
/// (src/modules/llm/docs/Language_Model.md §3.5). Nothing is ever overwritten, so
/// nothing is lost and a revert can itself be reverted.
class document_store {
public:
    explicit document_store(db::database& db) : db_(&db) {}

    /// The newest version, or nothing when the guild never edited it.
    [[nodiscard]] auto current(dpp::snowflake guild_id, document_kind kind) const -> std::optional<document_version>;

    /// What the document says now: the newest version, or the default.
    [[nodiscard]] auto text(dpp::snowflake guild_id, document_kind kind) const -> std::string;

    /// Saves `content` as a new version and returns its number.
    auto save(dpp::snowflake guild_id, document_kind kind, std::string_view content, dpp::snowflake edited_by, std::chrono::sys_seconds at,
              std::string_view note = {}) -> int;

    /// Newest first.
    [[nodiscard]] auto history(dpp::snowflake guild_id, document_kind kind) const -> std::vector<document_version>;

    /// One version. Version 0 is the default text, which every guild has.
    [[nodiscard]] auto version(dpp::snowflake guild_id, document_kind kind, int number) const -> std::optional<document_version>;

    /// Saves what `number` said as a new version. Nothing when there is no
    /// such version.
    auto revert(dpp::snowflake guild_id, document_kind kind, int number, dpp::snowflake edited_by, std::chrono::sys_seconds at)
        -> std::optional<int>;

private:
    db::database* db_;
};

/// A line-by-line difference, for `/llm … diff`: removed lines start with
/// "- ", added ones with "+ ", and unchanged ones with two spaces, with long
/// unchanged runs cut to the lines either side of a change. Empty when the
/// texts are the same.
[[nodiscard]] auto diff_lines(std::string_view before, std::string_view after) -> std::string;

} // namespace latibot::llm
