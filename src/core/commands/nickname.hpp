#pragma once

#include "core/commands/registry.hpp"
#include "core/events/nicknames.hpp"

#include <dpp/appcommand.h>
#include <dpp/message.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace dpp {
class cluster;
}

namespace latibot::ports {
class clock;
}

namespace latibot::commands {

/// How many entries one page of `/nicknames` shows.
inline constexpr std::size_t nicknames_per_page = 10;

/// The view name the paginator encodes into the ◀ / ▶ buttons.
inline constexpr std::string_view nickname_history_view = "nicks";

/// Past this many entries, the history is sent as a file instead of paged.
///
/// Ten pages is the point where clicking through stops being the easier way
/// to read something. The Java version simply gave up past 2000 characters,
/// which by now is most histories (plan §8.2).
inline constexpr std::size_t nickname_attachment_threshold = nicknames_per_page * 10;

/// Discord's limit on a nickname, which it enforces server-side.
inline constexpr std::size_t nickname_length_limit = 32;

/// One page of a member's history, with its ◀ / ▶ row.
///
/// Takes the history rather than the store so it can be tested as the pure
/// function it is; the button handler and the command both load it the same
/// way and call this.
[[nodiscard]] auto render_nickname_history(std::span<const events::nickname_change> history, dpp::snowflake user_id, int page)
    -> dpp::message;

/// `/nickname` (plan §8.1).
///
/// Records who ran it before asking Discord to make the change, because the
/// invoker is the one thing Discord's own audit log gets wrong: it records the
/// bot, since the bot is what called the API.
class nickname_command final : public command {
public:
    nickname_command(events::nickname_store& store, events::pending_nicknames& pending, ports::clock& clock, dpp::cluster& cluster);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    command_info info_;
    events::nickname_store* store_;
    events::pending_nicknames* pending_;
    ports::clock* clock_;
    dpp::cluster* cluster_;
};

/// `/nicknames` (plan §8.2).
class nicknames_command final : public command {
public:
    explicit nicknames_command(events::nickname_store& store);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    command_info info_;
    events::nickname_store* store_;
};

} // namespace latibot::commands
