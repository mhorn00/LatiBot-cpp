#include "documents.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <format>
#include <vector>

namespace latibot::llm {
namespace {

constexpr std::string_view default_personality =
    "You're LatiBot, a member of this Discord server who happens to be a bot. You're relaxed, a bit dry, and happy to joke "
    "around. Keep replies short and conversational, usually a sentence or two, the way a person types in chat. Lowercase is "
    "fine. Don't use headings or bullet lists unless someone asks for something list-shaped.";

constexpr std::string_view default_trigger_style =
    "Nobody asked you anything: something in the conversation caught your attention. Chime in with one short line, in "
    "character, as if you happened to be reading along. Never mention that something triggered you, and don't explain "
    "yourself.";

/// How many unchanged lines a diff keeps either side of a change.
constexpr std::size_t diff_context = 1;

/// Past this many cells the table would be too big to be worth building for
/// a chat message; the diff says so instead.
constexpr std::size_t diff_cell_limit = 4'000'000;

auto read_version(db::statement& row) -> document_version {
    return {.version = row.get<int>(0),
            .content = row.get<std::string>(1),
            .edited_by = row.get<dpp::snowflake>(2),
            .edited_at = row.get<std::chrono::sys_seconds>(3),
            .note = row.get<std::optional<std::string>>(4).value_or(std::string{})};
}

} // namespace

auto to_string(document_kind kind) noexcept -> std::string_view {
    switch (kind) {
    case document_kind::personality:
        return "personality";
    case document_kind::system:
        return "system";
    case document_kind::trigger_style:
        break;
    }
    return "trigger_style";
}

auto document_kind_from_string(std::string_view name) -> std::optional<document_kind> {
    const std::string key = util::to_lower(util::trim(name));
    if (key == "personality") return document_kind::personality;
    if (key == "system") return document_kind::system;
    if (key == "trigger_style" || key == "style") return document_kind::trigger_style;
    return std::nullopt;
}

auto default_document(document_kind kind) noexcept -> std::string_view {
    switch (kind) {
    case document_kind::personality:
        return default_personality;
    case document_kind::system:
        // Nothing: it is where admins put rules of their own.
        return {};
    case document_kind::trigger_style:
        break;
    }
    return default_trigger_style;
}

auto estimate_tokens(std::string_view text) noexcept -> std::size_t {
    return (util::character_count(text) + 3) / 4;
}

// --------------------------------------------------------------------------

auto document_store::current(dpp::snowflake guild_id, document_kind kind) const -> std::optional<document_version> {
    auto query = db_->prepare(
        "SELECT version, content, edited_by, edited_at, note FROM llm_documents WHERE guild_id = ? AND kind = ? "
        "ORDER BY version DESC LIMIT 1",
        guild_id, to_string(kind));
    if (!query.step()) return std::nullopt;
    return read_version(query);
}

auto document_store::text(dpp::snowflake guild_id, document_kind kind) const -> std::string {
    const auto found = current(guild_id, kind);
    return found ? found->content : std::string(default_document(kind));
}

auto document_store::save(dpp::snowflake guild_id, document_kind kind, std::string_view content, dpp::snowflake edited_by,
                          std::chrono::sys_seconds at, std::string_view note) -> int {
    // One transaction, so two saves at once cannot both take the same number.
    db::transaction tx(*db_);

    int next = 1;
    {
        auto newest =
            db_->prepare("SELECT COALESCE(MAX(version), 0) FROM llm_documents WHERE guild_id = ? AND kind = ?", guild_id, to_string(kind));
        if (newest.step()) next = newest.get<int>(0) + 1;
    }

    db_->prepare("INSERT INTO llm_documents (guild_id, kind, version, content, edited_by, edited_at, note) VALUES (?, ?, ?, ?, ?, ?, ?)",
                 guild_id, to_string(kind), next, content, edited_by, at,
                 note.empty() ? std::optional<std::string_view>{} : std::optional<std::string_view>(note))
        .run();

    tx.commit();
    return next;
}

auto document_store::history(dpp::snowflake guild_id, document_kind kind) const -> std::vector<document_version> {
    std::vector<document_version> versions;
    auto query = db_->prepare(
        "SELECT version, content, edited_by, edited_at, note FROM llm_documents WHERE guild_id = ? AND kind = ? ORDER BY version DESC",
        guild_id, to_string(kind));
    while (query.step()) {
        versions.push_back(read_version(query));
    }
    return versions;
}

auto document_store::version(dpp::snowflake guild_id, document_kind kind, int number) const -> std::optional<document_version> {
    if (number == 0) {
        return document_version{
            .version = 0, .content = std::string(default_document(kind)), .edited_by = {}, .edited_at = {}, .note = "the default"};
    }

    auto query = db_->prepare(
        "SELECT version, content, edited_by, edited_at, note FROM llm_documents WHERE guild_id = ? AND kind = ? AND version = ?", guild_id,
        to_string(kind), number);
    if (!query.step()) return std::nullopt;
    return read_version(query);
}

auto document_store::revert(dpp::snowflake guild_id, document_kind kind, int number, dpp::snowflake edited_by, std::chrono::sys_seconds at)
    -> std::optional<int> {
    const auto old = version(guild_id, kind, number);
    if (!old) return std::nullopt;
    return save(guild_id, kind, old->content, edited_by, at, std::format("reverted to version {}", number));
}

// --------------------------------------------------------------------------

namespace {

/// One line of a diff: ' ' unchanged, '-' removed, '+' added.
struct diff_line {
    char mark;
    std::string_view text;
};

/// Every line of both texts, in order, marked by the longest common
/// subsequence of their lines.
auto walk_diff(const std::vector<std::string_view>& old_lines, const std::vector<std::string_view>& new_lines) -> std::vector<diff_line> {
    const std::size_t rows = old_lines.size();
    const std::size_t columns = new_lines.size();

    // Counted from the end, so the walk below can go forwards.
    std::vector<std::size_t> common((rows + 1) * (columns + 1), 0);
    const auto at = [&](std::size_t row, std::size_t column) -> std::size_t& { return common[(row * (columns + 1)) + column]; };
    for (std::size_t row = rows; row-- > 0;) {
        for (std::size_t column = columns; column-- > 0;) {
            at(row, column) =
                old_lines[row] == new_lines[column] ? at(row + 1, column + 1) + 1 : std::max(at(row + 1, column), at(row, column + 1));
        }
    }

    std::vector<diff_line> walked;
    std::size_t row = 0;
    std::size_t column = 0;
    while (row < rows || column < columns) {
        if (row < rows && column < columns && old_lines[row] == new_lines[column]) {
            walked.push_back({.mark = ' ', .text = old_lines[row]});
            ++row;
            ++column;
        } else if (row < rows && (column == columns || at(row + 1, column) >= at(row, column + 1))) {
            // Removals before additions, as a diff is usually read.
            walked.push_back({.mark = '-', .text = old_lines[row]});
            ++row;
        } else {
            walked.push_back({.mark = '+', .text = new_lines[column]});
            ++column;
        }
    }
    return walked;
}

/// The lines as text, unchanged ones kept only near a change, and each run
/// left out written as one "…" line.
auto render_diff(const std::vector<diff_line>& walked) -> std::string {
    std::vector<bool> keep(walked.size(), false);
    for (std::size_t index = 0; index < walked.size(); ++index) {
        if (walked[index].mark == ' ') continue;
        const std::size_t from = index >= diff_context ? index - diff_context : 0;
        const std::size_t to = std::min(walked.size() - 1, index + diff_context);
        std::fill(keep.begin() + static_cast<std::ptrdiff_t>(from), keep.begin() + static_cast<std::ptrdiff_t>(to) + 1, true);
    }

    std::string text;
    bool skipping = false;
    for (std::size_t index = 0; index < walked.size(); ++index) {
        if (!keep[index]) {
            if (!skipping) text += "  …\n";
            skipping = true;
            continue;
        }
        skipping = false;
        text += std::format("{} {}\n", walked[index].mark, walked[index].text);
    }
    return text;
}

} // namespace

auto diff_lines(std::string_view before, std::string_view after) -> std::string {
    if (before == after) return {};

    const std::vector<std::string_view> old_lines = util::lines(before);
    const std::vector<std::string_view> new_lines = util::lines(after);
    if ((old_lines.size() + 1) * (new_lines.size() + 1) > diff_cell_limit) return "the versions are too long to compare line by line\n";
    return render_diff(walk_diff(old_lines, new_lines));
}

} // namespace latibot::llm
