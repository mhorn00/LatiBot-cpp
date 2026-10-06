#pragma once

#include "core/db/migrations.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace latibot::module {

class capability_registry;
class host;

/// A feature built into the bot or left out of it
/// (docs/modules/Module_Plan_Final.md §4).
///
/// A module owns its stores, commands, stages, listeners and timers. It
/// reaches the rest of the bot through the `host`, and other modules through
/// the capabilities they offer, so leaving it out of the build changes
/// nothing else. Each has one factory, `make_module(host&)`, which reads what
/// it needs and builds its stores, without reading its tables yet: its
/// schema is applied next. `offer` and `start` follow, every module's `offer`
/// before any `start` (§4.3).
class module {
public:
    virtual ~module() = default;

    module() = default;
    module(const module&) = delete;
    auto operator=(const module&) -> module& = delete;

    /// Its name in the log, and later in `schema_versions` and config.json:
    /// "midnight", "llm".
    [[nodiscard]] virtual auto name() const -> std::string_view = 0;

    /// Its tables, as schema steps, version 1 first, recorded under its name
    /// (§7). Steps that have shipped are never edited.
    [[nodiscard]] virtual auto schema() const -> std::span<const db::migration> { return {}; }

    /// Offers what other modules may use. Every module exists by now, and
    /// none has started.
    virtual auto offer(capability_registry& /*offered*/) -> void {}

    /// Registers its commands, stages, listeners, timers and panels, and
    /// looks up the capabilities it uses. Throwing stops startup.
    virtual auto start(host& bot) -> void = 0;
};

using module_list = std::vector<std::unique_ptr<module>>;

/// Builds every module this build includes, in dependency order: the
/// generated `enabled_modules` in the executable (§4.8), or a test's own.
using module_factory = std::function<module_list(host&)>;

/// Builds the modules with `make`, applies each one's schema, has every one
/// offer its capabilities into `offered`, then has every one start
/// (docs/modules/Module_Plan_Final.md §4.3). The database must have been
/// through `db::prepare_schema_versions`. Whatever a module throws stops startup, so it is left to escape.
/// Returns the modules, which must then outlive everything they registered.
auto start_modules(const module_factory& make, host& bot, capability_registry& offered) -> module_list;

/// This build's modules: the `module_factory` the executable runs with.
/// Defined by the executable, not the core, so a test linking the core
/// chooses its own.
auto enabled_modules(host& bot) -> module_list;

} // namespace latibot::module
