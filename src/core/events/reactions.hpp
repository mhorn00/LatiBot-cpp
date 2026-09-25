#pragma once

#include "core/db/statement.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::db {
class database;
} // namespace latibot::db

namespace latibot::events {

// --------------------------------------------------------------------------
// Emoji
// --------------------------------------------------------------------------

/// An emoji as the statistics know it (plan §9.6).
struct emoji_ref {
    /// "u:💀" for a Unicode emoji, "c:<id>" for a custom one.
    std::string key;

    /// The Unicode emoji itself, or the custom emoji's name.
    std::string name;

    bool animated = false;
};

/// The key for a reaction as Discord reports it: an id for a custom emoji, a
/// name for a Unicode one.
///
/// Unicode keys drop U+FE0F, the variation selector that asks for the colour
/// form. Whether it is there depends on the keyboard that typed it, and "❤"
/// and "❤️" are the same heart to everybody reading.
[[nodiscard]] emoji_ref reaction_emoji(dpp::snowflake custom_id, std::string_view name, bool animated = false);

/// What somebody typed into a command: `<:skull:123>`, `<a:party:456>`, a
/// Unicode emoji, or a key an autocomplete choice already filled in. Nothing
/// for blank text.
[[nodiscard]] std::optional<emoji_ref> parse_emoji(std::string_view text);

/// How to show an emoji in a message: itself, or `<:name:id>`.
[[nodiscard]] std::string display_emoji(const emoji_ref& emoji);

// --------------------------------------------------------------------------
// Statistics
// --------------------------------------------------------------------------

/// Which side of a reaction a statistic counts (plan §9.6).
enum class stat_kind : std::uint8_t {
    /// Credited to whoever posted the original link.
    received,
    /// Credited to whoever reacted.
    given,
    /// Somebody reacting to their own link: counted on its own, and left out
    /// of the other two.
    self,
};

[[nodiscard]] std::string_view to_string(stat_kind kind) noexcept;
[[nodiscard]] std::optional<stat_kind> stat_kind_from_string(std::string_view name);

/// What to count.
struct stat_query {
    stat_kind kind = stat_kind::received;

    /// Only this emoji, after aliases. Nothing for every emoji.
    std::optional<std::string> emoji_key;

    /// Only this person, on the side `kind` counts. Nothing for everybody.
    std::optional<dpp::snowflake> user_id;

    /// Half-open. A reaction's time is when it was added, or for a backfilled
    /// one, which Discord never dated, when the message was posted.
    std::optional<std::chrono::sys_seconds> since;
    std::optional<std::chrono::sys_seconds> until;

    /// Only replacements of links to this site ("x.com"). Nothing for every
    /// site.
    std::optional<std::string> domain;
};

struct person_tally {
    dpp::snowflake user_id;
    std::int64_t count = 0;
};

struct emoji_tally {
    emoji_ref emoji;
    std::int64_t count = 0;
};

struct emoji_alias {
    emoji_ref emoji;
    emoji_ref canonical;
};

/// `reactions`, `reaction_log`, `emojis` and `emoji_aliases`.
class reaction_store {
public:
    explicit reaction_store(db::database& db) : db_(&db) {}

    // -- Recording ----------------------------------------------------------

    /// A reaction added live. Counted only on one of our replacements; true
    /// when it was.
    bool add(dpp::snowflake message_id, dpp::snowflake user_id, const emoji_ref& emoji, std::chrono::sys_seconds at);

    /// True when there was such a reaction to remove.
    bool remove(dpp::snowflake message_id, dpp::snowflake user_id, std::string_view emoji_key, std::chrono::sys_seconds at);

    /// Somebody with Manage Messages cleared one emoji, or all of them.
    /// Returns how many reactions went.
    int remove_emoji(dpp::snowflake message_id, std::string_view emoji_key, std::chrono::sys_seconds at);
    int remove_all(dpp::snowflake message_id, std::chrono::sys_seconds at);

    /// One reaction as a backfill sees it.
    struct observed {
        dpp::snowflake user_id;
        std::string emoji_key;
    };

    /// Makes one message's reactions exactly what Discord shows now, which is
    /// what makes a backfill safe to run twice (plan §9.7). Rows that stay
    /// keep the time they were seen being added; new ones have none. Returns
    /// how many reactions the message has afterwards.
    int replace_for_message(dpp::snowflake message_id, std::span<const observed> reactions);

    /// Remembers what an emoji looks like. The newest name wins; `animated`
    /// is only ever turned on, since a backfill cannot tell.
    void remember(const emoji_ref& emoji);

    /// What we know about an emoji, falling back to the key itself.
    [[nodiscard]] emoji_ref describe(std::string_view emoji_key) const;

    // -- Aliases ------------------------------------------------------------

    /// Counts `emoji` as `canonical` from now on, and in all history.
    ///
    /// Chains are flattened when written, so reading needs one lookup: an
    /// emoji aliased to something that is itself an alias points at the end
    /// of the chain, and anything that pointed at `emoji` follows it. Returns
    /// why it could not be done, or nothing when it was.
    std::optional<std::string> set_alias(dpp::snowflake guild_id, std::string_view emoji_key, std::string_view canonical_key);

    /// Counts an emoji as itself again. False when it was not an alias.
    bool remove_alias(dpp::snowflake guild_id, std::string_view emoji_key);

    [[nodiscard]] std::vector<emoji_alias> aliases(dpp::snowflake guild_id) const;

    /// The key an emoji counts as in this guild.
    [[nodiscard]] std::string canonical(dpp::snowflake guild_id, std::string_view emoji_key) const;

    // -- Reading ------------------------------------------------------------

    [[nodiscard]] std::int64_t total(dpp::snowflake guild_id, const stat_query& query) const;

    /// People, most first.
    [[nodiscard]] std::vector<person_tally> leaderboard(dpp::snowflake guild_id, const stat_query& query, std::size_t limit,
                                                        std::size_t offset = 0) const;

    /// How many people a leaderboard has, for paging it.
    [[nodiscard]] std::int64_t people(dpp::snowflake guild_id, const stat_query& query) const;

    /// How many emojis, after aliases, for paging the emoji board.
    [[nodiscard]] std::int64_t emojis(dpp::snowflake guild_id, const stat_query& query) const;

    /// The sites this guild's replacements were for, for autocomplete.
    [[nodiscard]] std::vector<std::string> known_domains(dpp::snowflake guild_id) const;

    /// Emojis, most first, after aliases.
    [[nodiscard]] std::vector<emoji_tally> emoji_breakdown(dpp::snowflake guild_id, const stat_query& query, std::size_t limit,
                                                           std::size_t offset = 0) const;

    /// Every emoji reacted with on this guild's replacements whose name
    /// contains `filter`, most used first, for autocomplete.
    [[nodiscard]] std::vector<emoji_tally> known_emojis(dpp::snowflake guild_id, std::string_view filter, std::size_t limit) const;

    /// Custom emojis that share a name, which is usually one emote uploaded
    /// twice (plan §9.6). Each group is at least two.
    [[nodiscard]] std::vector<std::vector<emoji_tally>> likely_duplicates(dpp::snowflake guild_id) const;

private:
    /// A statistics query with its six shared filters bound (see
    /// `from_where` in the source).
    [[nodiscard]] db::statement prepare_stat(std::string_view sql, dpp::snowflake guild_id, const stat_query& query) const;

    void log_change(dpp::snowflake message_id, dpp::snowflake user_id, std::string_view emoji_key, std::string_view action,
                    std::chrono::sys_seconds at);

    db::database* db_;
};

} // namespace latibot::events
