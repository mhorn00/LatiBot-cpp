#include "personas.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/util/text.hpp"
#include "prompt.hpp"

#include <array>
#include <format>

namespace latibot::llm {
namespace {

constexpr std::string_view columns = "id, guild_id, name, avatar_url, model, enabled, created_by, created_at";

auto read_persona(db::statement& row) -> persona {
    return {.id = row.get<std::int64_t>(0),
            .guild_id = row.get<dpp::snowflake>(1),
            .name = row.get<std::string>(2),
            .avatar_url = row.get<std::string>(3),
            .model = row.get<std::optional<std::string>>(4).value_or(std::string{}),
            .enabled = row.get<bool>(5),
            .created_by = row.get<dpp::snowflake>(6),
            .created_at = row.get<std::chrono::sys_seconds>(7)};
}

auto model_or_null(const persona& entry) -> std::optional<std::string_view> {
    if (entry.model.empty()) return std::nullopt;
    return entry.model;
}

} // namespace

auto persona_name_problem(std::string_view name) -> std::optional<std::string> {
    const std::string_view trimmed = util::trim(name);
    if (trimmed.empty()) return "a persona needs a name";
    if (util::character_count(trimmed) > persona_name_limit) {
        return std::format("a persona's name is at most {} characters", persona_name_limit);
    }
    if (util::equals_ignoring_case(trimmed, model_name)) return "that's my own name";

    // Discord refuses these in a webhook's name, and @everyone and @here
    // would read as pings.
    const std::string lowered = util::to_lower(trimmed);
    for (const std::string_view refused : std::array<std::string_view, 2>{"clyde", "discord"}) {
        if (lowered.contains(refused)) return std::format("Discord doesn't allow \"{}\" in the name", refused);
    }
    if (lowered == "everyone" || lowered == "here") return "that name would read as a ping";
    return std::nullopt;
}

auto answering_model(const persona& who, std::string_view server_model) -> std::string_view {
    return who.model.empty() ? server_model : std::string_view(who.model);
}

auto avatar_problem(std::string_view url) -> std::optional<std::string> {
    const std::string_view trimmed = util::trim(url);
    if (trimmed.empty()) return std::nullopt;
    if (!trimmed.starts_with("https://")) return "the avatar has to be an https:// link to an image";
    if (trimmed.contains(' ')) return "that link has a space in it";
    if (trimmed.size() > avatar_url_limit) return std::format("that link is over {} characters", avatar_url_limit);
    return std::nullopt;
}

// --------------------------------------------------------------------------

auto persona_store::for_guild(dpp::snowflake guild_id) const -> std::vector<persona> {
    std::vector<persona> found;
    auto query = db_->prepare(std::format("SELECT {} FROM llm_personas WHERE guild_id = ? ORDER BY name", columns), guild_id);
    while (query.step()) {
        found.push_back(read_persona(query));
    }
    return found;
}

auto persona_store::find(std::int64_t id, dpp::snowflake guild_id) const -> std::optional<persona> {
    auto query = db_->prepare(std::format("SELECT {} FROM llm_personas WHERE id = ? AND guild_id = ?", columns), id, guild_id);
    if (!query.step()) return std::nullopt;
    return read_persona(query);
}

auto persona_store::find_named(dpp::snowflake guild_id, std::string_view name) const -> std::optional<persona> {
    // The column compares without case, as the unique constraint does.
    auto query =
        db_->prepare(std::format("SELECT {} FROM llm_personas WHERE guild_id = ? AND name = ?", columns), guild_id, util::trim(name));
    if (!query.step()) return std::nullopt;
    return read_persona(query);
}

auto persona_store::count(dpp::snowflake guild_id) const -> std::size_t {
    auto query = db_->prepare("SELECT count(*) FROM llm_personas WHERE guild_id = ?", guild_id);
    return query.step() ? query.get<std::size_t>(0) : 0;
}

auto persona_store::add(const persona& entry) -> std::int64_t {
    const auto guard = db_->lock();
    db_->prepare(
           "INSERT INTO llm_personas (guild_id, name, avatar_url, model, enabled, created_by, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
           entry.guild_id, entry.name, entry.avatar_url, model_or_null(entry), entry.enabled, entry.created_by, entry.created_at)
        .run();
    return db_->last_insert_rowid();
}

auto persona_store::update(const persona& entry) -> bool {
    const auto guard = db_->lock();
    db_->prepare("UPDATE llm_personas SET name = ?, avatar_url = ?, model = ?, enabled = ? WHERE id = ? AND guild_id = ?", entry.name,
                 entry.avatar_url, model_or_null(entry), entry.enabled, entry.id, entry.guild_id)
        .run();
    return db_->changes() > 0;
}

auto persona_store::remove(std::int64_t id, dpp::snowflake guild_id) -> bool {
    db::transaction tx(*db_);
    db_->prepare("DELETE FROM llm_personas WHERE id = ? AND guild_id = ?", id, guild_id).run();
    if (db_->changes() == 0) return false;

    db_->prepare("DELETE FROM llm_documents WHERE guild_id = ? AND kind LIKE ?", guild_id, std::format("persona.{}.%", id)).run();
    db_->prepare("DELETE FROM llm_triggers WHERE guild_id = ? AND persona_id = ?", guild_id, id).run();
    db_->prepare("DELETE FROM llm_persona_posts WHERE persona_id = ?", id).run();
    tx.commit();
    return true;
}

auto persona_store::record_post(dpp::snowflake message_id, std::int64_t persona_id, dpp::snowflake guild_id, dpp::snowflake channel_id,
                                std::chrono::sys_seconds at) -> void {
    db_->prepare(
           "INSERT OR REPLACE INTO llm_persona_posts (message_id, persona_id, guild_id, channel_id, posted_at) VALUES (?, ?, ?, ?, ?)",
           message_id, persona_id, guild_id, channel_id, at)
        .run();
}

auto persona_store::poster(dpp::snowflake message_id) const -> std::optional<std::int64_t> {
    if (message_id.empty()) return std::nullopt;
    auto query = db_->prepare("SELECT persona_id FROM llm_persona_posts WHERE message_id = ?", message_id);
    if (!query.step()) return std::nullopt;
    return query.get<std::int64_t>(0);
}

} // namespace latibot::llm
