#include "core/commands/message_options.hpp"

#include "core/commands/options.hpp"

#include <optional>

namespace latibot::commands {
namespace {

/// Sets or clears `bit` when the boolean option `name` was given. `on_means_set`
/// says which way round it reads: `silent:true` sets its flag, `previews:true`
/// clears one.
void apply_option(const dpp::slashcommand_t& event, const char* name, discord::message_flags bit, bool on_means_set,
                  discord::message_flags& flags) {
    const std::optional<bool> chosen = bool_option(event, name);
    if (!chosen) {
        return;
    }

    const bool set = *chosen == on_means_set;
    flags = static_cast<discord::message_flags>(set ? flags | bit : flags & ~bit);
}

} // namespace

void add_message_options(dpp::command_option& subcommand) {
    subcommand.add_option(dpp::command_option(dpp::co_boolean, "silent", "Post without notifying anyone. On unless set to false.", false));
    subcommand.add_option(dpp::command_option(dpp::co_boolean, "previews", "Show link previews. On unless set to false.", false));
}

void apply_message_options(const dpp::slashcommand_t& event, discord::message_flags& flags) {
    apply_option(event, "silent", dpp::m_suppress_notifications, true, flags);
    apply_option(event, "previews", dpp::m_suppress_embeds, false, flags);
}

std::string describe_message_options(discord::message_flags flags) {
    std::string text;
    if ((flags & dpp::m_suppress_notifications) == 0) {
        text = "notifies";
    }
    if ((flags & dpp::m_suppress_embeds) != 0) {
        text += text.empty() ? "no previews" : ", no previews";
    }
    return text;
}

} // namespace latibot::commands
