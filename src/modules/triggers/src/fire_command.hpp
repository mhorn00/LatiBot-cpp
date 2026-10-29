#pragma once

#include "core/capabilities/model_replies.hpp"
#include "core/commands/registry.hpp"
#include "core/ui/paginator.hpp"
#include "triggers.hpp"

#include <dpp/appcommand.h>
#include <dpp/dispatcher.h>
#include <dpp/message.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::commands {

/// The command's name in a message's Apps menu.
inline constexpr std::string_view fire_command_name = "Fire a trigger";

/// The menu of triggers it answers with, and its pages.
inline constexpr std::string_view trigger_fire_view = "trigfire";

/// How many triggers one page of the menu holds: Discord's most for a menu.
inline constexpr std::size_t fire_menu_size = 25;

/// A message as the stages see it, from one Discord resolved for a context
/// menu. Whoever fires a trigger by hand vouches for the message, so another
/// bot's counts as one the server allowed.
[[nodiscard]] auto as_incoming(const dpp::message& message, dpp::snowflake guild, dpp::snowflake me) -> events::incoming_message;

/// The menu for request `request`: a page of `triggers`, the enabled ones, to
/// pick the one to fire, with ◀ / ▶ when they do not fit in one.
[[nodiscard]] auto render_fire_menu(std::span<const events::trigger> triggers, int page, std::uint64_t request) -> dpp::message;

/// Right-click a message → Apps → "Fire a trigger"
/// (src/modules/triggers/docs/Triggers.md §2.9): fires the trigger picked on
/// that message, whatever its pattern, chance, check or cooldown say.
/// Manage Messages by default.
///
/// The message is remembered between the command and the pick, for as long
/// as Discord lets the menu be answered; a restart forgets it, and the menu
/// says to try again.
class fire_command final : public command {
public:
    /// Carries out what firing came to: posts a reply, or runs the model.
    using carry_out = std::function<void(std::vector<events::action>)>;

    fire_command(events::trigger_store& store, events::trigger_responder& responder, const capabilities::model_replies* model,
                 std::function<dpp::snowflake()> me, carry_out act);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

    /// The menu's picks and pages. False for any other view.
    auto on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool;

    /// How long a request waits for its pick: as long as Discord keeps an
    /// interaction answerable.
    static constexpr std::chrono::minutes request_lifetime{15};

private:
    struct request {
        events::incoming_message message;
        std::chrono::steady_clock::time_point made;
    };

    /// The guild's triggers that can be fired: the enabled ones.
    [[nodiscard]] auto fireable(dpp::snowflake guild) const -> std::vector<events::trigger>;

    command_info info_;
    events::trigger_store* store_;
    events::trigger_responder* responder_;
    const capabilities::model_replies* model_;
    std::function<dpp::snowflake()> me_;
    carry_out act_;

    std::mutex mutex_;
    std::map<std::uint64_t, request> pending_;
    std::uint64_t next_ = 1;
};

} // namespace latibot::commands
