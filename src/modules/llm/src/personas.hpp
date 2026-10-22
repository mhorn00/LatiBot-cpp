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

/// Someone else the model answers as: its own name and avatar, posted
/// through a webhook, with instructions of its own
/// (src/modules/llm/docs/Language_Model.md §2.11).
struct persona {
    std::int64_t id = 0;
    dpp::snowflake guild_id;

    /// What it is called, and answers to. Unique in its server, ignoring
    /// case.
    std::string name;

    /// The image shown beside its messages; empty for the webhook's own.
    std::string avatar_url;

    /// The model it answers with; empty for the server's.
    std::string model;

    bool enabled = true;
    dpp::snowflake created_by;
    std::chrono::sys_seconds created_at;
};

/// How many personas a server may have.
inline constexpr std::size_t persona_limit = 10;

/// The longest name Discord shows on a webhook's message.
inline constexpr std::size_t persona_name_limit = 80;

/// The longest avatar URL kept.
inline constexpr std::size_t avatar_url_limit = 1000;

/// Why a persona cannot be called `name`, or nothing when it can: it is
/// empty or too long, it is the bot's own name, or it holds a word Discord
/// refuses in a webhook's name.
[[nodiscard]] auto persona_name_problem(std::string_view name) -> std::optional<std::string>;

/// The model a persona answers with: its own, or the server's.
[[nodiscard]] auto answering_model(const persona& who, std::string_view server_model) -> std::string_view;

/// Why `url` cannot be a persona's avatar, or nothing when it can. Empty is
/// fine: the webhook's own is shown.
[[nodiscard]] auto avatar_problem(std::string_view url) -> std::optional<std::string>;

/// `llm_personas`, and `llm_persona_posts`: what each persona posted, so a
/// reply to one of its messages is a reply to it.
class persona_store {
public:
    explicit persona_store(db::database& db) : db_(&db) {}

    /// Every persona in the guild, by name.
    [[nodiscard]] auto for_guild(dpp::snowflake guild_id) const -> std::vector<persona>;
    [[nodiscard]] auto find(std::int64_t id, dpp::snowflake guild_id) const -> std::optional<persona>;

    /// The one called `name` here, ignoring case.
    [[nodiscard]] auto find_named(dpp::snowflake guild_id, std::string_view name) const -> std::optional<persona>;

    [[nodiscard]] auto count(dpp::snowflake guild_id) const -> std::size_t;

    /// Returns the new id. The name must not be taken here.
    auto add(const persona& entry) -> std::int64_t;

    /// False when it does not exist, or belongs to another guild.
    auto update(const persona& entry) -> bool;

    /// Removes it with its documents, its triggers and the record of what
    /// it posted. Its messages stay in Discord. False when there is none.
    auto remove(std::int64_t id, dpp::snowflake guild_id) -> bool;

    /// Notes that `persona_id` posted `message_id`.
    auto record_post(dpp::snowflake message_id, std::int64_t persona_id, dpp::snowflake guild_id, dpp::snowflake channel_id,
                     std::chrono::sys_seconds at) -> void;

    /// The persona that posted a message, or nothing when none did.
    [[nodiscard]] auto poster(dpp::snowflake message_id) const -> std::optional<std::int64_t>;

private:
    db::database* db_;
};

} // namespace latibot::llm
