#pragma once

#include "core/db/statement.hpp"

#include "core/db/schema_versions.hpp"

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

/// The linkstats module's tables, version 1 first
/// (docs/modules/Module_Plan_Final.md §7.1). Defined in module.cpp. They
/// refer to links' `replacement_messages`, so links' come first.
[[nodiscard]] auto linkstats_schema() noexcept -> db::module_schema;

// --------------------------------------------------------------------------
// Emoji
// --------------------------------------------------------------------------

/// The bot's own copy of a custom emoji, an application emoji the bot can
/// show anywhere, even after the original's server deletes it
/// (src/modules/linkstats/docs/Link_Stats.md §10).
struct emoji_copy {
    dpp::snowflake id;
    std::string name;
    bool animated = false;
};

/// An emoji as the statistics know it (src/modules/linkstats/docs/Link_Stats.md §3).
struct emoji_ref {
    /// "u:💀" for a Unicode emoji, "c:<id>" for a custom one.
    std::string key;

    /// The Unicode emoji itself, or the custom emoji's name.
    std::string name;

    bool animated = false;

    /// What is shown in its place, when the bot has a copy of it or of the
    /// same emote under an alias.
    std::optional<emoji_copy> copy;
};

/// The key for a reaction as Discord reports it: an id for a custom emoji, a
/// name for a Unicode one.
///
/// Unicode keys drop U+FE0F, the variation selector that asks for the colour
/// form. Whether it is there depends on the keyboard that typed it, and "❤"
/// and "❤️" are the same heart to everybody reading.
[[nodiscard]] auto reaction_emoji(dpp::snowflake custom_id, std::string_view name, bool animated = false) -> emoji_ref;

/// What somebody typed into a command: `<:skull:123>`, `<a:party:456>`, a
/// Unicode emoji, or a key an autocomplete choice already filled in. Nothing
/// for blank text.
[[nodiscard]] auto parse_emoji(std::string_view text) -> std::optional<emoji_ref>;

/// How to show an emoji in a message: itself, or `<:name:id>`, from the
/// bot's copy when it has one.
[[nodiscard]] auto display_emoji(const emoji_ref& emoji) -> std::string;

// --------------------------------------------------------------------------
// Statistics
// --------------------------------------------------------------------------

/// Which side of a reaction a statistic counts
/// (src/modules/linkstats/docs/Link_Stats.md §2).
enum class stat_kind : std::uint8_t {
    /// Credited to whoever posted the original link.
    received,
    /// Credited to whoever reacted.
    given,
    /// Somebody reacting to their own link: counted on its own, and left out
    /// of the other two.
    self,
};

[[nodiscard]] auto to_string(stat_kind kind) noexcept -> std::string_view;
[[nodiscard]] auto stat_kind_from_string(std::string_view name) -> std::optional<stat_kind>;

/// Which posts a statistic counts: the bot's link replacements, people's own
/// images and videos (src/modules/linkstats/docs/Link_Stats.md §9), or both.
enum class stat_source : std::uint8_t { both, links, images };

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
    /// site. Images have no site, so a site leaves them out.
    std::optional<std::string> domain;

    stat_source source = stat_source::both;
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

/// One emoji somebody sent as a message of its own, just after a post or as a
/// reply to it, which counts as a reaction (src/modules/linkstats/docs/Link_Stats.md §12).
struct emote_reaction {
    dpp::snowflake user_id;
    emoji_ref emoji;

    /// The message it was sent in, which also dates it.
    dpp::snowflake source_id;
};

/// Which emojis `reaction_store::known_emojis` lists.
enum class emoji_listing : std::uint8_t {
    /// Every emoji as it was reacted with, aliases and all.
    every,
    /// What the statistics count, after aliases: an emoji merged into another
    /// is left out, and its reactions are counted with the one it counts as.
    counted,
    /// Only the emojis merged into another, which are what an alias can be
    /// removed from.
    aliases,
};

/// Whether two custom emoji names are close enough to be the same emote:
/// the same ignoring case, or a letter or two apart. How many letters is
/// scaled to the shorter name, since two letters of a three-letter name is
/// most of it: none up to 3 letters, one up to 5, two from 6.
[[nodiscard]] auto names_look_alike(std::string_view first, std::string_view second) -> bool;

/// `reactions`, `emote_reactions`, `reaction_log`, `emojis` and
/// `emoji_aliases`.
///
/// Every statistic reads `counted_reactions`: the reactions, and the emotes
/// sent as reactions that the same person did not also react with, so one
/// person's emoji on one post counts once however they gave it.
class reaction_store {
public:
    explicit reaction_store(db::database& db) : db_(&db) {}

    // -- Recording ----------------------------------------------------------

    /// A reaction added live. Counted only on one of our replacements; true
    /// when it was.
    auto add(dpp::snowflake message_id, dpp::snowflake user_id, const emoji_ref& emoji, std::chrono::sys_seconds at) -> bool;

    /// True when there was such a reaction to remove.
    auto remove(dpp::snowflake message_id, dpp::snowflake user_id, std::string_view emoji_key, std::chrono::sys_seconds at) -> bool;

    /// Somebody with Manage Messages cleared one emoji, or all of them.
    /// Returns how many reactions went.
    auto remove_emoji(dpp::snowflake message_id, std::string_view emoji_key, std::chrono::sys_seconds at) -> int;
    auto remove_all(dpp::snowflake message_id, std::chrono::sys_seconds at) -> int;

    /// One reaction as a backfill sees it.
    struct observed {
        dpp::snowflake user_id;
        std::string emoji_key;
    };

    /// Makes one message's reactions exactly what Discord shows now, which is
    /// what makes a backfill safe to run twice
    /// (src/modules/linkstats/docs/Link_Stats.md §4). Rows that stay keep the time they
    /// were seen being added; new ones have none. Returns how many reactions
    /// the message has afterwards.
    auto replace_for_message(dpp::snowflake message_id, std::span<const observed> reactions) -> int;

    /// Emotes sent as reactions, as they arrive. Kept only on one of our
    /// posts; an emoji a person was already counted for stays as it was.
    /// Returns how many were new.
    auto add_emotes(dpp::snowflake post, std::span<const emote_reaction> found) -> std::size_t;

    /// Makes one post's emote reactions what a recompute found. Rows sent in
    /// a message at or after `unseen_from`, which the recompute did not
    /// read, are kept; nothing means it read everything after the post.
    /// Returns how many it found.
    auto replace_emotes(dpp::snowflake post, std::span<const emote_reaction> found, std::optional<dpp::snowflake> unseen_from)
        -> std::size_t;

    /// A message was deleted: the emotes counted from it go. Returns how
    /// many.
    auto remove_emotes_from(dpp::snowflake source_id) -> std::size_t;

    /// Remembers what an emoji looks like. The newest name wins; `animated`
    /// is only ever turned on, since a backfill cannot tell.
    auto remember(const emoji_ref& emoji) -> void;

    /// What we know about an emoji, falling back to the key itself, with the
    /// bot's copy to show when there is one.
    [[nodiscard]] auto describe(std::string_view emoji_key) const -> emoji_ref;

    /// The bot's copy to show for a custom emoji: its own, or failing that
    /// one of the same emote under an alias, since an alias says the two are
    /// one. Nothing when there is none.
    [[nodiscard]] auto copy_of(std::string_view emoji_key) const -> std::optional<emoji_copy>;

    // -- Aliases ------------------------------------------------------------

    /// Counts `emoji` as `canonical` from now on, and in all history.
    ///
    /// Chains are flattened when written, so reading needs one lookup: an
    /// emoji aliased to something that is itself an alias points at the end
    /// of the chain, and anything that pointed at `emoji` follows it. An
    /// emoji that is already an alias is refused rather than moved, so one
    /// merge is never quietly undone by another. Returns why it could not be
    /// done, or nothing when it was.
    auto set_alias(dpp::snowflake guild_id, std::string_view emoji_key, std::string_view canonical_key) -> std::optional<std::string>;

    /// Counts an emoji as itself again. False when it was not an alias.
    auto remove_alias(dpp::snowflake guild_id, std::string_view emoji_key) -> bool;

    [[nodiscard]] auto aliases(dpp::snowflake guild_id) const -> std::vector<emoji_alias>;

    /// The key an emoji counts as in this guild.
    [[nodiscard]] auto canonical(dpp::snowflake guild_id, std::string_view emoji_key) const -> std::string;

    // -- Reading ------------------------------------------------------------

    [[nodiscard]] auto total(dpp::snowflake guild_id, const stat_query& query) const -> std::int64_t;

    /// People, most first.
    [[nodiscard]] auto leaderboard(dpp::snowflake guild_id, const stat_query& query, std::size_t limit, std::size_t offset = 0) const
        -> std::vector<person_tally>;

    /// How many people a leaderboard has, for paging it.
    [[nodiscard]] auto people(dpp::snowflake guild_id, const stat_query& query) const -> std::int64_t;

    /// How many emojis, after aliases, for paging the emoji board.
    [[nodiscard]] auto emojis(dpp::snowflake guild_id, const stat_query& query) const -> std::int64_t;

    /// The sites this guild's replacements were for, for autocomplete.
    [[nodiscard]] auto known_domains(dpp::snowflake guild_id) const -> std::vector<std::string>;

    /// Emojis, most first, after aliases.
    [[nodiscard]] auto emoji_breakdown(dpp::snowflake guild_id, const stat_query& query, std::size_t limit, std::size_t offset = 0) const
        -> std::vector<emoji_tally>;

    /// The emojis reacted with on this guild's replacements whose name
    /// contains `filter`, most used first, for autocomplete. `which` says
    /// whether aliases are listed apart, merged, or alone.
    [[nodiscard]] auto known_emojis(dpp::snowflake guild_id, std::string_view filter, std::size_t limit,
                                    emoji_listing which = emoji_listing::every) const -> std::vector<emoji_tally>;

    /// Custom emojis whose names look alike (`names_look_alike`), which is
    /// usually one emote uploaded twice, or to two servers
    /// (src/modules/linkstats/docs/Link_Stats.md §5). Emojis already merged by an alias
    /// count as the one they were merged into. Each group is at least two,
    /// most used first, and the groups are in order of how many reactions
    /// they hold between them.
    [[nodiscard]] auto similar_emojis(dpp::snowflake guild_id) const -> std::vector<std::vector<emoji_tally>>;

private:
    /// A statistics query with its six shared filters bound (see
    /// `from_where` in the source).
    [[nodiscard]] auto prepare_stat(std::string_view sql, dpp::snowflake guild_id, const stat_query& query) const -> db::statement;

    auto log_change(dpp::snowflake message_id, dpp::snowflake user_id, std::string_view emoji_key, std::string_view action,
                    std::chrono::sys_seconds at) -> void;

    db::database* db_;
};

} // namespace latibot::events
