#include "core/events/nicknames.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"

#include <dpp/json.h>

#include <algorithm>
#include <format>

namespace latibot::events {
namespace {

/// Unix seconds, for the <t:…> timestamps Discord shows in local time.
auto to_unix(std::chrono::system_clock::time_point when) -> std::int64_t {
    return std::chrono::duration_cast<std::chrono::seconds>(when.time_since_epoch()).count();
}

/// Every column of a history row, in the order `read_row` expects.
constexpr std::string_view row_columns = "id, guild_id, user_id, nickname, changed_at, changed_by, source, imported_raw";

auto read_row(const db::statement& row) -> nickname_change {
    nickname_change change;
    change.id = row.get<std::int64_t>(0);
    change.guild_id = row.get<dpp::snowflake>(1);
    change.user_id = row.get<dpp::snowflake>(2);
    change.nickname = row.get<std::optional<std::string>>(3);
    change.changed_at = row.get<std::chrono::sys_seconds>(4);
    change.changed_by = row.get<std::optional<dpp::snowflake>>(5);
    change.source = nickname_source_from_string(row.get<std::string>(6)).value_or(nickname_source::seen);
    change.imported_raw = row.get<std::optional<std::string>>(7).value_or(std::string{});
    return change;
}

/// Text to store, where an empty string means "no value" rather than "".
auto text_or_null(const std::string& text) -> std::optional<std::string> {
    return text.empty() ? std::optional<std::string>{} : std::optional(text);
}

} // namespace

auto to_string(nickname_source source) noexcept -> std::string_view {
    switch (source) {
    case nickname_source::command:
        return "command";
    case nickname_source::audit_log:
        return "audit_log";
    case nickname_source::startup:
        return "startup";
    case nickname_source::imported:
        return "imported";
    case nickname_source::seen:
        break;
    }
    return "seen";
}

auto nickname_source_from_string(std::string_view name) -> std::optional<nickname_source> {
    if (name == "command") return nickname_source::command;
    if (name == "audit_log") return nickname_source::audit_log;
    if (name == "seen") return nickname_source::seen;
    if (name == "startup") return nickname_source::startup;
    if (name == "imported") return nickname_source::imported;
    return std::nullopt;
}

// --------------------------------------------------------------------------

auto is_new_nickname(const std::optional<nickname_change>& latest, const std::optional<std::string>& current) -> bool {
    if (!latest) {
        // Nothing on record. A member with no nickname is not a change worth
        // writing down; one with a nickname is the first thing known about them.
        return current.has_value();
    }
    return latest->nickname != current;
}

auto describes(const nickname_change& change, dpp::snowflake target, const std::optional<std::string>& new_nickname) -> bool {
    return change.user_id == target && change.nickname == new_nickname;
}

auto audit_nickname(std::string_view dumped_json) -> std::optional<std::string> {
    if (dumped_json.empty() || dumped_json == "null") return std::nullopt;

    const auto parsed = nlohmann::json::parse(dumped_json, nullptr, /*allow_exceptions=*/false);
    if (!parsed.is_string()) return std::nullopt;
    return parsed.get<std::string>();
}

auto may_attribute(const nickname_change& change, dpp::snowflake actor, dpp::snowflake self) -> bool {
    if (actor.empty() || actor == self) return false;
    return !change.changed_by.has_value();
}

// --------------------------------------------------------------------------

auto show_nickname(const std::optional<std::string>& nickname) -> std::string {
    return nickname && !nickname->empty() ? *nickname : "*(cleared)*";
}

auto show_author(const nickname_change& change) -> std::string {
    if (change.changed_by) return std::format("<@{}>", change.changed_by->str());
    // An imported row's author was a guess, so there is nothing honest to
    // show. A row the bot watched happen and could not attribute is genuinely
    // unknown, which is worth saying (plan §8.1).
    return change.source == nickname_source::imported ? std::string{} : "unknown";
}

auto describe_change(const nickname_change& change) -> std::string {
    // Discord renders <t:seconds:f> in the reader's own timezone, which is the
    // only way one stored instant reads correctly for everybody.
    std::string line = std::format("**{}** — <t:{}:f>", show_nickname(change.nickname), to_unix(change.changed_at));

    const std::string author = show_author(change);
    if (!author.empty()) line += std::format(" by {}", author);
    return line;
}

auto render_history_text(std::span<const nickname_change> history, std::string_view who) -> std::string {
    std::string text = std::format("Nickname history for {}\n", who);
    text += std::format("{} entr{}, newest first. Times are UTC.\n\n", history.size(), history.size() == 1 ? "y" : "ies");

    for (const nickname_change& change : history) {
        const auto when = std::chrono::floor<std::chrono::seconds>(change.changed_at);
        text += std::format("{:%Y-%m-%d %H:%M:%S}  {}", when, change.nickname.value_or("(cleared)"));

        if (change.changed_by) {
            text += std::format("  (by {})", change.changed_by->str());
        } else if (change.source != nickname_source::imported) {
            text += "  (by unknown)";
        }
        text += '\n';
    }
    return text;
}

// --------------------------------------------------------------------------

auto pending_nicknames::expect(dpp::snowflake guild_id, dpp::snowflake user_id, const std::optional<std::string>& nickname,
                               std::chrono::system_clock::time_point now) -> void {
    const std::scoped_lock guard(mutex_);

    // Expired entries are cleared here rather than on a timer: the list only
    // grows when somebody runs the command, so that is when it needs tidying.
    std::erase_if(expected_, [now](const expectation& old) { return old.expires_at <= now; });

    expected_.push_back({.guild_id = guild_id, .user_id = user_id, .nickname = nickname, .expires_at = now + pending_nickname_ttl});
}

auto pending_nicknames::claim(dpp::snowflake guild_id, dpp::snowflake user_id, const std::optional<std::string>& nickname,
                              std::chrono::system_clock::time_point now) -> bool {
    const std::scoped_lock guard(mutex_);

    const auto found = std::ranges::find_if(expected_, [&](const expectation& waiting) {
        return waiting.guild_id == guild_id && waiting.user_id == user_id && waiting.nickname == nickname && waiting.expires_at > now;
    });
    if (found == expected_.end()) return false;

    // Consumed, so two identical changes in a row are recorded as two changes.
    expected_.erase(found);
    return true;
}

auto pending_nicknames::forget(dpp::snowflake guild_id, dpp::snowflake user_id, const std::optional<std::string>& nickname) -> void {
    const std::scoped_lock guard(mutex_);

    const auto found = std::ranges::find_if(expected_, [&](const expectation& waiting) {
        return waiting.guild_id == guild_id && waiting.user_id == user_id && waiting.nickname == nickname;
    });
    if (found != expected_.end()) expected_.erase(found);
}

auto pending_nicknames::size() const -> std::size_t {
    const std::scoped_lock guard(mutex_);
    return expected_.size();
}

// --------------------------------------------------------------------------

auto nickname_store::record(const nickname_change& change) -> std::int64_t {
    const auto guard = db_->lock();

    db_->prepare(std::format("INSERT INTO nickname_history ({}) VALUES (NULL, ?, ?, ?, ?, ?, ?, ?)", row_columns), change.guild_id,
                 change.user_id, change.nickname, change.changed_at, change.changed_by, to_string(change.source),
                 text_or_null(change.imported_raw))
        .run();

    return db_->last_insert_rowid();
}

auto nickname_store::history(dpp::snowflake guild_id, dpp::snowflake user_id) const -> std::vector<nickname_change> {
    const auto guard = db_->lock();

    // id breaks ties, so two changes recorded in the same second still read in
    // the order they happened.
    auto query = db_->prepare(std::format("SELECT {} FROM nickname_history WHERE guild_id = ? AND user_id = ? "
                                          "ORDER BY changed_at DESC, id DESC",
                                          row_columns),
                              guild_id, user_id);

    std::vector<nickname_change> found;
    while (query.step()) {
        found.push_back(read_row(query));
    }
    return found;
}

auto nickname_store::latest(dpp::snowflake guild_id, dpp::snowflake user_id) const -> std::optional<nickname_change> {
    const auto guard = db_->lock();

    auto query = db_->prepare(std::format("SELECT {} FROM nickname_history WHERE guild_id = ? AND user_id = ? "
                                          "ORDER BY changed_at DESC, id DESC LIMIT 1",
                                          row_columns),
                              guild_id, user_id);

    return query.step() ? std::optional(read_row(query)) : std::nullopt;
}

auto nickname_store::find(std::int64_t id) const -> std::optional<nickname_change> {
    const auto guard = db_->lock();

    auto query = db_->prepare(std::format("SELECT {} FROM nickname_history WHERE id = ?", row_columns), id);
    return query.step() ? std::optional(read_row(query)) : std::nullopt;
}

auto nickname_store::unattributed(dpp::snowflake guild_id, dpp::snowflake user_id, const std::optional<std::string>& nickname,
                                  std::chrono::system_clock::time_point now, std::chrono::seconds window) const
    -> std::optional<nickname_change> {
    const auto guard = db_->lock();

    auto query = db_->prepare(std::format("SELECT {} FROM nickname_history "
                                          "WHERE guild_id = ? AND user_id = ? AND changed_by IS NULL AND changed_at >= ? "
                                          "ORDER BY changed_at DESC, id DESC",
                                          row_columns),
                              guild_id, user_id, now - window);

    while (query.step()) {
        nickname_change candidate = read_row(query);
        if (describes(candidate, user_id, nickname)) return candidate;
    }
    return std::nullopt;
}

auto nickname_store::attribute(std::int64_t id, dpp::snowflake changed_by, nickname_source source) -> bool {
    const auto guard = db_->lock();

    // `changed_by IS NULL` sits in the statement rather than in a read first,
    // so two audit entries racing for the same row cannot both win.
    db_->prepare("UPDATE nickname_history SET changed_by = ?, source = ? WHERE id = ? AND changed_by IS NULL", changed_by,
                 to_string(source), id)
        .run();

    return db_->changes() > 0;
}

auto nickname_store::already_recorded(dpp::snowflake guild_id, dpp::snowflake user_id, const std::optional<std::string>& nickname,
                                      std::chrono::system_clock::time_point at) const -> bool {
    const auto guard = db_->lock();

    // `IS` rather than `=`, so a cleared nickname matches a cleared nickname:
    // NULL = NULL is never true in SQL.
    auto query = db_->prepare("SELECT 1 FROM nickname_history WHERE guild_id = ? AND user_id = ? AND nickname IS ? AND changed_at = ?",
                              guild_id, user_id, nickname, at);

    return query.step();
}

auto nickname_store::remove(std::int64_t id) -> bool {
    const auto guard = db_->lock();

    db_->prepare("DELETE FROM nickname_history WHERE id = ?", id).run();
    return db_->changes() > 0;
}

auto nickname_store::count(dpp::snowflake guild_id, dpp::snowflake user_id) const -> std::size_t {
    const auto guard = db_->lock();

    auto query = db_->prepare("SELECT COUNT(*) FROM nickname_history WHERE guild_id = ? AND user_id = ?", guild_id, user_id);

    return query.step() ? static_cast<std::size_t>(query.get<std::int64_t>(0)) : 0;
}

} // namespace latibot::events
