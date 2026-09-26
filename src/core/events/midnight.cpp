#include "core/events/midnight.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/ports/clock.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <format>

namespace latibot::events {
namespace {

/// Every column of an entry, in the order `read_row` expects.
constexpr std::string_view row_columns = "id, guild_id, channel_id, timezone, message, enabled, last_fired_date, message_flags";

auto read_row(const db::statement& row) -> midnight_entry {
    midnight_entry entry;
    entry.id = row.get<std::int64_t>(0);
    entry.guild_id = row.get<dpp::snowflake>(1);
    entry.channel_id = row.get<dpp::snowflake>(2);
    entry.timezone = row.get<std::string>(3);
    entry.message = row.get<std::string>(4);
    entry.enabled = row.get<bool>(5);
    entry.last_fired_date = row.get<std::optional<std::string>>(6).value_or(std::string{});
    entry.message_flags = discord::channel_flags(row.get<std::int64_t>(7));
    return entry;
}

} // namespace

auto read_local(std::string_view timezone, std::chrono::system_clock::time_point now) -> std::optional<local_reading> {
    const std::chrono::time_zone* zone = nullptr;
    try {
        zone = std::chrono::locate_zone(timezone);
    } catch (const std::exception&) {
        return std::nullopt;
    }

    const auto local = zone->to_local(std::chrono::floor<std::chrono::seconds>(now));
    const auto midnight = std::chrono::floor<std::chrono::days>(local);

    return local_reading{.date = std::format("{:%Y-%m-%d}", midnight),
                         .since_midnight = std::chrono::duration_cast<std::chrono::seconds>(local - midnight)};
}

auto verdict_for(const midnight_entry& entry, std::chrono::system_clock::time_point now) -> midnight_verdict {
    if (!entry.enabled) return midnight_verdict::wait;

    const auto local = read_local(entry.timezone, now);
    if (!local) {
        // A zone this machine does not know. Saying nothing beats posting at
        // the wrong time, and the command refuses unknown zones anyway.
        return midnight_verdict::wait;
    }

    // Comparing the local date against the one saved is what survives a
    // suspend, a clock jump and a restart alike (plan §10).
    if (local->date == entry.last_fired_date || local->since_midnight < midnight_grace) return midnight_verdict::wait;

    // A new day, but how new? Far enough past midnight and the bot cannot have
    // been running for it, and yesterday's midnight message over breakfast is
    // worse than no midnight message.
    //
    // Nothing is written down for a missed day, and nothing needs to be: the
    // time since midnight only grows, so the rest of the day answers `wait`
    // on its own and the next midnight starts clean.
    return local->since_midnight <= midnight_window ? midnight_verdict::post : midnight_verdict::missed;
}

auto already_posted_today(std::string_view timezone, std::chrono::system_clock::time_point now) -> std::string {
    const auto local = read_local(timezone, now);
    return local ? local->date : std::string{};
}

auto is_known_timezone(std::string_view name) -> bool {
    try {
        return std::chrono::locate_zone(name) != nullptr;
    } catch (const std::exception&) {
        return false;
    }
}

auto matching_timezones(std::string_view typed, std::size_t limit) -> std::vector<std::string> {
    std::vector<std::string> found;
    if (limit == 0) return found;

    const std::string wanted = util::to_lower(typed);

    try {
        for (const std::chrono::time_zone& zone : std::chrono::get_tzdb().zones) {
            const std::string name(zone.name());
            if (!wanted.empty() && util::to_lower(name).find(wanted) == std::string::npos) continue;

            found.push_back(name);
            if (found.size() == limit) break;
        }
    } catch (const std::exception&) {
        // No timezone database. An empty list reads as "no matches", which is
        // the truth from here.
        return {};
    }

    return found;
}

// --------------------------------------------------------------------------

auto midnight_store::for_guild(dpp::snowflake guild_id) const -> std::vector<midnight_entry> {
    const auto guard = db_->lock();

    auto query = db_->prepare(std::format("SELECT {} FROM midnight_messages WHERE guild_id = ? ORDER BY id", row_columns), guild_id);

    std::vector<midnight_entry> found;
    while (query.step()) {
        found.push_back(read_row(query));
    }
    return found;
}

auto midnight_store::enabled() const -> std::vector<midnight_entry> {
    const auto guard = db_->lock();

    auto query = db_->prepare(std::format("SELECT {} FROM midnight_messages WHERE enabled != 0 ORDER BY id", row_columns));

    std::vector<midnight_entry> found;
    while (query.step()) {
        found.push_back(read_row(query));
    }
    return found;
}

auto midnight_store::find(std::int64_t id, dpp::snowflake guild_id) const -> std::optional<midnight_entry> {
    const auto guard = db_->lock();

    auto query = db_->prepare(std::format("SELECT {} FROM midnight_messages WHERE id = ? AND guild_id = ?", row_columns), id, guild_id);

    return query.step() ? std::optional(read_row(query)) : std::nullopt;
}

auto midnight_store::add(const midnight_entry& entry) -> std::int64_t {
    const auto guard = db_->lock();

    db_->prepare(
           "INSERT INTO midnight_messages (guild_id, channel_id, timezone, message, enabled, last_fired_date, message_flags) "
           "VALUES (?, ?, ?, ?, ?, ?, ?)",
           entry.guild_id, entry.channel_id, entry.timezone, entry.message, entry.enabled,
           entry.last_fired_date.empty() ? std::optional<std::string>{} : std::optional(entry.last_fired_date),
           std::int64_t{discord::channel_flags(entry.message_flags)})
        .run();

    return db_->last_insert_rowid();
}

auto midnight_store::update(const midnight_entry& entry) -> bool {
    const auto guard = db_->lock();

    db_->prepare(
           "UPDATE midnight_messages SET channel_id = ?, timezone = ?, message = ?, enabled = ?, message_flags = ? "
           "WHERE id = ? AND guild_id = ?",
           entry.channel_id, entry.timezone, entry.message, entry.enabled, std::int64_t{discord::channel_flags(entry.message_flags)},
           entry.id, entry.guild_id)
        .run();

    return db_->changes() > 0;
}

auto midnight_store::remove(std::int64_t id, dpp::snowflake guild_id) -> bool {
    const auto guard = db_->lock();

    db_->prepare("DELETE FROM midnight_messages WHERE id = ? AND guild_id = ?", id, guild_id).run();
    return db_->changes() > 0;
}

auto midnight_store::mark_fired(std::int64_t id, std::string_view date) -> bool {
    const auto guard = db_->lock();

    // The date is part of the condition, so claiming a day is one statement
    // and two ticks cannot both win it.
    db_->prepare(
           "UPDATE midnight_messages SET last_fired_date = ? "
           "WHERE id = ? AND (last_fired_date IS NULL OR last_fired_date != ?)",
           date, id, date)
        .run();

    return db_->changes() > 0;
}

// --------------------------------------------------------------------------

auto midnight_scheduler::note_missed(const midnight_entry& entry, std::chrono::system_clock::time_point now) -> void {
    const auto local = read_local(entry.timezone, now);
    if (!local) return;

    std::string& reported = reported_misses_[entry.id];
    if (reported == local->date) return;
    reported = local->date;

    // Info rather than debug: a message that was meant to go out and did not
    // is exactly the thing somebody comes looking for the next morning.
    util::log().info("midnight message {} missed midnight on {} in {} (it is already {} there); waiting for the next one", entry.id,
                     local->date, entry.timezone, std::format("{:%H:%M}", local->since_midnight));
}

auto midnight_scheduler::tick() -> std::vector<action> {
    const auto now = clock_->now();

    // Every enabled entry, every tick: `verdict_for` decides from the entry's
    // own local date, so nothing needs scheduling and a restart loses
    // nothing. Only a `post` verdict goes on to claim the day.
    std::vector<action> posts;
    for (const midnight_entry& entry : store_->enabled()) {
        switch (verdict_for(entry, now)) {
        case midnight_verdict::wait:
            continue;
        case midnight_verdict::missed:
            note_missed(entry, now);
            continue;
        case midnight_verdict::post:
            break;
        }

        const auto local = read_local(entry.timezone, now);
        if (!local) continue;

        // Claimed before it is posted: a crash in between costs one message,
        // where the other order would repeat it every thirty seconds.
        //
        // A claim that fails is left for the next tick, and the rest go
        // ahead: the posts already claimed here are only sent if this
        // returns, so one entry's error must not cost the others theirs.
        try {
            if (!store_->mark_fired(entry.id, local->date)) {
                util::log().debug("midnight message {} was already posted for {}", entry.id, local->date);
                continue;
            }
        } catch (const std::exception& error) {
            util::log().error("could not claim {} for midnight message {}; trying again next tick: {}", local->date, entry.id,
                              error.what());
            continue;
        }

        util::log().info("posting midnight message {} in channel {} for {} in {}", entry.id, entry.channel_id, local->date, entry.timezone);
        posts.emplace_back(send_message{.channel_id = entry.channel_id,
                                        .content = entry.message,
                                        .flags = entry.message_flags,
                                        .what = std::format("midnight message {}", entry.id)});
    }

    return posts;
}

} // namespace latibot::events
