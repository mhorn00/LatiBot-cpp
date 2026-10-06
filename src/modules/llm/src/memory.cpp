#include "memory.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <set>

namespace latibot::llm {
namespace {

/// The most words a search is made of. A long message would otherwise make
/// a query that matches everything a little.
constexpr std::size_t search_words = 16;

/// Words that match nearly every memory. Shorter ones are already out by
/// length. Apostrophes split words, as FTS5 splits them, so "don't" arrives
/// here as "don".
constexpr std::array<std::string_view, 46> common_words{
    "the",  "and",  "you",  "for",   "are",  "was",  "what",  "that",  "this",   "with",   "have", "but",
    "not",  "can",  "your", "just",  "like", "its",  "about", "they",  "them",   "there",  "then", "than",
    "from", "when", "who",  "how",   "why",  "will", "would", "could", "should", "been",   "were", "some",
    "any",  "don",  "didn", "doesn", "isn",  "wasn", "won",   "aren",  "couldn", "wouldn",
};

constexpr std::size_t shortest_word = 3;

auto is_word_byte(char letter) -> bool {
    const auto byte = static_cast<unsigned char>(letter);
    // Bytes above 127 are part of a UTF-8 character, which FTS5's tokenizer
    // reads as a letter too.
    return byte >= 128 || (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
}

auto read_memory(db::statement& row) -> memory {
    return {.id = row.get<std::int64_t>(0),
            .guild_id = row.get<dpp::snowflake>(1),
            .subject = row.get<std::optional<dpp::snowflake>>(2),
            .content = row.get<std::string>(3),
            .created_by = row.get<std::optional<dpp::snowflake>>(4),
            .created_at = row.get<std::chrono::sys_seconds>(5)};
}

constexpr std::string_view columns = "m.id, m.guild_id, m.subject_user_id, m.content, m.created_by, m.created_at";

} // namespace

auto search_query(std::string_view text) -> std::string {
    std::set<std::string> seen;
    std::string query;
    std::size_t words = 0;

    std::size_t at = 0;
    while (at < text.size() && words < search_words) {
        while (at < text.size() && !is_word_byte(text[at])) {
            ++at;
        }
        const std::size_t start = at;
        while (at < text.size() && is_word_byte(text[at])) {
            ++at;
        }

        const std::string word = util::to_lower(text.substr(start, at - start));
        const bool common = std::ranges::find(common_words, std::string_view(word)) != common_words.end();
        if (common || util::character_count(word) < shortest_word || !seen.insert(word).second) continue;

        if (!query.empty()) query += " OR ";
        query += std::format("\"{}\"", word);
        ++words;
    }
    return query;
}

auto memory_store::add(const memory& entry) -> std::int64_t {
    const auto guard = db_->lock();
    db_->prepare("INSERT INTO llm_memory (guild_id, subject_user_id, content, created_by, created_at) VALUES (?, ?, ?, ?, ?)",
                 entry.guild_id, entry.subject, entry.content, entry.created_by, entry.created_at)
        .run();
    return db_->last_insert_rowid();
}

auto memory_store::find(std::int64_t id, dpp::snowflake guild_id) const -> std::optional<memory> {
    auto query = db_->prepare(std::format("SELECT {} FROM llm_memory m WHERE m.id = ? AND m.guild_id = ?", columns), id, guild_id);
    if (!query.step()) return std::nullopt;
    return read_memory(query);
}

auto memory_store::search(dpp::snowflake guild_id, std::string_view text, std::size_t limit) const -> std::vector<memory> {
    std::vector<memory> found;
    const std::string query_text = search_query(text);
    if (query_text.empty() || limit == 0) return found;

    auto query = db_->prepare(std::format("SELECT {} FROM llm_memory_search s JOIN llm_memory m ON m.id = s.rowid "
                                          "WHERE llm_memory_search MATCH ? AND m.guild_id = ? ORDER BY bm25(llm_memory_search) LIMIT ?",
                                          columns),
                              query_text, guild_id, static_cast<std::int64_t>(limit));
    while (query.step()) {
        found.push_back(read_memory(query));
    }
    return found;
}

auto memory_store::list(dpp::snowflake guild_id, std::optional<dpp::snowflake> subject, std::size_t offset, std::size_t limit) const
    -> std::vector<memory> {
    std::vector<memory> found;
    auto query = db_->prepare(std::format("SELECT {} FROM llm_memory m WHERE m.guild_id = ? AND (? IS NULL OR m.subject_user_id = ?) "
                                          "ORDER BY m.created_at DESC, m.id DESC LIMIT ? OFFSET ?",
                                          columns),
                              guild_id, subject, subject, static_cast<std::int64_t>(limit), static_cast<std::int64_t>(offset));
    while (query.step()) {
        found.push_back(read_memory(query));
    }
    return found;
}

auto memory_store::count(dpp::snowflake guild_id, std::optional<dpp::snowflake> subject) const -> std::size_t {
    auto query = db_->prepare("SELECT COUNT(*) FROM llm_memory WHERE guild_id = ? AND (? IS NULL OR subject_user_id = ?)", guild_id,
                              subject, subject);
    return query.step() ? static_cast<std::size_t>(query.get<std::int64_t>(0)) : 0;
}

auto memory_store::remove(std::int64_t id, dpp::snowflake guild_id) -> bool {
    const auto guard = db_->lock();
    db_->prepare("DELETE FROM llm_memory WHERE id = ? AND guild_id = ?", id, guild_id).run();
    return db_->changes() > 0;
}

auto memory_store::clear(dpp::snowflake guild_id, std::optional<dpp::snowflake> subject) -> int {
    const auto guard = db_->lock();
    db_->prepare("DELETE FROM llm_memory WHERE guild_id = ? AND (? IS NULL OR subject_user_id = ?)", guild_id, subject, subject).run();
    return db_->changes();
}

auto relevant_memories(const memory_store& store, dpp::snowflake guild_id, dpp::snowflake author, std::string_view text, std::size_t limit)
    -> std::vector<memory> {
    std::vector<memory> chosen = store.list(guild_id, author, 0, limit / 2);
    for (memory& match : store.search(guild_id, text, limit)) {
        if (chosen.size() >= limit) break;
        if (std::ranges::none_of(chosen, [&](const memory& already) { return already.id == match.id; })) chosen.push_back(std::move(match));
    }
    return chosen;
}

} // namespace latibot::llm
