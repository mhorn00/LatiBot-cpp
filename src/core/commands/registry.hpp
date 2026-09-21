#pragma once

#include <dpp/appcommand.h>
#include <dpp/coro/task.h>
#include <dpp/dispatcher.h>
#include <dpp/permissions.h>
#include <dpp/snowflake.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::commands {

/// Thrown when two commands claim the same name or alias. Always a
/// programming error, caught at startup rather than in production.
class registry_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// What a command is and what it needs.
///
/// `required_bot_permissions` feeds the startup permission check, which warns
/// per guild instead of exiting (plan v4 §7).
struct command_info {
    std::string name;
    std::string description;

    /// Extra names the same command answers to. Each is registered with
    /// Discord as its own slash command.
    std::vector<std::string> aliases;

    std::uint64_t required_bot_permissions = 0;
    std::optional<dpp::permission> default_member_permissions;
    bool guild_only = true;
};

/// One slash command.
///
/// Handlers stay thin: turn the event into plain data, call a core function,
/// act on the result (plan v4 §5.3).
class command {
public:
    virtual ~command() = default;

    command() = default;
    command(const command&) = delete;
    command& operator=(const command&) = delete;

    [[nodiscard]] virtual const command_info& info() const = 0;

    /// Builds the registration payload under `name`, which may be an alias.
    /// Commands with options override this and add them.
    [[nodiscard]] virtual dpp::slashcommand build(const std::string& name,
                                                  dpp::snowflake application_id) const;

    virtual dpp::task<void> execute(const dpp::slashcommand_t& event) = 0;
};

/// Holds the commands and routes interactions to them.
class registry {
public:
    /// Throws `registry_error` if the name or any alias is already taken.
    void add(std::unique_ptr<command> new_command);

    [[nodiscard]] command* find(std::string_view name_or_alias) const;

    /// Every registration payload, including one per alias.
    [[nodiscard]] std::vector<dpp::slashcommand> build_all(dpp::snowflake application_id) const;

    /// Union of what every command needs, for the startup permission check.
    [[nodiscard]] std::uint64_t required_bot_permissions() const;

    [[nodiscard]] std::size_t size() const { return commands_.size(); }

    /// Runs the command registered under `name`.
    ///
    /// An unknown name is logged, not thrown: Discord can still deliver a
    /// command that was removed from the code but not yet from the guild. An
    /// exception escaping a handler is logged too, since letting it leave the
    /// coroutine would take the process down.
    dpp::task<void> dispatch(std::string name, const dpp::slashcommand_t& event) const;

private:
    std::vector<std::unique_ptr<command>> commands_;
    std::map<std::string, command*, std::less<>> by_name_;
};

} // namespace latibot::commands
