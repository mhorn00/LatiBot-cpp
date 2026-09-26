#pragma once

#include "core/discord/message_flags.hpp"

#include <dpp/appcommand.h>
#include <dpp/dispatcher.h>

#include <string>

namespace latibot::commands {

/// Adds `silent` and `previews` to a subcommand that sets up messages the bot
/// posts later, like a trigger's replies or a midnight message.
///
/// Both are optional booleans that default to on, matching what every such
/// message did before they could be chosen: silent, with previews.
auto add_message_options(dpp::command_option& subcommand) -> void;

/// Applies whichever of `silent` and `previews` the event was given to
/// `flags`. One left out keeps its current value, so an edit that does not
/// mention them changes nothing.
auto apply_message_options(const dpp::slashcommand_t& event, discord::message_flags& flags) -> void;

/// "notifies", "no previews", both, or empty: how `flags` differ from the
/// default of silent with previews, for a one-line description.
[[nodiscard]] auto describe_message_options(discord::message_flags flags) -> std::string;

} // namespace latibot::commands
