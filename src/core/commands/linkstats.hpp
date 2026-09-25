#pragma once

#include "core/commands/registry.hpp"
#include "core/events/backfill.hpp"
#include "core/events/reactions.hpp"

#include <dpp/appcommand.h>
#include <dpp/message.h>

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
[[nodiscard]] std::optional<std::chrono::sys_days> parse_day(std::string_view text);

/// What `/linkstats top` counts: one of the three sides, or the emojis
/// themselves.
enum class board : std::uint8_t { received, given, self, emoji };

[[nodiscard]] std::optional<board> board_from_string(std::string_view name);

/// An emoji somebody typed or picked, made into what the statistics know.
///
/// A bare name ("skull" or ":skull:") is looked up among the emojis this
/// guild has reacted with, since that is what people type when they do not
/// have the emote to hand. Nothing when it names nothing.
[[nodiscard]] std::optional<events::emoji_ref> resolve_emoji(const events::reaction_store& store, dpp::snowflake guild_id,
                                                             std::string_view typed);

/// The view name on a leaderboard's ◀ / ▶ buttons.
inline constexpr std::string_view board_view = "linkboard";

/// A leaderboard's filters, packed into its buttons' custom_id so a page
/// reached by paging is the same board: `r;<emoji>;<site>;<since>;<until>`,
/// with the dates as days since 1970.
[[nodiscard]] std::string encode_board(board which, const events::stat_query& query);
[[nodiscard]] std::optional<std::pair<board, events::stat_query>> decode_board(std::string_view argument);

/// `/linkstats top`, at `page`, with ◀ / ▶ when there is more than one.
///
/// Public, and anybody can page it, the way `/nicknames` works: a
/// leaderboard is something a room reads together.
[[nodiscard]] dpp::message render_board(const events::reaction_store& store, dpp::snowflake guild_id, board which,
                                        const events::stat_query& query, int page = 0);

/// `/linkstats user`: received, given and self-reactions for one person.
[[nodiscard]] std::string render_profile(const events::reaction_store& store, dpp::snowflake guild_id, dpp::snowflake user_id,
                                         const events::stat_query& window);

/// `/linkstats emojis`: custom emojis that share a name.
[[nodiscard]] std::string render_duplicates(const events::reaction_store& store, dpp::snowflake guild_id);

/// `/linkstats alias list`.
[[nodiscard]] std::string render_aliases(const events::reaction_store& store, dpp::snowflake guild_id);

/// A recompute's progress message: running while it runs, then the final
/// report (plan §9.7).
[[nodiscard]] std::string render_backfill(const events::backfill_report& report, const events::backfill_request& request, bool finished);

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
/// checked here because Discord's default permissions are per command, not
/// per subcommand.
class linkstats_command final : public command {
public:
    /// Without `recompute`, the recompute subcommands say they are not
    /// available rather than failing.
    explicit linkstats_command(events::reaction_store& store, recompute_support recompute = {});

    [[nodiscard]] const command_info& info() const override { return info_; }
    [[nodiscard]] dpp::slashcommand build(const std::string& name, dpp::snowflake application_id) const override;
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;

    /// The emojis this guild has reacted with, by name.
    void autocomplete(const dpp::autocomplete_t& event) const override;

private:
    dpp::task<void> top(const dpp::slashcommand_t& event);
    dpp::task<void> user(const dpp::slashcommand_t& event);
    dpp::task<void> alias(const dpp::slashcommand_t& event, const std::string& action);
    dpp::task<void> recompute(const dpp::slashcommand_t& event, const std::string& action);
    dpp::task<void> recompute_start(const dpp::slashcommand_t& event);

    command_info info_;
    events::reaction_store* store_;
    recompute_support recompute_;
};

} // namespace latibot::commands
