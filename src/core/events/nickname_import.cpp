#include "core/events/nickname_import.hpp"

#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <dpp/json.h>

#include <format>
#include <fstream>
#include <sstream>

namespace latibot::events {
namespace {

using json = nlohmann::json;

/// An id the Java bot wrote as a string, or nothing when it is not one.
auto read_id(const json& value) -> std::optional<dpp::snowflake> {
    if (!value.is_string()) {
        return std::nullopt;
    }
    return util::parse_snowflake(value.get<std::string>());
}

/// One `nicknames` element: `{nickname, changedById, datetime}`.
auto read_entry(const json& element, dpp::snowflake guild_id, dpp::snowflake user_id, import_report& report) -> void {
    if (!element.is_object() || !element.contains("nickname") || !element.contains("datetime")) {
        report.problems.push_back(std::format("{} has an entry with no nickname or no time", user_id.str()));
        return;
    }

    const auto& nickname = element.at("nickname");
    const auto& datetime = element.at("datetime");
    if (!nickname.is_string() || !datetime.is_string()) {
        report.problems.push_back(std::format("{} has an entry whose nickname or time is not text", user_id.str()));
        return;
    }

    const std::string raw = datetime.get<std::string>();
    const auto when = central_time_to_utc(raw);
    if (!when) {
        report.problems.push_back(std::format("{} has an entry timed \"{}\", which is not a date", user_id.str(), raw));
        return;
    }

    // A cleared nickname was stored as an empty string there, and is stored as
    // nothing here (plan §8.2).
    const std::string text = nickname.get<std::string>();

    std::optional<dpp::snowflake> author;
    if (element.contains("changedById")) {
        if (const auto claimed = read_id(element.at("changedById"))) {
            author = imported_author(user_id, *claimed);
        }
    }

    report.entries.push_back({.guild_id = guild_id,
                              .user_id = user_id,
                              .nickname = text.empty() ? std::nullopt : std::optional(text),
                              .changed_at = *when,
                              .changed_by = author,
                              .source = nickname_source::imported,
                              .imported_raw = raw});
}

/// One member's record: `{guild, member: {id, username}, nicknames: [...]}`.
auto read_member(const json& record, dpp::snowflake guild_id, import_report& report) -> void {
    if (!record.is_object() || !record.contains("member") || !record.contains("nicknames")) {
        report.problems.push_back(std::format("guild {} has a record with no member or no nicknames", guild_id.str()));
        return;
    }

    const auto& member = record.at("member");
    if (!member.is_object() || !member.contains("id")) {
        report.problems.push_back(std::format("guild {} has a record whose member has no id", guild_id.str()));
        return;
    }

    const auto user_id = read_id(member.at("id"));
    if (!user_id) {
        report.problems.push_back(std::format("guild {} has a record whose member id is not an id", guild_id.str()));
        return;
    }

    std::string username;
    if (member.contains("username") && member.at("username").is_string()) {
        username = member.at("username").get<std::string>();
    }
    report.members.emplace_back(*user_id, std::move(username));

    const auto& entries = record.at("nicknames");
    if (!entries.is_array()) {
        report.problems.push_back(std::format("{} has a nicknames field that is not a list", user_id->str()));
        return;
    }

    for (const auto& element : entries) {
        read_entry(element, guild_id, *user_id, report);
    }
}

} // namespace

auto central_time_to_utc(std::string_view local_text) -> std::optional<std::chrono::system_clock::time_point> {
    std::istringstream stream{std::string(local_text)};
    std::chrono::local_seconds local{};
    stream >> std::chrono::parse("%Y-%m-%d %H:%M:%S", local);
    if (stream.fail()) {
        return std::nullopt;
    }

    const std::chrono::time_zone* zone = nullptr;
    try {
        zone = std::chrono::locate_zone(imported_timezone);
    } catch (const std::exception&) {
        // No timezone database. Nothing sensible is left to do with a
        // wall-clock time, so say so rather than inventing an offset.
        return std::nullopt;
    }

    try {
        return zone->to_sys(local);
    } catch (const std::chrono::ambiguous_local_time&) {
        // The hour that happens twice each November. There is no way to know
        // which was meant, and either is at most an hour out.
        return zone->to_sys(local, std::chrono::choose::earliest);
    } catch (const std::chrono::nonexistent_local_time&) {
        // The hour that never happens each March. Shifting forward keeps the
        // minutes, where choosing the transition would flatten every time in
        // the gap onto the same instant.
        return zone->to_sys(local + std::chrono::hours{1});
    }
}

auto imported_author(dpp::snowflake user_id, dpp::snowflake changed_by) -> std::optional<dpp::snowflake> {
    if (changed_by.empty() || changed_by == user_id) {
        return std::nullopt;
    }
    return changed_by;
}

auto read_nicknames_json(std::string_view text) -> import_report {
    import_report report;

    const json parsed = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        report.problems.emplace_back("the file is not a JSON object");
        return report;
    }

    // Keyed by guild id, each holding a list of members' records.
    for (const auto& [key, records] : parsed.items()) {
        const auto guild_id = read_id(key);
        if (!guild_id) {
            report.problems.push_back(std::format("\"{}\" is not a guild id", key));
            continue;
        }
        if (!records.is_array()) {
            report.problems.push_back(std::format("guild {} does not hold a list", key));
            continue;
        }

        for (const auto& record : records) {
            read_member(record, *guild_id, report);
        }
    }

    return report;
}

auto import_nicknames(nickname_store& store, const import_report& report) -> int {
    int added = 0;
    for (const nickname_change& entry : report.entries) {
        if (store.already_recorded(entry.guild_id, entry.user_id, entry.nickname, entry.changed_at)) {
            continue;
        }
        store.record(entry);
        ++added;
    }
    return added;
}

auto import_nicknames_file(nickname_store& store, const std::filesystem::path& path) -> std::optional<int> {
    const std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }

    std::ostringstream contents;
    contents << file.rdbuf();

    const import_report report = read_nicknames_json(contents.str());
    for (const std::string& problem : report.problems) {
        util::log().warn("{}: {}", path.generic_string(), problem);
    }

    const int added = import_nicknames(store, report);
    util::log().debug("{}: {} member(s), {} entr{} read, {} new", path.generic_string(), report.members.size(), report.entries.size(),
                      report.entries.size() == 1 ? "y" : "ies", added);
    return added;
}

} // namespace latibot::events
