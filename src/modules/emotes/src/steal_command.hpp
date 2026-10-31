#pragma once

#include "core/commands/registry.hpp"
#include "core/ports/clock.hpp"
#include "core/ports/discord_gateway.hpp"
#include "core/ports/http_client.hpp"
#include "core/ui/paginator.hpp"

#include <dpp/appcommand.h>
#include <dpp/coro/task.h>
#include <dpp/dispatcher.h>
#include <dpp/message.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::commands {

/// As it shows in a message's Apps menu.
inline constexpr std::string_view steal_command_name = "Steal this emote";

// The panel (src/modules/emotes/docs/Steal_Emote.md §3). The page is which
// emote is showing, and the argument says which request it belongs to.
inline constexpr std::string_view steal_page_view = "stealpage";
inline constexpr std::string_view steal_pick_view = "stealpick";
inline constexpr std::string_view steal_name_view = "stealname";
inline constexpr std::string_view steal_form_view = "stealform";
inline constexpr std::string_view steal_add_view = "stealadd";

/// A custom emote written in a message, or a sticker sent with one.
struct found_emote {
    dpp::snowflake id;
    std::string name;
    bool animated = false;

    /// Its format, when it is a sticker.
    std::optional<dpp::sticker_format> sticker = std::nullopt;
};

/// The custom emotes in a message's text, in order, each once. Those inside
/// code are left out, since Discord shows them there as text.
[[nodiscard]] auto find_emotes(std::string_view content) -> std::vector<found_emote>;

/// The stickers a message came with, without any in Lottie format, which
/// Discord lets only verified and partnered servers upload.
[[nodiscard]] auto find_stickers(std::span<const dpp::sticker> stickers) -> std::vector<found_emote>;

/// Whether Discord takes this as an emote's name: 2 to 32 letters, digits and
/// underscores.
[[nodiscard]] auto valid_emote_name(std::string_view name) -> bool;

/// Whether Discord takes this as a sticker's name: 2 to 30 characters, on one
/// line.
[[nodiscard]] auto valid_sticker_name(std::string_view name) -> bool;

/// One emote of a request, and what has been chosen for it.
struct stolen_emote {
    found_emote emote;

    /// What it will be called; the name it has now, until someone changes it.
    std::string name;

    /// Where it will go, empty until someone picks.
    dpp::snowflake server;

    /// Where it has gone already, which the menu then stops offering.
    std::vector<dpp::snowflake> added_to;

    /// What the last try at adding it came to.
    std::string note;

    bool adding = false;
};

/// One use of the command.
struct steal_request {
    dpp::snowflake user;

    std::vector<stolen_emote> emotes;

    /// How many stickers each server had when the request was made, and when
    /// one was added since. Empty for a message with no stickers, and a server
    /// Discord wouldn't say about is missing, so stickers aren't offered there.
    std::map<dpp::snowflake, std::uint32_t> sticker_counts;

    /// The message's stickers that can't be uploaded.
    std::size_t left_out = 0;

    /// The emote last shown, which an upload finishing redraws.
    int page = 0;

    std::chrono::steady_clock::time_point last_used;
};

/// The panel at `page`. `servers` is those it can add to, for how much room
/// each has now, and `id` is the request's.
[[nodiscard]] auto render_steal_panel(const steal_request& request, std::span<const ports::emoji_server> servers, int page,
                                      std::uint64_t id) -> dpp::message;

/// The requests being worked through, and the panel's buttons, menu and form.
///
/// A message's emotes will not fit in a custom_id, so a request is kept in
/// memory, as Fire a trigger's are. It lasts until `request_lifetime` passes
/// without being used, or the bot restarts.
class steal_panel {
public:
    using detacher = std::function<void(dpp::task<void>, std::string)>;

    static constexpr std::chrono::minutes request_lifetime{15};

    /// `detach` runs an upload to its end with nobody waiting on it. `trusted`
    /// is `trusted_guilds`: the only servers it adds to.
    steal_panel(ports::discord_gateway& discord, ports::http_client& http, const ports::clock& clock, std::vector<dpp::snowflake> trusted,
                detacher detach);

    /// Makes a request for `user`, asking Discord how many stickers each server
    /// has when there are stickers to add. `left_out` is how many of the
    /// message's stickers can't be. Gives its id.
    auto open(dpp::snowflake user, std::vector<found_emote> emotes, std::size_t left_out = 0) -> dpp::task<std::uint64_t>;

    /// The request's first page, or a note when it has gone.
    [[nodiscard]] auto first_page(std::uint64_t id) -> dpp::message;

    /// False when the view is not one of the panel's.
    auto on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool;

    /// False when the form is not the panel's.
    auto on_form(const dpp::form_submit_t& event, const ui::page_state& state) -> bool;

private:
    /// The servers in `trusted_` that the bot is in.
    [[nodiscard]] auto targets() const -> std::vector<ports::emoji_server>;

    /// The request, or null when it has gone. Forgets any whose time is up.
    /// The caller holds `mutex_`.
    auto live_request(std::uint64_t id) -> steal_request*;

    /// Adds the emote at `page`, then shows how that went under `token`.
    auto add(std::uint64_t id, int page, std::string token) -> dpp::task<void>;

    /// Why `entry` could not be added, or nothing when it was. Checks again
    /// what may have changed since the request was made.
    auto upload(dpp::snowflake user, const stolen_emote& entry) -> dpp::task<std::optional<std::string>>;

    auto download(const found_emote& emote) -> dpp::task<std::optional<std::string>>;

    ports::discord_gateway* discord_;
    ports::http_client* http_;
    const ports::clock* clock_;
    std::vector<dpp::snowflake> trusted_;
    detacher detach_;

    std::mutex mutex_;
    std::map<std::uint64_t, steal_request> requests_;
    std::uint64_t next_ = 1;
};

/// Steal this emote: a message's Apps menu command, open to everyone
/// (src/modules/emotes/docs/Steal_Emote.md §2.1).
class steal_command final : public command {
public:
    explicit steal_command(steal_panel& panel);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    command_info info_;
    steal_panel* panel_;
};

} // namespace latibot::commands
