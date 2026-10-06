#pragma once

#include "core/db/migrations.hpp"

#include <dpp/json.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::modules {

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

/// The modules, in the order they were built, which is dependency order.
/// Destroyed in reverse, last first, so that no module outlives one it
/// requires: music, say, lets go of voice's mixer before voice is gone
/// (docs/modules/Module_Plan_Final.md §4.3).
class module_list {
public:
    using container = std::vector<std::unique_ptr<module>>;

    module_list() = default;
    module_list(const module_list&) = delete;
    module_list(module_list&&) noexcept = default;
    auto operator=(const module_list&) -> module_list& = delete;
    auto operator=(module_list&& other) noexcept -> module_list& {
        clear();
        modules_ = std::move(other.modules_);
        return *this;
    }
    ~module_list() { clear(); }

    auto push_back(std::unique_ptr<module> made) -> void { modules_.push_back(std::move(made)); }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return modules_.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return modules_.empty(); }
    [[nodiscard]] auto front() const -> const std::unique_ptr<module>& { return modules_.front(); }
    [[nodiscard]] auto begin() const noexcept -> container::const_iterator { return modules_.begin(); }
    [[nodiscard]] auto end() const noexcept -> container::const_iterator { return modules_.end(); }

private:
    auto clear() noexcept -> void {
        while (!modules_.empty()) {
            modules_.pop_back();
        }
    }

    container modules_;
};

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
/// Defined by the generated module list, not the core, so a test linking the
/// core chooses its own.
auto enabled_modules(host& bot) -> module_list;

/// This build's modules' config.json sections, each at its defaults, for
/// the file the bot writes when there is none (`config::bootstrap::load`).
auto enabled_config_defaults() -> nlohmann::ordered_json;

} // namespace latibot::modules
