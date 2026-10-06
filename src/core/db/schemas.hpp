#pragma once

#include "core/db/schema_versions.hpp"

#include <span>

namespace latibot::db {

// Each module's schema, version 1 first (docs/modules/Module_Plan_Final.md
// §7.1). Version 1 is the module's tables exactly as migration 15 left
// them, with the columns that migrations added by ALTER TABLE last, where
// ALTER put them; the comparison test holds the two to that.
//
// These live in the core only until their module moves out of it: each then
// takes its schema along, and offers it as `module::schema`.

/// `guild_settings` and `allowed_bots`.
[[nodiscard]] auto core_schema() noexcept -> module_schema;

[[nodiscard]] auto triggers_schema() noexcept -> module_schema;
[[nodiscard]] auto nicknames_schema() noexcept -> module_schema;
[[nodiscard]] auto midnight_schema() noexcept -> module_schema;
[[nodiscard]] auto links_schema() noexcept -> module_schema;
/// Requires links': it counts reactions on `replacement_messages`.
[[nodiscard]] auto linkstats_schema() noexcept -> module_schema;
[[nodiscard]] auto dectalk_schema() noexcept -> module_schema;
[[nodiscard]] auto llm_schema() noexcept -> module_schema;

/// The schemas of the core and of every feature still inside it, in the
/// order they are applied: a module's after those it requires.
[[nodiscard]] auto builtin_schemas() noexcept -> std::span<const module_schema>;

} // namespace latibot::db
