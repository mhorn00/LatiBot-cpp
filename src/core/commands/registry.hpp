#pragma once

#include "core/discord/message_flags.hpp"
#include "core/util/log.hpp"

#include <dpp/appcommand.h>
#include <dpp/coro/task.h>
#include <dpp/dispatcher.h>
#include <dpp/permissions.h>
#include <dpp/snowflake.h>
#include <dpp/user.h>

#include <cstdint>
#include <format>
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

/// The message flags a command sends each kind of message with.
///
/// Kinds rather than one set per command, because one command can send all
/// three: `/say` answers its caller privately and speaks publicly, and
/// `/linkstats` posts a public board, refuses a typo privately, and reports a
/// recompute's progress in the channel. Only the flags in
/// `discord::reply_flags` can be chosen; `registry::add` refuses the rest.
struct response_flags {
    /// The answer the command exists to give: a list, a board, "ok, updated".
    discord::message_flags result = dpp::m_ephemeral;

    /// Why it could not: a bad option, a missing permission, something that
    /// went wrong. Private by default; nobody else needs to see a typo.
    discord::message_flags refusal = dpp::m_ephemeral;

    /// An ordinary channel message sent on the command's behalf, not a reply
    /// to it: what `/say` says, a recompute's progress. Never ephemeral, since
    /// only a reply can be.
    discord::message_flags post = 0;
};

/// Where a subcommand differs from its command. Anything left unset is the
/// command's.
struct response_overrides {
    std::optional<discord::message_flags> result;
    std::optional<discord::message_flags> refusal;
    std::optional<discord::message_flags> post;
};

/// What a command is and what it needs.
///
/// `required_bot_permissions` feeds the startup permission check, which warns
/// per guild instead of exiting (plan §7).
// Moving one is only as noexcept as moving a std::map, which allocates on
// MSVC. Only tests move a command_info, and nothing relies on it not throwing.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct command_info {
    std::string name;
    std::string description;

    /// Extra names the same command answers to. Each is registered with
    /// Discord as its own slash command.
    std::vector<std::string> aliases;

    std::uint64_t required_bot_permissions = 0;
    std::optional<dpp::permission> default_member_permissions;
    bool guild_only = true;

    /// How the command's messages are flagged, unless a subcommand says
    /// otherwise below.
    response_flags responses;

    /// Per subcommand, keyed by its path as typed: `"test"`, `"alias add"`.
    /// `registry::add` checks every key against the command's options, so a
    /// misspelt one stops startup rather than quietly applying to nothing.
    std::map<std::string, response_overrides, std::less<>> subcommand_responses;

    /// The flags for one subcommand path: its overrides over the command's.
    [[nodiscard]] auto responses_for(std::string_view subcommand) const -> response_flags;
};

/// The subcommand an interaction ran, as `"alias add"` or `"test"`; empty for
/// a command without subcommands.
[[nodiscard]] auto subcommand_path(const dpp::command_interaction& interaction) -> std::string;

/// Every subcommand path a registration payload offers, groups included.
[[nodiscard]] auto subcommand_paths(const dpp::slashcommand& payload) -> std::vector<std::string>;

/// What the bot says when a command threw, rather than leaving Discord to say
/// "The application did not respond".
inline constexpr std::string_view command_failed_reply = "something went wrong on my end running that; it's in the log";

/// What it says to a command it no longer has, which Discord can still offer
/// for a while after one is removed.
inline constexpr std::string_view unknown_command_reply = "i don't have that command any more";

/// One line naming what a command was asked to do: the subcommand path, then
/// each option as `name=value`.
///
/// For the log, so it has to stay one line and stay bounded — newlines are
/// escaped and long values are cut with their full length noted, otherwise a
/// single 2000-character `/say` would bury everything around it.
[[nodiscard]] auto describe_invocation(const dpp::command_interaction& interaction) -> std::string;

/// Who ran something, as the log shows them: `name (id)`.
///
/// A type rather than a string so the id keeps its colour in the log; the
/// formatter below writes it with `util::paint_to`.
struct user_label {
    std::string name;
    dpp::snowflake id;
};

/// Names are not unique and ids are not readable, so the log carries both.
[[nodiscard]] auto describe_user(const dpp::user& who) -> user_label;

/// The option Discord is asking for completions on.
///
/// Options nest: a command holds a subcommand, which holds the option being
/// typed into, so the focused one is not always at the top. Returns nullptr
/// when nothing is focused, which happens if Discord's payload changes shape.
[[nodiscard]] auto focused_option(const std::vector<dpp::command_option>& options) -> const dpp::command_option*;

/// One slash command.
///
/// Handlers stay thin: turn the event into plain data, call a core function,
/// act on the result (plan §5.3).
class command {
public:
    virtual ~command() = default;

    command() = default;
    command(const command&) = delete;
    auto operator=(const command&) -> command& = delete;

    [[nodiscard]] virtual auto info() const -> const command_info& = 0;

    /// Builds the registration payload under `name`, which may be an alias.
    /// Commands with options override this and add them.
    [[nodiscard]] virtual auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand;

    virtual auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> = 0;

    /// The flags this command sends messages with, for the subcommand `event`
    /// ran.
    [[nodiscard]] auto responses_for(const dpp::slashcommand_t& event) const -> response_flags;

    /// A reply carrying this command's `result` flags. Every reply goes
    /// through one of these three, which is what makes `command_info` the one
    /// place its flags are decided; a flag a renderer set is replaced.
    [[nodiscard]] auto result(const dpp::slashcommand_t& event, dpp::message message) const -> dpp::message;
    [[nodiscard]] auto result(const dpp::slashcommand_t& event, std::string_view text) const -> dpp::message;

    /// A reply carrying the `refusal` flags.
    [[nodiscard]] auto refusal(const dpp::slashcommand_t& event, std::string_view text) const -> dpp::message;

    /// A channel message carrying the `post` flags.
    [[nodiscard]] auto post(const dpp::slashcommand_t& event, dpp::message message) const -> dpp::message;

    /// Tells Discord the answer is on its way, for a command that has to wait
    /// on Discord before it knows what to say.
    ///
    /// The first response to a command is due within three seconds, and a
    /// REST call queued behind DPP's rate limiter can take longer. Private
    /// when this subcommand's result is, since that cannot change afterwards:
    /// a refusal that follows is only as private as the result. After this,
    /// answer with `answer_deferred`, not `co_reply`.
    auto defer(const dpp::slashcommand_t& event) const -> dpp::task<void>;

    /// Replaces the "thinking…" `defer` left with `message`, a `result` or a
    /// `refusal`.
    auto answer_deferred(const dpp::slashcommand_t& event, dpp::message message) const -> dpp::task<void>;

    /// Offers completions for the option being typed into.
    ///
    /// Not a coroutine: Discord gives an autocomplete three seconds and there
    /// is nothing to await, since the answer comes from what the bot already
    /// knows. Commands without an autocompleted option leave it alone.
    virtual auto autocomplete(const dpp::autocomplete_t& event) const -> void;
};

/// Holds the commands and routes interactions to them.
class registry {
public:
    /// Throws `registry_error` if the name or any alias is already taken, or
    /// if the command's response flags name a subcommand it does not have or
    /// a flag its messages cannot carry. All of these are programming
    /// errors, so they stop startup rather than surfacing in a reply.
    auto add(std::unique_ptr<command> new_command) -> void;

    [[nodiscard]] auto find(std::string_view name_or_alias) const -> command*;

    /// Every registration payload, including one per alias.
    [[nodiscard]] auto build_all(dpp::snowflake application_id) const -> std::vector<dpp::slashcommand>;

    /// Union of what every command needs, for the startup permission check.
    [[nodiscard]] auto required_bot_permissions() const -> std::uint64_t;

    [[nodiscard]] auto size() const -> std::size_t { return commands_.size(); }

    /// Runs the command registered under `name`.
    ///
    /// An unknown name is logged, not thrown: Discord can still deliver a
    /// command that was removed from the code but not yet from the guild. An
    /// exception escaping a handler is logged too, since letting it leave the
    /// coroutine would take the process down. Either way the person who ran
    /// it is told, with the command's refusal flags.
    auto dispatch(std::string name, const dpp::slashcommand_t& event) const -> dpp::task<void>;

    /// Routes an autocomplete to the command that owns it.
    ///
    /// Unknown names are ignored rather than logged at anything louder than
    /// debug: these arrive on every keystroke.
    auto offer_completions(std::string_view name, const dpp::autocomplete_t& event) const -> void;

private:
    /// Throws `registry_error` for response flags `add` should refuse.
    static auto check_responses(const command& candidate) -> void;

    std::vector<std::unique_ptr<command>> commands_;
    std::map<std::string, command*, std::less<>> by_name_;
};

} // namespace latibot::commands

/// `name (id)`, with the id in its colour when the line is being coloured.
template <>
struct std::formatter<latibot::commands::user_label, char> {
    static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template <typename FormatContext>
    auto format(const latibot::commands::user_label& who, FormatContext& ctx) const {
        auto out = std::format_to(ctx.out(), "{} (", who.name);
        out = latibot::util::paint_to(out, who.id);
        return std::format_to(out, ")");
    }
};
