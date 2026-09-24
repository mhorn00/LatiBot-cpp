#pragma once

#include "core/commands/registry.hpp"
#include "core/events/reactions.hpp"

#include <dpp/appcommand.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace latibot::commands {

/// How many places a leaderboard shows.
inline constexpr std::size_t leaderboard_size = 10;

/// How many emojis a profile lists per side: "your top 3 reactions"
/// (plan v4 §9.6).
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

/// `/linkstats top`.
[[nodiscard]] std::string render_board(const events::reaction_store& store, dpp::snowflake guild_id, board which,
                                       const events::stat_query& query);

/// `/linkstats user`: received, given and self-reactions for one person.
[[nodiscard]] std::string render_profile(const events::reaction_store& store, dpp::snowflake guild_id, dpp::snowflake user_id,
                                         const events::stat_query& window);

/// `/linkstats emojis`: custom emojis that share a name.
[[nodiscard]] std::string render_duplicates(const events::reaction_store& store, dpp::snowflake guild_id);

/// `/linkstats alias list`.
[[nodiscard]] std::string render_aliases(const events::reaction_store& store, dpp::snowflake guild_id);

/// `/linkstats top | user | emojis | alias …` (plan v4 §9.6).
///
/// Reading is open to everyone; changing aliases needs Manage Messages,
/// checked here because Discord's default permissions are per command, not
/// per subcommand.
class linkstats_command final : public command {
public:
    explicit linkstats_command(events::reaction_store& store);

    [[nodiscard]] const command_info& info() const override { return info_; }
    [[nodiscard]] dpp::slashcommand build(const std::string& name, dpp::snowflake application_id) const override;
    dpp::task<void> execute(const dpp::slashcommand_t& event) override;

    /// The emojis this guild has reacted with, by name.
    void autocomplete(const dpp::autocomplete_t& event) const override;

private:
    dpp::task<void> top(const dpp::slashcommand_t& event);
    dpp::task<void> user(const dpp::slashcommand_t& event);
    dpp::task<void> alias(const dpp::slashcommand_t& event, const std::string& action);

    command_info info_;
    events::reaction_store* store_;
};

} // namespace latibot::commands
