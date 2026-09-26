#pragma once

#include "core/commands/registry.hpp"
#include "core/events/backfill.hpp"
#include "core/events/reactions.hpp"

#include <dpp/appcommand.h>
#include <dpp/message.h>
#include <dpp/permissions.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace latibot::ports {
class discord_gateway;
}

namespace latibot::commands {

/// How many places a leaderboard shows.
inline constexpr std::size_t leaderboard_size = 10;

/// How many emojis a profile lists per side: "your top 3 reactions"
/// (plan §9.6).
inline constexpr std::size_t profile_emojis = 3;

/// A day typed as YYYY-MM-DD, in UTC. Nothing when it is not a real date.
[[nodiscard]] auto parse_day(std::string_view text) -> std::optional<std::chrono::sys_days>;

/// What `/linkstats top` counts: one of the three sides, or the emojis
/// themselves.
enum class board : std::uint8_t { received, given, self, emoji };

[[nodiscard]] auto board_from_string(std::string_view name) -> std::optional<board>;

/// An emoji somebody typed or picked, made into what the statistics know.
///
/// A bare name ("skull" or ":skull:") is looked up among the emojis this
/// guild has reacted with, since that is what people type when they do not
/// have the emote to hand. Nothing when it names nothing.
[[nodiscard]] auto resolve_emoji(const events::reaction_store& store, dpp::snowflake guild_id, std::string_view typed)
    -> std::optional<events::emoji_ref>;

/// The view name on a leaderboard's ◀ / ▶ buttons.
inline constexpr std::string_view board_view = "linkboard";

/// The longest `domain` filter the command takes.
///
/// A board's filters ride in its buttons' custom_id, which holds 100
/// characters, and a longer site left too little room: paging then quietly
/// dropped out. At 40, any custom emoji and any ordinary Unicode one fit
/// beside it; only the longest joined emoji sequences can still crowd it.
/// Real sites are far shorter.
inline constexpr std::uint32_t domain_length_limit = 40;

/// A leaderboard's filters, packed into its buttons' custom_id so a page
/// reached by paging is the same board: `r;<emoji>;<site>;<since>;<until>`,
/// with the dates as days since 1970.
[[nodiscard]] auto encode_board(board which, const events::stat_query& query) -> std::string;
[[nodiscard]] auto decode_board(std::string_view argument) -> std::optional<std::pair<board, events::stat_query>>;

/// `/linkstats top`, at `page`, with ◀ / ▶ when there is more than one.
///
/// Public, and anybody can page it, the way `/nicknames` works: a
/// leaderboard is something a room reads together.
[[nodiscard]] auto render_board(const events::reaction_store& store, dpp::snowflake guild_id, board which, const events::stat_query& query,
                                int page = 0) -> dpp::message;

/// `/linkstats user`: received, given and self-reactions for one person.
[[nodiscard]] auto render_profile(const events::reaction_store& store, dpp::snowflake guild_id, dpp::snowflake user_id,
                                  const events::stat_query& window) -> std::string;

/// `/linkstats emojis`: custom emojis that share a name.
[[nodiscard]] auto render_duplicates(const events::reaction_store& store, dpp::snowflake guild_id) -> std::string;

/// `/linkstats alias list`.
[[nodiscard]] auto render_aliases(const events::reaction_store& store, dpp::snowflake guild_id) -> std::string;

/// A recompute's progress message: running while it runs, then the final
/// report (plan §9.7).
[[nodiscard]] auto render_backfill(const events::backfill_report& report, const events::backfill_request& request, bool finished)
    -> std::string;

/// Why the invoker may not run the `/linkstats` subcommand at `subcommand`, as
/// `subcommand_path` spells it ("alias add"), or nothing when they may.
///
/// Reading is open to everyone; changing aliases and recomputing need Manage
/// Server. Discord's default permissions are per command, not per
/// subcommand, so this is the only check these get (plan §21.13).
[[nodiscard]] auto linkstats_refusal(std::string_view subcommand, dpp::permission invoker) -> std::optional<std::string>;

/// What `/linkstats recompute` needs from outside the statistics.
struct recompute_support {
    events::backfill_service* service = nullptr;

    /// Where the progress message is posted and edited.
    ports::discord_gateway* discord = nullptr;

    /// The text channels to scan when none is named.
    std::function<std::vector<dpp::snowflake>(dpp::snowflake guild_id)> channels_of;

    /// The bot's own user, known once connected.
    std::function<dpp::snowflake()> bot_id;
};

/// `/linkstats top | user | emojis | alias …` (plan §9.6).
///
/// Reading is open to everyone; aliases and recomputing need Manage Server,
/// which `linkstats_refusal` decides before any subcommand runs.
class linkstats_command final : public command {
public:
    /// Without `recompute`, the recompute subcommands say they are not
    /// available rather than failing.
    explicit linkstats_command(events::reaction_store& store, recompute_support recompute = {});

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

    /// The emojis this guild has reacted with, by name.
    auto autocomplete(const dpp::autocomplete_t& event) const -> void override;

private:
    auto top(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto user(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto alias(const dpp::slashcommand_t& event, std::string_view subcommand) -> dpp::task<void>;
    auto recompute(const dpp::slashcommand_t& event, std::string_view subcommand) -> dpp::task<void>;
    auto recompute_start(const dpp::slashcommand_t& event) -> dpp::task<void>;

    command_info info_;
    events::reaction_store* store_;
    recompute_support recompute_;
};

} // namespace latibot::commands
