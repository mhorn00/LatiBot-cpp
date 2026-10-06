#pragma once

#include "backfill.hpp"
#include "core/commands/registry.hpp"
#include "reactions.hpp"

#include <dpp/appcommand.h>
#include <dpp/dispatcher.h>
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

namespace latibot::config {
class guild_settings;
}

namespace latibot::ports {
class discord_gateway;
}

namespace latibot::ui {
struct page_state;
}

namespace latibot::commands {

/// How many places a leaderboard of people shows, unless `per_page` says.
inline constexpr std::size_t leaderboard_size = 10;

/// How many emojis a page of `/linkstats reactions` shows, unless `per_page`
/// says. More than a leaderboard, since a server's every emoji is a long list
/// and each line is short.
inline constexpr std::size_t emoji_page_size = 20;

/// The most `per_page` takes. Whether that many fit in a message is checked
/// when it is asked for (`largest_page`); this only keeps the option sane.
inline constexpr std::size_t max_page_size = 200;

/// A day typed as YYYY-MM-DD, in UTC. Nothing when it is not a real date.
[[nodiscard]] auto parse_day(std::string_view text) -> std::optional<std::chrono::sys_days>;

/// What a board counts: people on one of the three sides, or the emojis
/// themselves, received or given.
enum class board : std::uint8_t { received, given, self, emoji, emoji_given };

/// `/linkstats top`'s `by` option.
[[nodiscard]] auto board_from_string(std::string_view name) -> std::optional<board>;

/// An emoji somebody typed or picked, made into what the statistics know.
///
/// A bare name ("skull" or ":skull:") is looked up among the emojis this
/// guild has reacted with, since that is what people type when they do not
/// have the emote to hand. Nothing when it names nothing.
[[nodiscard]] auto resolve_emoji(const events::reaction_store& store, dpp::snowflake guild_id, std::string_view typed)
    -> std::optional<events::emoji_ref>;

/// The view name on a board's ◀ / ▶ buttons.
inline constexpr std::string_view board_view = "linkboard";

/// The views of `/linkstats duplicates`: a group of emojis that look alike,
/// picking the one to keep, and merging others into it.
inline constexpr std::string_view similar_view = "linkdupes";
inline constexpr std::string_view keep_view = "linkkeep";
inline constexpr std::string_view merge_view = "linkmerge";

/// What the merge menu's "all of them" choice sends.
inline constexpr std::string_view merge_everything = "*";

/// The longest `domain` filter the command takes.
///
/// A board's filters ride in its buttons' custom_id, which holds 100
/// characters, and a longer site left too little room: paging then quietly
/// dropped out. At 40, any custom emoji and any ordinary Unicode one fit
/// beside it, or a person on an emoji board; only the longest joined emoji
/// sequences can still crowd it. Real sites are far shorter.
inline constexpr std::uint32_t domain_length_limit = 40;

/// How many a page of `which` holds when nobody said: `leaderboard_size`
/// people or `emoji_page_size` emojis.
[[nodiscard]] auto default_page_size(board which) -> std::size_t;

/// A board as its buttons remember it.
struct board_spec {
    board which = board::received;
    events::stat_query query;

    /// How many to a page; 0 for `default_page_size`.
    std::size_t per_page = 0;
};

/// A board's filters, packed into its buttons' custom_id so a page reached
/// by paging is the same board:
/// `r;<emoji>;<site>;<since>;<until>;<user>;<source>;<per page>`, with the
/// dates as days since 1970. Buttons sent before the last three existed
/// have five to seven fields, and still decode.
[[nodiscard]] auto encode_board(board which, const events::stat_query& query, std::size_t per_page = 0) -> std::string;
[[nodiscard]] auto decode_board(std::string_view argument) -> std::optional<board_spec>;

/// `/linkstats top` or `/linkstats reactions`, at `page`, `per_page` to a
/// page (0 for `default_page_size`), with ◀ / ▶ when there is more than one.
///
/// Public, and anybody can page it, the way `/nicknames` works: a
/// leaderboard is something a room reads together. An emoji board with
/// `query.user_id` set is one person's reactions. A page that has outgrown
/// a message since its size was checked shows what fits and says so.
[[nodiscard]] auto render_board(const events::reaction_store& store, dpp::snowflake guild_id, board which, const events::stat_query& query,
                                int page = 0, std::size_t per_page = 0) -> dpp::message;

/// The most to a page, up to `wanted`, with which every page of the board
/// fits in a message of Discord's 2,000 characters. 0 when not even one
/// does.
[[nodiscard]] auto largest_page(const events::reaction_store& store, dpp::snowflake guild_id, board which, const events::stat_query& query,
                                std::size_t wanted) -> std::size_t;

/// `/linkstats duplicates`: one group of custom emojis whose names look
/// alike (`events::names_look_alike`) per page.
///
/// With `can_merge`, menus pick the one to keep, `keeper`, and then which of
/// the others to merge into it; without, it only lists them. `note` says what
/// the last merge did.
[[nodiscard]] auto render_similar(const events::reaction_store& store, dpp::snowflake guild_id, int page, std::string_view keeper,
                                  bool can_merge, std::string_view note = {}) -> dpp::message;

/// `/linkstats alias list`.
[[nodiscard]] auto render_aliases(const events::reaction_store& store, dpp::snowflake guild_id) -> std::string;

/// Answers the buttons and menus on `/linkstats` messages: the boards'
/// paging, and `/linkstats duplicates`. False when the view is not one of
/// these.
///
/// Merging needs Manage Server, checked again when the menu is used, since
/// whoever opened the list could have lost it since.
auto on_linkstats_component(events::reaction_store& store, const dpp::interaction_create_t& event, const ui::page_state& state,
                            const std::string& chosen) -> bool;

/// A recompute's progress message: running while it runs, then the final
/// report (docs/features/Link_Stats.md §4.3). Messages worth a look are
/// linked, so a click shows them.
[[nodiscard]] auto render_backfill(const events::backfill_report& report, const events::backfill_request& request, bool finished)
    -> std::string;

/// Every message a recompute thought worth a look, as a text file of links
/// under headings. Empty when there were none.
[[nodiscard]] auto backfill_issues(const events::backfill_report& report, const events::backfill_request& request) -> std::string;

/// The reply to a recompute's progress message once it is over, which pings
/// whoever started it: the report is far up the channel by then, under
/// everything posted while it ran. Carries `backfill_issues` as a file when
/// there are any, since the report only has room for a few.
[[nodiscard]] auto backfill_done_reply(const events::backfill_report& report, const events::backfill_request& request,
                                       dpp::snowflake channel_id, dpp::snowflake progress_id, dpp::snowflake starter) -> dpp::message;

/// Why the invoker may not run the `/linkstats` subcommand at `subcommand`, as
/// `subcommand_path` spells it ("alias add"), or nothing when they may.
///
/// Reading is open to everyone; changing aliases and recomputing need Manage
/// Server. Discord's default permissions are per command, not per subcommand,
/// so this is the only check these get
/// (docs/features/Commands_and_Panels.md §2.1).
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

/// `/linkstats top | reactions | duplicates | alias … | recompute … | images …`
/// (docs/features/Link_Stats.md §1).
///
/// Reading is open to everyone; aliases and recomputing need Manage Server,
/// which `linkstats_refusal` decides before any subcommand runs.
class linkstats_command final : public command {
public:
    /// Without `recompute`, the recompute subcommands say they are not
    /// available rather than failing. Without `settings`, nothing counts
    /// images and `images` says so.
    explicit linkstats_command(events::reaction_store& store, recompute_support recompute = {}, config::guild_settings* settings = nullptr);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

    /// Sites, and the emojis this guild has reacted with, by name. Where an
    /// alias is being made, only emojis that count as themselves are
    /// offered; where one is being removed, only aliases.
    auto autocomplete(const dpp::autocomplete_t& event) const -> void override;

private:
    auto top(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto reactions(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto alias(const dpp::slashcommand_t& event, std::string_view subcommand) -> dpp::task<void>;
    auto recompute(const dpp::slashcommand_t& event, std::string_view subcommand) -> dpp::task<void>;
    auto recompute_start(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto images(const dpp::slashcommand_t& event, bool enabled) -> dpp::task<void>;

    /// Whether this guild counts reactions on images and videos
    /// (docs/features/Link_Stats.md §9).
    [[nodiscard]] auto counts_images(dpp::snowflake guild_id) const -> bool;

    /// The `source` option, or when it is left out, both where images are
    /// counted and links alone where they are not.
    [[nodiscard]] auto source_for(const dpp::slashcommand_t& event) const -> events::stat_source;

    /// The `per_page` option into `per_page`, 0 when it was left out. Says
    /// why not when that many to a page would not fit in a message.
    [[nodiscard]] auto read_page_size(const dpp::slashcommand_t& event, board which, const events::stat_query& query,
                                      std::size_t& per_page) const -> std::optional<std::string>;

    command_info info_;
    events::reaction_store* store_;
    recompute_support recompute_;
    config::guild_settings* settings_;
};

} // namespace latibot::commands
