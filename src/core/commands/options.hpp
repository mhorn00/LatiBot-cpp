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
[[nodiscard]] std::string string_option(const dpp::slashcommand_t& event, const char* name);

/// An integer option.
[[nodiscard]] std::optional<std::int64_t> int_option(const dpp::slashcommand_t& event, const char* name);

/// A true-or-false option. Nothing when it was not given, which is not the
/// same as false: an edit leaves what it was not told to change.
[[nodiscard]] std::optional<bool> bool_option(const dpp::slashcommand_t& event, const char* name);

/// A user, channel or role option: its id.
[[nodiscard]] std::optional<dpp::snowflake> snowflake_option(const dpp::slashcommand_t& event, const char* name);

/// What the person running the command may do in this channel, as Discord
/// worked it out for the interaction. Empty in a DM, where it sends none.
[[nodiscard]] dpp::permission invoker_permissions(const dpp::slashcommand_t& event);

} // namespace latibot::commands
