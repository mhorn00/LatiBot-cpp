#pragma once

#include "core/events/nicknames.hpp"

#include <dpp/snowflake.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::events {

/// The timezone the Java bot's timestamps were written in.
///
/// They are local wall-clock with no zone recorded, from a machine in US
/// Central. `America/Chicago` carries the full daylight-saving history,
/// including the 2007 rule change (plan v4 §8.3).
inline constexpr std::string_view imported_timezone = "America/Chicago";

/// Reads one of the Java bot's timestamps as an instant.
///
/// The text is `yyyy-MM-dd HH:mm:ss` in `imported_timezone`. Two dates a year
/// need a decision rather than a conversion:
///
/// - the ambiguous hour each November, where the same wall-clock time happens
///   twice: the earlier one is chosen, and it is at most an hour wrong;
/// - the hour each March that never happens: shifted forward by an hour, so
///   02:30 becomes 03:30 rather than collapsing onto 03:00 with every other
///   time in the gap.
///
/// Nothing when the text is not a timestamp at all.
[[nodiscard]] std::optional<std::chrono::system_clock::time_point> central_time_to_utc(std::string_view local_text);

/// The author an imported entry can be trusted with.
///
/// The Java bot wrote the member's own id whenever it could not identify who
/// made a change ("assuming self"), so that value says nothing at all. A
/// different id could only have come from its `/nickname`, which is the one
/// case it did know, so that one is kept.
[[nodiscard]] std::optional<dpp::snowflake> imported_author(dpp::snowflake user_id, dpp::snowflake changed_by);

/// What reading `nicknames.json` produced.
struct import_report {
    std::vector<nickname_change> entries;

    /// The Java bot's last known username for each member, for the log: the
    /// history itself stores ids and resolves names when it is shown.
    std::vector<std::pair<dpp::snowflake, std::string>> members;

    /// Anything unreadable, named rather than dropped silently.
    std::vector<std::string> problems;
};

/// Reads the Java bot's `nicknames.json`.
///
/// Pure, so the timezone conversion and every malformed shape can be tested
/// without a file. An entry that cannot be read is reported and skipped; one
/// bad row does not lose the rest.
[[nodiscard]] import_report read_nicknames_json(std::string_view text);

/// Writes everything in `report` that is not already recorded.
///
/// Idempotent, so importing the same file twice adds nothing the second time
/// and the file can simply be left where it is. Returns how many rows were
/// added.
int import_nicknames(nickname_store& store, const import_report& report);

/// Reads and imports the file, if it is there. Returns how many rows were
/// added, and nothing at all when there is no file to read.
[[nodiscard]] std::optional<int> import_nicknames_file(nickname_store& store, const std::filesystem::path& path);

} // namespace latibot::events
