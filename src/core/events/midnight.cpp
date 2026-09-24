#include "core/events/midnight.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/ports/clock.hpp"
#include "core/util/log.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace latibot::events {
namespace {

/// Every column of an entry, in the order `read_row` expects.
constexpr std::string_view row_columns = "id, guild_id, channel_id, timezone, message, enabled, last_fired_date";

midnight_entry read_row(const db::statement& row) {
    midnight_entry entry;
    entry.id = row.get<std::int64_t>(0);
    entry.guild_id = dpp::snowflake(row.get<std::uint64_t>(1));
    entry.channel_id = dpp::snowflake(row.get<std::uint64_t>(2));
    entry.timezone = row.get<std::string>(3);
    entry.message = row.get<std::string>(4);
    entry.enabled = row.get<bool>(5);
    entry.last_fired_date = row.get<std::optional<std::string>>(6).value_or(std::string{});
    return entry;
}

std::string lowercased(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(),
                           [](char letter) { return static_cast<char>(std::tolower(static_cast<unsigned char>(letter))); });
    return result;
}

} // namespace

std::optional<local_reading> read_local(std::string_view timezone, std::chrono::system_clock::time_point now) {
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

bool due(const midnight_entry& entry, std::chrono::system_clock::time_point now) {
    if (!entry.enabled) {
        return false;
    }

    const auto local = read_local(entry.timezone, now);
    if (!local) {
        // A zone this machine does not know. Saying nothing beats posting at
        // the wrong time, and the command refuses unknown zones anyway.
        return false;
    }

    // Comparing the local date against the one saved is what survives a
    // suspend, a clock jump and a restart alike (plan v4 §10).
    return local->since_midnight >= midnight_grace && local->date != entry.last_fired_date;
}

std::string already_posted_today(std::string_view timezone, std::chrono::system_clock::time_point now) {
    const auto local = read_local(timezone, now);
    return local ? local->date : std::string{};
}

bool is_known_timezone(std::string_view name) {
    try {
        return std::chrono::locate_zone(name) != nullptr;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<std::string> matching_timezones(std::string_view typed, std::size_t limit) {
    std::vector<std::string> found;
    if (limit == 0) {
        return found;
    }

    const std::string wanted = lowercased(typed);

    try {
        for (const std::chrono::time_zone& zone : std::chrono::get_tzdb().zones) {
            const std::string name(zone.name());
            if (!wanted.empty() && lowercased(name).find(wanted) == std::string::npos) {
                continue;
            }

            found.push_back(name);
            if (found.size() == limit) {
                break;
            }
        }
    } catch (const std::exception&) {
        // No timezone database. An empty list reads as "no matches", which is
        // the truth from here.
        return {};
    }

    return found;
}

// --------------------------------------------------------------------------

std::vector<midnight_entry> midnight_store::for_guild(dpp::snowflake guild_id) const {
    const auto guard = db_->lock();

    auto query = db_->prepare(std::format("SELECT {} FROM midnight_messages WHERE guild_id = ? ORDER BY id", row_columns),
                              static_cast<std::uint64_t>(guild_id));

    std::vector<midnight_entry> found;
    while (query.step()) {
        found.push_back(read_row(query));
    }
    return found;
}

std::vector<midnight_entry> midnight_store::enabled() const {
    const auto guard = db_->lock();

    auto query = db_->prepare(std::format("SELECT {} FROM midnight_messages WHERE enabled != 0 ORDER BY id", row_columns));

    std::vector<midnight_entry> found;
    while (query.step()) {
        found.push_back(read_row(query));
    }
    return found;
}

std::optional<midnight_entry> midnight_store::find(std::int64_t id, dpp::snowflake guild_id) const {
    const auto guard = db_->lock();

    auto query = db_->prepare(std::format("SELECT {} FROM midnight_messages WHERE id = ? AND guild_id = ?", row_columns), id,
                              static_cast<std::uint64_t>(guild_id));

    return query.step() ? std::optional(read_row(query)) : std::nullopt;
}

std::int64_t midnight_store::add(const midnight_entry& entry) {
    const auto guard = db_->lock();

    db_->prepare(
           "INSERT INTO midnight_messages (guild_id, channel_id, timezone, message, enabled, last_fired_date) "
           "VALUES (?, ?, ?, ?, ?, ?)",
           static_cast<std::uint64_t>(entry.guild_id), static_cast<std::uint64_t>(entry.channel_id), entry.timezone, entry.message,
           entry.enabled, entry.last_fired_date.empty() ? std::optional<std::string>{} : std::optional(entry.last_fired_date))
        .run();

    return db_->last_insert_rowid();
}

bool midnight_store::update(const midnight_entry& entry) {
    const auto guard = db_->lock();

    db_->prepare("UPDATE midnight_messages SET channel_id = ?, timezone = ?, message = ?, enabled = ? WHERE id = ? AND guild_id = ?",
                 static_cast<std::uint64_t>(entry.channel_id), entry.timezone, entry.message, entry.enabled, entry.id,
                 static_cast<std::uint64_t>(entry.guild_id))
        .run();

    return db_->changes() > 0;
}

bool midnight_store::remove(std::int64_t id, dpp::snowflake guild_id) {
    const auto guard = db_->lock();

    db_->prepare("DELETE FROM midnight_messages WHERE id = ? AND guild_id = ?", id, static_cast<std::uint64_t>(guild_id)).run();
    return db_->changes() > 0;
}

bool midnight_store::mark_fired(std::int64_t id, std::string_view date) {
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

std::vector<action> midnight_scheduler::tick() {
    const auto now = clock_->now();

    std::vector<action> posts;
    for (const midnight_entry& entry : store_->enabled()) {
        if (!due(entry, now)) {
            continue;
        }

        const auto local = read_local(entry.timezone, now);
        if (!local) {
            continue;
        }

        // Claimed before it is posted: a crash in between costs one message,
        // where the other order would repeat it every thirty seconds.
        if (!store_->mark_fired(entry.id, local->date)) {
            util::log().debug("midnight message {} was already posted for {}", entry.id, local->date);
            continue;
        }

        util::log().info("posting midnight message {} in channel {} for {} in {}", entry.id, entry.channel_id.str(), local->date,
                         entry.timezone);
        posts.emplace_back(send_message{.channel_id = entry.channel_id, .content = entry.message});
    }

    return posts;
}

} // namespace latibot::events
