#pragma once

#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::events {

/// Where a history row's attribution came from, which is also how far it can
/// be trusted (plan §8.1).
enum class nickname_source : std::uint8_t {
    /// `/nickname`: the invoker is known for certain.
    command,
    /// Discord's audit log named the actor.
    audit_log,
    /// Seen on the gateway, with nobody claiming it yet.
    seen,
    /// Noticed at startup, after the fact. Nobody can be named.
    startup,
    /// Carried over from the Java bot's `nicknames.json`.
    imported,
};

[[nodiscard]] std::string_view to_string(nickname_source source) noexcept;
[[nodiscard]] std::optional<nickname_source> nickname_source_from_string(std::string_view name);

/// One nickname a member had, and what is known about how it got there.
struct nickname_change {
    std::int64_t id = 0;
    dpp::snowflake guild_id;
    dpp::snowflake user_id;

    /// Empty when the nickname was cleared, which is not the same as "".
    std::optional<std::string> nickname;

    std::chrono::system_clock::time_point changed_at;

    /// Empty when nothing could attribute it. The Java bot guessed "they did
    /// it themselves" here, which was usually wrong (plan §8.1).
    std::optional<dpp::snowflake> changed_by;

    nickname_source source = nickname_source::seen;

    /// The original timestamp text, for imported rows only, so the timezone
    /// conversion can be redone (plan §8.3).
    std::string imported_raw;
};

// --------------------------------------------------------------------------
// Decisions
// --------------------------------------------------------------------------

/// Whether `current` differs from the last nickname recorded for a member.
///
/// The gateway updates DPP's cached member before the handler runs, so what
/// somebody was called a moment ago is only knowable from our own history.
/// That makes this the same question at startup as it is mid-run, which is
/// why reconciliation needs no separate rule (plan §8.4).
[[nodiscard]] bool is_new_nickname(const std::optional<nickname_change>& latest, const std::optional<std::string>& current);

/// Whether an audit entry is about this row: same member, same resulting
/// nickname.
///
/// Discord writes the audit entry and sends the member update separately, so
/// they are matched on what they say rather than on any shared identifier.
[[nodiscard]] bool describes(const nickname_change& change, dpp::snowflake target, const std::optional<std::string>& new_nickname);

/// The nickname an audit log change carries.
///
/// DPP hands the value over as dumped JSON rather than as text, so a nickname
/// arrives quoted and a cleared one arrives as `null`. Returns nothing for
/// both "cleared" and "unreadable", which are the same thing to a caller that
/// is only trying to match a row it already wrote.
[[nodiscard]] std::optional<std::string> audit_nickname(std::string_view dumped_json);

/// Whether `actor` may be written into a row that has no author yet.
///
/// Never the bot: when the bot calls the API it is what Discord records, and
/// overwriting a known invoker with "LatiBot" is how the Java version lost
/// the only attribution that was ever certain (plan §8.1).
[[nodiscard]] bool may_attribute(const nickname_change& change, dpp::snowflake actor, dpp::snowflake self);

// --------------------------------------------------------------------------
// Display
// --------------------------------------------------------------------------

/// A nickname as it is shown: the text, or *(cleared)*.
[[nodiscard]] std::string show_nickname(const std::optional<std::string>& nickname);

/// Who a row is attributed to, as it is shown.
///
/// A mention when somebody is named, "unknown" when nothing could attribute
/// it, and nothing at all for imported rows, whose author the Java bot mostly
/// guessed. Mentions in an embed resolve to a name without pinging anyone.
[[nodiscard]] std::string show_author(const nickname_change& change);

/// One line of `/nicknames`, using Discord's own timestamp markup so each
/// reader sees it in their own timezone.
[[nodiscard]] std::string describe_change(const nickname_change& change);

/// The whole history as plain text, for the attachment a long history gets
/// instead of pages. Times are UTC, since a file has no reader to localise for.
[[nodiscard]] std::string render_history_text(std::span<const nickname_change> history, std::string_view who);

/// How long a change the bot just made stays claimable.
///
/// Long enough to cover a slow gateway, short enough that an unrelated change
/// to the same nickname later is not mistaken for it (plan §8.1).
inline constexpr std::chrono::seconds pending_nickname_ttl{30};

/// How long to wait for Discord's audit entry before going and asking.
///
/// The gateway entry normally arrives within a second, so this is the cover
/// for a reconnect or a dropped event rather than the usual path
/// (plan §8.1). It is comfortably inside `pending_nickname_ttl`, so a row
/// the fallback finds is still one the window would accept.
inline constexpr std::chrono::seconds audit_fallback_delay{10};

/// Changes `/nickname` has just made, so the member update that follows is not
/// recorded a second time.
///
/// Shared between a command handler and a gateway event, which run on
/// different threads, so it locks (plan §2.5).
class pending_nicknames {
public:
    void expect(dpp::snowflake guild_id, dpp::snowflake user_id, const std::optional<std::string>& nickname,
                std::chrono::system_clock::time_point now);

    /// Consumes a matching expectation. True when this change was ours, which
    /// means it is already in the history with the invoker against it.
    bool claim(dpp::snowflake guild_id, dpp::snowflake user_id, const std::optional<std::string>& nickname,
               std::chrono::system_clock::time_point now);

    /// Drops an expectation whose change never arrived, so a `/nickname` that
    /// Discord refused does not silently swallow the next matching change.
    void forget(dpp::snowflake guild_id, dpp::snowflake user_id, const std::optional<std::string>& nickname);

    [[nodiscard]] std::size_t size() const;

private:
    struct expectation {
        dpp::snowflake guild_id;
        dpp::snowflake user_id;
        std::optional<std::string> nickname;
        std::chrono::system_clock::time_point expires_at;
    };

    mutable std::mutex mutex_;
    std::vector<expectation> expected_;
};

// --------------------------------------------------------------------------
// Storage
// --------------------------------------------------------------------------

/// Nickname history, in SQLite.
class nickname_store {
public:
    explicit nickname_store(db::database& db) : db_(&db) {}

    /// Records a change and returns the new row id.
    std::int64_t record(const nickname_change& change);

    /// Everything recorded for one member, newest first.
    [[nodiscard]] std::vector<nickname_change> history(dpp::snowflake guild_id, dpp::snowflake user_id) const;

    /// The most recent row for a member, which is what a new sighting is
    /// compared against.
    [[nodiscard]] std::optional<nickname_change> latest(dpp::snowflake guild_id, dpp::snowflake user_id) const;

    [[nodiscard]] std::optional<nickname_change> find(std::int64_t id) const;

    /// The newest row for this member that still has no author, if it matches
    /// `nickname` and was recorded within `window` of `now`.
    ///
    /// The window is what stops an audit entry from attaching itself to a
    /// change from last week that happens to have the same nickname.
    [[nodiscard]] std::optional<nickname_change> unattributed(dpp::snowflake guild_id, dpp::snowflake user_id,
                                                              const std::optional<std::string>& nickname,
                                                              std::chrono::system_clock::time_point now, std::chrono::seconds window) const;

    /// Fills in the author of a row that has none. False when the row is gone
    /// or somebody has already been named.
    bool attribute(std::int64_t id, dpp::snowflake changed_by, nickname_source source);

    /// Whether this exact row is already there.
    ///
    /// What makes importing the Java bot's file idempotent, so it can simply
    /// be left where it is rather than having to be moved after one run.
    [[nodiscard]] bool already_recorded(dpp::snowflake guild_id, dpp::snowflake user_id, const std::optional<std::string>& nickname,
                                        std::chrono::system_clock::time_point at) const;

    /// Removes a row, for when a change the bot recorded did not go through.
    bool remove(std::int64_t id);

    /// Every member with history in this guild.
    [[nodiscard]] std::vector<dpp::snowflake> members(dpp::snowflake guild_id) const;

    [[nodiscard]] std::size_t count(dpp::snowflake guild_id, dpp::snowflake user_id) const;

private:
    db::database* db_;
};

} // namespace latibot::events
