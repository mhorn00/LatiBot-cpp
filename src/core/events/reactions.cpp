#include "core/events/reactions.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <map>
#include <set>
#include <utility>

namespace latibot::events {
namespace {

/// U+FE0F, which asks for an emoji's colour form. See `reaction_emoji`.
constexpr std::string_view variation_selector = "\xEF\xB8\x8F";

constexpr std::string_view unicode_prefix = "u:";
constexpr std::string_view custom_prefix = "c:";

auto without_variation_selectors(std::string_view text) -> std::string {
    std::string cleaned;
    cleaned.reserve(text.size());
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t found = text.find(variation_selector, at);
        cleaned += text.substr(at, found == std::string_view::npos ? std::string_view::npos : found - at);
        if (found == std::string_view::npos) {
            break;
        }
        at = found + variation_selector.size();
    }
    return cleaned;
}

auto all_digits(std::string_view text) -> bool {
    return !text.empty() && std::ranges::all_of(text, [](char letter) { return letter >= '0' && letter <= '9'; });
}

/// What an emoji looks like, from its row in `emojis` when it has one. One
/// never seen by name still shows: a Unicode emoji is its own name.
auto emoji_from(std::string_view key, std::optional<std::string> name, bool animated) -> emoji_ref {
    if (name) {
        return {.key = std::string(key), .name = std::move(*name), .animated = animated};
    }

    emoji_ref unknown{.key = std::string(key), .name = {}, .animated = false};
    if (key.starts_with(unicode_prefix)) {
        unknown.name = std::string(key.substr(unicode_prefix.size()));
    }
    return unknown;
}

/// The parts of a statistics query that depend on which side is counted.
struct kind_sql {
    /// Who a row is credited to.
    std::string_view person;

    /// Which rows count at all.
    std::string_view filter;
};

auto sql_for(stat_kind kind) -> kind_sql {
    switch (kind) {
    case stat_kind::given:
        // An unattributed message cannot be a self-reaction, as far as anyone
        // can tell, so its reactions still count as given.
        return {.person = "r.user_id", .filter = "(m.original_author_id IS NULL OR r.user_id <> m.original_author_id)"};
    case stat_kind::self:
        return {.person = "r.user_id", .filter = "r.user_id = m.original_author_id"};
    case stat_kind::received:
        break;
    }
    return {.person = "m.original_author_id", .filter = "m.original_author_id IS NOT NULL AND r.user_id <> m.original_author_id"};
}

/// Everything after SELECT's column list, shared by every statistic.
///
/// Aliases are applied here, at read time, which is what lets one added
/// today change every count back to the first reaction (plan §9.6). The
/// parameters are numbered so every query binds the same six filters in the
/// same order — guild, emoji, since, until, person, site — and a list adds
/// its limit and offset as 7 and 8.
auto from_where(stat_kind kind) -> std::string {
    const kind_sql parts = sql_for(kind);
    return std::format(
        " FROM reactions r"
        " JOIN replacement_messages m ON m.message_id = r.message_id"
        " LEFT JOIN emoji_aliases a ON a.guild_id = m.guild_id AND a.emoji_key = r.emoji_key"
        " WHERE m.guild_id = ?1"
        " AND (?2 IS NULL OR COALESCE(a.canonical_key, r.emoji_key) = ?2)"
        " AND (?3 IS NULL OR COALESCE(r.reacted_at, m.created_at) >= ?3)"
        " AND (?4 IS NULL OR COALESCE(r.reacted_at, m.created_at) < ?4)"
        " AND (?5 IS NULL OR {} = ?5)"
        " AND (?6 IS NULL OR EXISTS (SELECT 1 FROM replacement_links l WHERE l.message_id = m.message_id AND l.domain = ?6))"
        " AND {}",
        parts.person, parts.filter);
}

} // namespace

// --------------------------------------------------------------------------

auto reaction_emoji(dpp::snowflake custom_id, std::string_view name, bool animated) -> emoji_ref {
    if (!custom_id.empty()) {
        return {.key = std::string(custom_prefix) + custom_id.str(), .name = std::string(name), .animated = animated};
    }
    const std::string cleaned = without_variation_selectors(name);
    return {.key = std::string(unicode_prefix) + cleaned, .name = cleaned, .animated = false};
}

auto parse_emoji(std::string_view text) -> std::optional<emoji_ref> {
    text = util::trim(text);
    if (text.empty()) {
        return std::nullopt;
    }

    // A key, which is what an autocomplete choice carries.
    if (text.starts_with(custom_prefix) && all_digits(text.substr(custom_prefix.size()))) {
        return emoji_ref{.key = std::string(text), .name = {}, .animated = false};
    }
    if (text.starts_with(unicode_prefix) && text.size() > unicode_prefix.size()) {
        return reaction_emoji({}, text.substr(unicode_prefix.size()));
    }

    // <:name:id> or <a:name:id>, which is what typing a custom emoji gives.
    if (text.starts_with('<') && text.ends_with('>')) {
        // Strip the brackets and the animated marker, then split "name:id" on
        // the last colon, since the id is always last.
        std::string_view inner = text.substr(1, text.size() - 2);
        const bool animated = inner.starts_with("a:");
        if (animated) {
            inner.remove_prefix(1);
        }
        if (inner.starts_with(':')) {
            inner.remove_prefix(1);
            const std::size_t colon = inner.rfind(':');
            if (colon != std::string_view::npos && all_digits(inner.substr(colon + 1))) {
                std::uint64_t id = 0;
                const std::string_view digits = inner.substr(colon + 1);
                std::from_chars(digits.data(), digits.data() + digits.size(), id);
                return reaction_emoji(dpp::snowflake(id), inner.substr(0, colon), animated);
            }
        }
    }

    // Anything else is taken as a Unicode emoji typed as itself. Whether it is
    // one the guild has used is the caller's question (`resolve_emoji`).
    return reaction_emoji({}, text);
}

auto display_emoji(const emoji_ref& emoji) -> std::string {
    if (emoji.key.starts_with(custom_prefix)) {
        // Discord draws a custom emoji from its id; the name only has to be
        // there, so an unknown one gets a placeholder.
        return std::format("<{}:{}:{}>", emoji.animated ? "a" : "", emoji.name.empty() ? "_" : emoji.name,
                           emoji.key.substr(custom_prefix.size()));
    }
    return emoji.name.empty() ? emoji.key.substr(std::min(emoji.key.size(), unicode_prefix.size())) : emoji.name;
}

auto to_string(stat_kind kind) noexcept -> std::string_view {
    switch (kind) {
    case stat_kind::given:
        return "given";
    case stat_kind::self:
        return "self";
    case stat_kind::received:
        break;
    }
    return "received";
}

auto stat_kind_from_string(std::string_view name) -> std::optional<stat_kind> {
    for (const stat_kind kind : {stat_kind::received, stat_kind::given, stat_kind::self}) {
        if (to_string(kind) == name) {
            return kind;
        }
    }
    return std::nullopt;
}

// --------------------------------------------------------------------------
// Recording
// --------------------------------------------------------------------------

auto reaction_store::log_change(dpp::snowflake message_id, dpp::snowflake user_id, std::string_view emoji_key, std::string_view action,
                                std::chrono::sys_seconds at) -> void {
    db_->prepare("INSERT INTO reaction_log (message_id, user_id, emoji_key, action, at) VALUES (?, ?, ?, ?, ?)", message_id, user_id,
                 emoji_key, action, at)
        .run();
}

auto reaction_store::add(dpp::snowflake message_id, dpp::snowflake user_id, const emoji_ref& emoji, std::chrono::sys_seconds at) -> bool {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    // Only our replacements are counted, and asking in the insert itself is
    // one statement for the reactions on every other message in the guild.
    db_->prepare(
           "INSERT OR IGNORE INTO reactions (message_id, user_id, emoji_key, reacted_at) "
           "SELECT ?1, ?2, ?3, ?4 WHERE EXISTS (SELECT 1 FROM replacement_messages WHERE message_id = ?1)",
           message_id, user_id, emoji.key, at)
        .run();
    if (db_->changes() == 0) {
        return false;
    }

    remember(emoji);
    log_change(message_id, user_id, emoji.key, "add", at);
    tx.commit();
    return true;
}

auto reaction_store::remove(dpp::snowflake message_id, dpp::snowflake user_id, std::string_view emoji_key, std::chrono::sys_seconds at)
    -> bool {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    db_->prepare("DELETE FROM reactions WHERE message_id = ? AND user_id = ? AND emoji_key = ?", message_id, user_id, emoji_key).run();
    if (db_->changes() == 0) {
        return false;
    }

    log_change(message_id, user_id, emoji_key, "remove", at);
    tx.commit();
    return true;
}

auto reaction_store::remove_emoji(dpp::snowflake message_id, std::string_view emoji_key, std::chrono::sys_seconds at) -> int {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    std::vector<dpp::snowflake> reactors;
    {
        auto query = db_->prepare("SELECT user_id FROM reactions WHERE message_id = ? AND emoji_key = ?", message_id, emoji_key);
        while (query.step()) {
            reactors.push_back(query.get<dpp::snowflake>(0));
        }
    }

    for (const dpp::snowflake user_id : reactors) {
        log_change(message_id, user_id, emoji_key, "remove", at);
    }
    db_->prepare("DELETE FROM reactions WHERE message_id = ? AND emoji_key = ?", message_id, emoji_key).run();

    tx.commit();
    return static_cast<int>(reactors.size());
}

auto reaction_store::remove_all(dpp::snowflake message_id, std::chrono::sys_seconds at) -> int {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    std::vector<observed> gone;
    {
        auto query = db_->prepare("SELECT user_id, emoji_key FROM reactions WHERE message_id = ?", message_id);
        while (query.step()) {
            gone.push_back({.user_id = query.get<dpp::snowflake>(0), .emoji_key = query.get<std::string>(1)});
        }
    }

    for (const observed& reaction : gone) {
        log_change(message_id, reaction.user_id, reaction.emoji_key, "remove", at);
    }
    db_->prepare("DELETE FROM reactions WHERE message_id = ?", message_id).run();

    tx.commit();
    return static_cast<int>(gone.size());
}

auto reaction_store::replace_for_message(dpp::snowflake message_id, std::span<const observed> reactions) -> int {
    const auto guard = db_->lock();
    db::transaction tx(*db_);

    std::set<std::pair<std::uint64_t, std::string>> wanted;
    for (const observed& reaction : reactions) {
        wanted.emplace(static_cast<std::uint64_t>(reaction.user_id), reaction.emoji_key);
    }

    std::vector<std::pair<std::uint64_t, std::string>> stale;
    {
        auto query = db_->prepare("SELECT user_id, emoji_key FROM reactions WHERE message_id = ?", message_id);
        while (query.step()) {
            std::pair<std::uint64_t, std::string> existing{query.get<std::uint64_t>(0), query.get<std::string>(1)};
            if (!wanted.contains(existing)) {
                stale.push_back(std::move(existing));
            }
        }
    }

    for (const auto& [user_id, emoji_key] : stale) {
        db_->prepare("DELETE FROM reactions WHERE message_id = ? AND user_id = ? AND emoji_key = ?", message_id, user_id, emoji_key).run();
    }

    // INSERT OR IGNORE keeps a row that is already there, and with it the
    // time a live reaction was seen being added.
    for (const auto& [user_id, emoji_key] : wanted) {
        db_->prepare("INSERT OR IGNORE INTO reactions (message_id, user_id, emoji_key, reacted_at) VALUES (?, ?, ?, NULL)", message_id,
                     user_id, emoji_key)
            .run();
    }

    tx.commit();
    return static_cast<int>(wanted.size());
}

auto reaction_store::remember(const emoji_ref& emoji) -> void {
    if (emoji.key.empty()) {
        return;
    }
    const std::string name = emoji.name.empty() ? display_emoji(emoji) : emoji.name;
    db_->prepare(
           "INSERT INTO emojis (emoji_key, name, animated) VALUES (?, ?, ?) "
           "ON CONFLICT (emoji_key) DO UPDATE SET name = excluded.name, animated = MAX(animated, excluded.animated)",
           emoji.key, name, emoji.animated)
        .run();
}

auto reaction_store::describe(std::string_view emoji_key) const -> emoji_ref {
    auto query = db_->prepare("SELECT name, animated FROM emojis WHERE emoji_key = ?", emoji_key);
    if (query.step()) {
        return emoji_from(emoji_key, query.get<std::string>(0), query.get<bool>(1));
    }
    return emoji_from(emoji_key, std::nullopt, false);
}

// --------------------------------------------------------------------------
// Aliases
// --------------------------------------------------------------------------

auto reaction_store::canonical(dpp::snowflake guild_id, std::string_view emoji_key) const -> std::string {
    auto query = db_->prepare("SELECT canonical_key FROM emoji_aliases WHERE guild_id = ? AND emoji_key = ?", guild_id, emoji_key);
    return query.step() ? query.get<std::string>(0) : std::string(emoji_key);
}

auto reaction_store::set_alias(dpp::snowflake guild_id, std::string_view emoji_key, std::string_view canonical_key)
    -> std::optional<std::string> {
    const auto guard = db_->lock();

    if (emoji_key == canonical_key) {
        return std::string("that's the same emoji");
    }

    const std::string target = canonical(guild_id, canonical_key);
    if (target == emoji_key) {
        return std::string("that one already counts as this one; remove that alias first");
    }

    db::transaction tx(*db_);
    db_->prepare(
           "INSERT INTO emoji_aliases (guild_id, emoji_key, canonical_key) VALUES (?, ?, ?) "
           "ON CONFLICT (guild_id, emoji_key) DO UPDATE SET canonical_key = excluded.canonical_key",
           guild_id, emoji_key, target)
        .run();

    // Anything that counted as this emoji now counts as what it counts as, so
    // no chain is ever more than one step.
    db_->prepare("UPDATE emoji_aliases SET canonical_key = ? WHERE guild_id = ? AND canonical_key = ?", target, guild_id, emoji_key).run();

    tx.commit();
    return std::nullopt;
}

auto reaction_store::remove_alias(dpp::snowflake guild_id, std::string_view emoji_key) -> bool {
    const auto guard = db_->lock();
    db_->prepare("DELETE FROM emoji_aliases WHERE guild_id = ? AND emoji_key = ?", guild_id, emoji_key).run();
    return db_->changes() > 0;
}

auto reaction_store::aliases(dpp::snowflake guild_id) const -> std::vector<emoji_alias> {
    std::vector<std::pair<std::string, std::string>> keys;
    {
        auto query = db_->prepare("SELECT emoji_key, canonical_key FROM emoji_aliases WHERE guild_id = ? ORDER BY canonical_key, emoji_key",
                                  guild_id);
        while (query.step()) {
            keys.emplace_back(query.get<std::string>(0), query.get<std::string>(1));
        }
    }

    std::vector<emoji_alias> found;
    found.reserve(keys.size());
    for (const auto& [key, canonical_key] : keys) {
        found.push_back({.emoji = describe(key), .canonical = describe(canonical_key)});
    }
    return found;
}

// --------------------------------------------------------------------------
// Reading
// --------------------------------------------------------------------------

auto reaction_store::prepare_stat(std::string_view sql, dpp::snowflake guild_id, const stat_query& query) const -> db::statement {
    // The alias is resolved first, so asking for an emoji that was merged
    // away asks for what it now counts as.
    const std::optional<std::string> emoji =
        query.emoji_key ? std::optional<std::string>(canonical(guild_id, *query.emoji_key)) : std::nullopt;

    return db_->prepare(sql, guild_id, emoji, query.since, query.until, query.user_id, query.domain);
}

auto reaction_store::total(dpp::snowflake guild_id, const stat_query& query) const -> std::int64_t {
    auto statement = prepare_stat("SELECT COUNT(*)" + from_where(query.kind), guild_id, query);
    return statement.step() ? statement.get<std::int64_t>(0) : 0;
}

auto reaction_store::people(dpp::snowflake guild_id, const stat_query& query) const -> std::int64_t {
    auto statement =
        prepare_stat(std::format("SELECT COUNT(DISTINCT {}){}", sql_for(query.kind).person, from_where(query.kind)), guild_id, query);
    return statement.step() ? statement.get<std::int64_t>(0) : 0;
}

auto reaction_store::emojis(dpp::snowflake guild_id, const stat_query& query) const -> std::int64_t {
    auto statement =
        prepare_stat("SELECT COUNT(DISTINCT COALESCE(a.canonical_key, r.emoji_key))" + from_where(query.kind), guild_id, query);
    return statement.step() ? statement.get<std::int64_t>(0) : 0;
}

auto reaction_store::leaderboard(dpp::snowflake guild_id, const stat_query& query, std::size_t limit, std::size_t offset) const
    -> std::vector<person_tally> {
    const std::string_view person = sql_for(query.kind).person;
    auto statement = prepare_stat(
        std::format("SELECT {0}, COUNT(*) AS n{1} GROUP BY {0} ORDER BY n DESC, {0} LIMIT ?7 OFFSET ?8", person, from_where(query.kind)),
        guild_id, query);
    statement.bind(7, static_cast<std::int64_t>(limit)).bind(8, static_cast<std::int64_t>(offset));

    std::vector<person_tally> board;
    while (statement.step()) {
        board.push_back({.user_id = statement.get<dpp::snowflake>(0), .count = statement.get<std::int64_t>(1)});
    }
    return board;
}

auto reaction_store::known_domains(dpp::snowflake guild_id) const -> std::vector<std::string> {
    std::vector<std::string> domains;
    auto statement = db_->prepare(
        "SELECT DISTINCT l.domain FROM replacement_links l "
        "JOIN replacement_messages m ON m.message_id = l.message_id "
        "WHERE m.guild_id = ? ORDER BY l.domain",
        guild_id);
    while (statement.step()) {
        domains.push_back(statement.get<std::string>(0));
    }
    return domains;
}

auto reaction_store::emoji_breakdown(dpp::snowflake guild_id, const stat_query& query, std::size_t limit, std::size_t offset) const
    -> std::vector<emoji_tally> {
    std::vector<std::pair<std::string, std::int64_t>> counted;
    {
        auto statement = prepare_stat("SELECT COALESCE(a.canonical_key, r.emoji_key) AS k, COUNT(*) AS n" + from_where(query.kind) +
                                          " GROUP BY k ORDER BY n DESC, k LIMIT ?7 OFFSET ?8",
                                      guild_id, query);
        statement.bind(7, static_cast<std::int64_t>(limit)).bind(8, static_cast<std::int64_t>(offset));
        while (statement.step()) {
            counted.emplace_back(statement.get<std::string>(0), statement.get<std::int64_t>(1));
        }
    }

    std::vector<emoji_tally> breakdown;
    breakdown.reserve(counted.size());
    for (const auto& [key, count] : counted) {
        breakdown.push_back({.emoji = describe(key), .count = count});
    }
    return breakdown;
}

auto reaction_store::known_emojis(dpp::snowflake guild_id, std::string_view filter, std::size_t limit) const -> std::vector<emoji_tally> {
    // The names come back with the counts, rather than one lookup per emoji:
    // this runs on every keystroke of an autocomplete, and a filter that
    // matched little used to look up every emoji the guild had ever used.
    // The filter stays here, since a Unicode emoji with no row is named by
    // its key, which SQL cannot see.
    auto statement = db_->prepare(
        "SELECT r.emoji_key, COUNT(*) AS n, e.name, e.animated FROM reactions r "
        "JOIN replacement_messages m ON m.message_id = r.message_id "
        "LEFT JOIN emojis e ON e.emoji_key = r.emoji_key "
        "WHERE m.guild_id = ? GROUP BY r.emoji_key ORDER BY n DESC, r.emoji_key",
        guild_id);

    const std::string wanted = util::to_lower(util::trim(filter));
    std::vector<emoji_tally> found;
    while (found.size() < limit && statement.step()) {
        emoji_ref emoji = emoji_from(statement.get<std::string>(0), statement.get<std::optional<std::string>>(2),
                                     statement.get<std::optional<std::int64_t>>(3).value_or(0) != 0);
        if (wanted.empty() || util::to_lower(emoji.name).find(wanted) != std::string::npos) {
            found.push_back({.emoji = std::move(emoji), .count = statement.get<std::int64_t>(1)});
        }
    }
    return found;
}

auto reaction_store::likely_duplicates(dpp::snowflake guild_id) const -> std::vector<std::vector<emoji_tally>> {
    std::map<std::string, std::vector<emoji_tally>> by_name;
    for (emoji_tally& tally : known_emojis(guild_id, {}, static_cast<std::size_t>(-1))) {
        if (tally.emoji.key.starts_with(custom_prefix) && !tally.emoji.name.empty()) {
            by_name[util::to_lower(tally.emoji.name)].push_back(std::move(tally));
        }
    }

    std::vector<std::vector<emoji_tally>> groups;
    for (auto& [name, group] : by_name) {
        if (group.size() > 1) {
            groups.push_back(std::move(group));
        }
    }
    return groups;
}

} // namespace latibot::events
