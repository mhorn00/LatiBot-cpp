#pragma once

#include <dpp/dispatcher.h>
#include <dpp/permissions.h>
#include <dpp/snowflake.h>

#include <cstdint>
#include <optional>
#include <string>

namespace latibot::commands {

// Reading a slash command's options.
//
// DPP hands each option over as a variant. These return the one type the
// option was declared with, and nothing, or empty text, when it was left out,
// so a handler checks one value rather than the variant.

/// A text option, or empty when it was not given.
[[nodiscard]] auto string_option(const dpp::slashcommand_t& event, const char* name) -> std::string;

/// An integer option.
[[nodiscard]] auto int_option(const dpp::slashcommand_t& event, const char* name) -> std::optional<std::int64_t>;

/// A true-or-false option. Nothing when it was not given, which is not the
/// same as false: an edit leaves what it was not told to change.
[[nodiscard]] auto bool_option(const dpp::slashcommand_t& event, const char* name) -> std::optional<bool>;

/// A user, channel or role option: its id.
[[nodiscard]] auto snowflake_option(const dpp::slashcommand_t& event, const char* name) -> std::optional<dpp::snowflake>;

/// What whoever used a command, button or form may do in this channel, as
/// Discord worked it out for the interaction. Empty in a DM, where it sends
/// none.
[[nodiscard]] auto invoker_permissions(const dpp::interaction_create_t& event) -> dpp::permission;

} // namespace latibot::commands
