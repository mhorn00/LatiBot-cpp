#include "core/commands/preflight.hpp"

#include <dpp/permissions.h>

#include <array>
#include <format>
#include <utility>

namespace latibot::commands {
namespace {

// Only the permissions this bot can plausibly need. A full table would be
// mostly noise, and describe_permissions falls back to hex for the rest.
constexpr std::array<std::pair<std::uint64_t, std::string_view>, 16> permission_names{{
    {dpp::p_administrator, "Administrator"},
    {dpp::p_view_channel, "View Channel"},
    {dpp::p_send_messages, "Send Messages"},
    {dpp::p_send_messages_in_threads, "Send Messages in Threads"},
    {dpp::p_manage_messages, "Manage Messages"},
    {dpp::p_embed_links, "Embed Links"},
    {dpp::p_attach_files, "Attach Files"},
    {dpp::p_read_message_history, "Read Message History"},
    {dpp::p_add_reactions, "Add Reactions"},
    {dpp::p_use_external_emojis, "Use External Emojis"},
    {dpp::p_connect, "Connect"},
    {dpp::p_speak, "Speak"},
    {dpp::p_manage_nicknames, "Manage Nicknames"},
    {dpp::p_change_nickname, "Change Nickname"},
    {dpp::p_view_audit_log, "View Audit Log"},
    {dpp::p_manage_webhooks, "Manage Webhooks"},
}};

// Passive features, as they land: the message pipeline reads messages and
// answers in the same channel, nickname tracking reads the audit log to find
// out who made a change, and URL replacement posts previews and turns off the
// original's. The entries plan §7 lists for voice are added by the phase
// that builds it, so a warning always names something that actually exists.
constexpr std::array<requirement, 4> passive{{
    {.permissions = dpp::p_view_channel | dpp::p_send_messages, .purpose = "replying to messages"},
    {.permissions = dpp::p_view_audit_log, .purpose = "naming who changed a nickname"},
    // Without it the replacement posts but shows nothing, which looks like a
    // broken mirror rather than a missing permission.
    {.permissions = dpp::p_embed_links, .purpose = "showing link previews in URL replacements"},
    {.permissions = dpp::p_manage_messages, .purpose = "turning off the original preview when a link is replaced"},
}};

} // namespace

auto passive_requirements() noexcept -> std::span<const requirement> {
    return passive;
}

auto unmet(std::span<const requirement> required, std::uint64_t granted) -> std::vector<gap> {
    std::vector<gap> gaps;

    // Administrator overrides every other permission on Discord's side, so a
    // guild that granted it is fully satisfied whatever the bits say.
    if ((granted & dpp::p_administrator) != 0) return gaps;

    for (const requirement& entry : required) {
        const std::uint64_t missing = entry.permissions & ~granted;
        if (missing != 0) gaps.push_back({.permissions = missing, .purpose = entry.purpose});
    }
    return gaps;
}

auto describe_permissions(std::uint64_t permissions) -> std::string {
    std::string description;
    std::uint64_t remaining = permissions;

    for (const auto& [bit, name] : permission_names) {
        if ((remaining & bit) == 0) continue;
        if (!description.empty()) description += ", ";
        description += name;
        remaining &= ~bit;
    }

    if (remaining != 0) {
        if (!description.empty()) description += ", ";
        description += std::format("0x{:x}", remaining);
    }
    return description;
}

} // namespace latibot::commands
