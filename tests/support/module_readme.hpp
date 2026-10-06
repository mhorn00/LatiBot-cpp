#pragma once

#include "core/commands/registry.hpp"
#include "core/db/database.hpp"
#include "core/db/statement.hpp"

#include <dpp/dpp.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace latibot::testing {

/// The names in one row of a module README's table of what it owns
/// (docs/modules/Module_Plan_Final.md §11): every `backticked` word in the
/// row whose first cell is `row`, such as "Commands". Empty when there is no
/// such row, which a test then fails on as a mismatch.
///
/// The README is what a reviewer reads to learn what a module adds, so its
/// test compares these with what the module registers: the README cannot
/// quietly fall behind the code.
inline auto readme_row(const std::filesystem::path& readme, std::string_view row) -> std::set<std::string> {
    std::ifstream file(readme);
    std::set<std::string> names;
    const std::string first_cell = "| " + std::string(row) + " |";
    std::string line;
    while (std::getline(file, line)) {
        if (!line.starts_with(first_cell)) continue;
        const std::string rest = line.substr(first_cell.size());
        std::size_t at = 0;
        while ((at = rest.find('`', at)) != std::string::npos) {
            const std::size_t end = rest.find('`', at + 1);
            if (end == std::string::npos) break;
            names.insert(rest.substr(at + 1, end - at - 1));
            at = end + 1;
        }
        break;
    }
    return names;
}

/// Every slash command in `registry`, as `/name`.
inline auto command_names(const commands::registry& registry) -> std::set<std::string> {
    std::set<std::string> names;
    for (const dpp::slashcommand& each : registry.build_all(dpp::snowflake{1})) {
        names.insert("/" + each.name);
    }
    return names;
}

/// Every table, view and virtual table in `db` but those in `except`, the
/// tables that were there before a module created its own.
inline auto table_names(db::database& db, const std::set<std::string>& except = {}) -> std::set<std::string> {
    std::set<std::string> names;
    auto query = db.prepare("SELECT name FROM sqlite_master WHERE type IN ('table', 'view') AND name NOT LIKE 'sqlite_%'");
    while (query.step()) {
        auto name = query.get<std::string>(0);
        if (!except.contains(name)) names.insert(std::move(name));
    }
    return names;
}

} // namespace latibot::testing
