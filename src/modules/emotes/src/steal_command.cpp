#include "steal_command.hpp"

#include "core/ui/interaction.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "core/util/url_scan.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <utility>

namespace latibot::commands {
namespace {

/// One emote to a page: each has its own name and server to choose.
constexpr std::size_t emotes_per_page = 1;

/// Discord's limits on an emote's image and a sticker's, and on a select menu's
/// options.
constexpr std::size_t emote_image_limit = 256 * 1024;
constexpr std::size_t sticker_image_limit = 512 * 1024;
constexpr std::size_t menu_limit = 25;

constexpr std::string_view expired_reply = "That list has expired; right-click the message and pick Steal this emote again.";

auto read_id(std::string_view text) -> std::uint64_t {
    std::uint64_t value = 0;
    const auto [stop, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && stop == text.data() + text.size() ? value : 0;
}

using sticker_counts = std::map<dpp::snowflake, std::uint32_t>;

auto image_url(const found_emote& emote) -> std::string {
    if (emote.sticker) {
        // Discord serves GIF stickers from its media host alone.
        return *emote.sticker == dpp::sf_gif ? std::format("https://media.discordapp.net/stickers/{}.gif", emote.id)
                                             : std::format("https://cdn.discordapp.com/stickers/{}.png", emote.id);
    }
    return std::format("https://cdn.discordapp.com/emojis/{}.{}", emote.id, emote.animated ? "gif" : "png");
}

auto kind_of(const found_emote& emote) -> std::string_view {
    if (emote.sticker) return "sticker";
    return emote.animated ? "animated" : "still";
}

auto slots_for(const ports::emoji_server& server, const found_emote& emote) -> std::uint32_t {
    return emote.sticker ? server.sticker_slots : server.slots;
}

/// `counts` holds this server, for a sticker.
auto room_for(const ports::emoji_server& server, const found_emote& emote, const sticker_counts& counts) -> std::uint32_t {
    std::uint32_t used = emote.animated ? server.animated_emoji : server.still_emoji;
    if (emote.sticker) used = counts.at(server.id);
    const std::uint32_t slots = slots_for(server, emote);
    return slots > used ? slots - used : 0;
}

auto describe_room(const ports::emoji_server& server, const found_emote& emote, const sticker_counts& counts) -> std::string {
    return std::format("{} of {} {} slots free", room_for(server, emote, counts), slots_for(server, emote), kind_of(emote));
}

auto button(dpp::component_style style, std::string_view label, const std::string& id) -> dpp::component {
    return dpp::component().set_type(dpp::cot_button).set_style(style).set_label(std::string(label)).set_id(id);
}

auto server_name(std::span<const ports::emoji_server> servers, dpp::snowflake id) -> std::string {
    const auto found = std::ranges::find(servers, id, &ports::emoji_server::id);
    return found == servers.end() ? std::string("that server") : found->name;
}

/// The name form, filled with what it will be called now.
auto name_form(int page, std::string_view argument, const stolen_emote& entry) -> dpp::interaction_modal_response {
    const bool sticker = entry.emote.sticker.has_value();
    const auto custom_id = ui::encode({.view = std::string(steal_form_view), .page = page, .argument = std::string(argument)});
    dpp::interaction_modal_response form(custom_id.value_or(std::string(steal_form_view) + ":0:"),
                                         sticker ? "Name this sticker" : "Name this emote");
    form.add_component(dpp::component()
                           .set_label(sticker ? "Name" : "Name: letters, numbers and _")
                           .set_placeholder(sticker ? "Party cat" : "party_cat")
                           .set_id("name")
                           .set_type(dpp::cot_text)
                           .set_text_style(dpp::text_short)
                           .set_required(true)
                           .set_min_length(2)
                           .set_max_length(sticker ? 30 : 32)
                           .set_default_value(entry.name));
    return form;
}

} // namespace

auto find_emotes(std::string_view content) -> std::vector<found_emote> {
    std::vector<found_emote> found;
    const std::vector<util::text_span> code = util::code_spans(content);

    // <:name:id> or <a:name:id>, with no spaces anywhere in it.
    for (std::size_t at = content.find('<'); at != std::string_view::npos; at = content.find('<', at + 1)) {
        const auto close = content.find('>', at);
        if (close == std::string_view::npos) break;
        std::string_view inside = content.substr(at + 1, close - at - 1);

        const bool animated = inside.starts_with("a:");
        if (animated) inside.remove_prefix(1);
        if (!inside.starts_with(':')) continue;
        inside.remove_prefix(1);

        const auto colon = inside.find(':');
        if (colon == std::string_view::npos) continue;
        const std::string_view name = inside.substr(0, colon);
        const std::uint64_t id = read_id(inside.substr(colon + 1));
        if (id == 0 || !valid_emote_name(name)) continue;

        const bool in_code = std::ranges::any_of(code, [at](const util::text_span& span) { return at >= span.begin && at < span.end; });
        if (in_code || std::ranges::any_of(found, [id](const found_emote& seen) { return seen.id == dpp::snowflake{id}; })) continue;
        found.push_back({.id = dpp::snowflake{id}, .name = std::string(name), .animated = animated, .sticker = std::nullopt});
    }
    return found;
}

auto find_stickers(std::span<const dpp::sticker> stickers) -> std::vector<found_emote> {
    std::vector<found_emote> found;
    for (const dpp::sticker& sticker : stickers) {
        if (sticker.format_type == dpp::sf_lottie) continue;
        found.push_back({.id = sticker.id, .name = sticker.name, .animated = false, .sticker = sticker.format_type});
    }
    return found;
}

auto valid_sticker_name(std::string_view name) -> bool {
    const std::size_t length = util::character_count(name);
    if (length < 2 || length > 30) return false;
    return std::ranges::none_of(name, [](char letter) { return std::iscntrl(static_cast<unsigned char>(letter)) != 0; });
}

auto valid_emote_name(std::string_view name) -> bool {
    if (name.size() < 2 || name.size() > 32) return false;
    return std::ranges::all_of(name, [](char letter) { return std::isalnum(static_cast<unsigned char>(letter)) != 0 || letter == '_'; });
}

auto render_steal_panel(const steal_request& request, std::span<const ports::emoji_server> servers, int page, std::uint64_t id)
    -> dpp::message {
    const int current = ui::clamp_page(page, request.emotes.size(), emotes_per_page);
    const stolen_emote& entry = request.emotes.at(static_cast<std::size_t>(current));
    const std::string argument = std::to_string(id);

    const bool sticker = entry.emote.sticker.has_value();

    // Where this one can go: by name, and without what has lost its room or
    // has it already.
    std::vector<const ports::emoji_server*> open;
    std::size_t full = 0;
    for (const ports::emoji_server& server : servers) {
        if (!server.bot_can_add || std::ranges::find(entry.added_to, server.id) != entry.added_to.end()) continue;
        if (sticker && !request.sticker_counts.contains(server.id)) continue;
        if (room_for(server, entry.emote, request.sticker_counts) == 0) {
            ++full;
        } else {
            open.push_back(&server);
        }
    }
    std::ranges::sort(open, {}, [](const ports::emoji_server* server) { return server->name; });

    const auto chosen = std::ranges::find_if(open, [&](const ports::emoji_server* server) { return server->id == entry.server; });
    const bool picked = chosen != open.end();

    std::string body = sticker ? std::format("**Steal this sticker**\nCalled `{}`.\n", entry.name)
                               : std::format("**Steal this emote**\nCalled `{}`, {}.\n", entry.name, kind_of(entry.emote));
    if (picked) {
        body += std::format("Goes to **{}**, with {}.\n", (*chosen)->name, describe_room(**chosen, entry.emote, request.sticker_counts));
    } else if (!open.empty()) {
        body += "Pick a server to add it to.\n";
    } else if (full == 0 && entry.added_to.empty()) {
        body +=
            "There's nowhere to add it: I add only to servers in `trusted_guilds` in my config, and need Create or Manage Expressions "
            "there.\n";
    } else {
        body += "None of my servers can take this one.\n";
    }
    body += std::format("_{}_", ui::page_label(current, request.emotes.size(), emotes_per_page));
    if (full > 0) body += std::format("\n-# {} of my servers have no {} slots free.", full, kind_of(entry.emote));
    if (open.size() > menu_limit) body += std::format("\n-# Only the first {} of {} servers fit in the menu.", menu_limit, open.size());
    if (request.left_out > 0)
        body += std::format("\n-# Left out {} Lottie sticker(s): only verified servers can upload those.", request.left_out);
    if (!entry.note.empty()) body += std::format("\n-# {}", entry.note);

    dpp::message reply(body);
    reply.add_embed(dpp::embed().set_image(image_url(entry.emote)));

    const auto pick = ui::encode({.view = std::string(steal_pick_view), .page = current, .argument = argument});
    if (pick && !open.empty()) {
        dpp::component menu;
        menu.set_type(dpp::cot_selectmenu).set_placeholder("Pick a server").set_id(*pick);
        for (std::size_t index = 0; index < std::min(menu_limit, open.size()); ++index) {
            const ports::emoji_server& server = *open[index];
            menu.add_select_option(dpp::select_option(util::truncate(server.name, 100), std::to_string(server.id),
                                                      describe_room(server, entry.emote, request.sticker_counts))
                                       .set_default(server.id == entry.server));
        }
        dpp::component row;
        row.set_type(dpp::cot_action_row).add_component(menu);
        reply.add_component(row);
    }

    // ◀ and ▶ when there is more than one emote, then what to do with this one.
    dpp::component row;
    row.set_type(dpp::cot_action_row);
    if (const auto paging = ui::controls({.view = std::string(steal_page_view), .page = current, .argument = argument},
                                         request.emotes.size(), emotes_per_page)) {
        row.components = paging->components;
    }
    if (const auto name = ui::encode({.view = std::string(steal_name_view), .page = current, .argument = argument})) {
        row.add_component(button(dpp::cos_secondary, "Name", *name));
    }
    if (const auto add = ui::encode({.view = std::string(steal_add_view), .page = current, .argument = argument})) {
        std::string label = "Add";
        if (entry.adding) {
            label = "Adding…";
        } else if (picked) {
            label = util::truncate(std::format("Add to {}", (*chosen)->name), 80);
        }
        row.add_component(button(dpp::cos_success, label, *add).set_disabled(entry.adding || !picked));
    }
    if (!row.components.empty()) reply.add_component(row);
    return reply;
}

// --------------------------------------------------------------------------
// The panel
// --------------------------------------------------------------------------

steal_panel::steal_panel(ports::discord_gateway& discord, ports::http_client& http, const ports::clock& clock,
                         std::vector<dpp::snowflake> trusted, detacher detach)
    : discord_(&discord), http_(&http), clock_(&clock), trusted_(std::move(trusted)), detach_(std::move(detach)) {}

auto steal_panel::targets() const -> std::vector<ports::emoji_server> {
    std::vector<ports::emoji_server> servers = discord_->emoji_servers();
    std::erase_if(servers, [this](const ports::emoji_server& server) { return std::ranges::find(trusted_, server.id) == trusted_.end(); });
    return servers;
}

auto steal_panel::open(dpp::snowflake user, std::vector<found_emote> emotes, std::size_t left_out) -> dpp::task<std::uint64_t> {
    steal_request request;
    request.user = user;
    request.left_out = left_out;

    // The cache doesn't hold stickers, so these are asked of Discord.
    if (std::ranges::any_of(emotes, [](const found_emote& emote) { return emote.sticker.has_value(); })) {
        const std::vector<ports::emoji_server> servers = targets();
        for (const ports::emoji_server& server : servers) {
            if (!server.bot_can_add) continue;
            const auto count = co_await discord_->guild_sticker_count(server.id);
            if (!count.has_value()) {
                util::log().warn("could not count the stickers of guild {}: {}", server.id, count.error().message);
                continue;
            }
            request.sticker_counts[server.id] = count.value();
        }
    }

    for (found_emote& emote : emotes) {
        std::string name = emote.name;
        request.emotes.push_back(
            {.emote = std::move(emote), .name = std::move(name), .server = {}, .added_to = {}, .note = {}, .adding = false});
    }

    const std::scoped_lock lock(mutex_);
    request.last_used = clock_->steady_now();
    const std::uint64_t id = next_++;
    requests_.emplace(id, std::move(request));
    co_return id;
}

auto steal_panel::live_request(std::uint64_t id) -> steal_request* {
    const auto now = clock_->steady_now();
    std::erase_if(requests_, [now](const auto& each) { return now - each.second.last_used > request_lifetime; });

    const auto found = requests_.find(id);
    if (found == requests_.end()) return nullptr;
    found->second.last_used = now;
    return &found->second;
}

auto steal_panel::first_page(std::uint64_t id) -> dpp::message {
    const std::vector<ports::emoji_server> servers = targets();
    const std::scoped_lock lock(mutex_);
    const steal_request* request = live_request(id);
    return request == nullptr ? dpp::message(std::string(expired_reply)) : render_steal_panel(*request, servers, 0, id);
}

auto steal_panel::on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool {
    if (state.view != steal_page_view && state.view != steal_pick_view && state.view != steal_name_view && state.view != steal_add_view) {
        return false;
    }

    const std::uint64_t id = read_id(state.argument);
    const std::vector<ports::emoji_server> servers = targets();

    std::optional<dpp::message> updated;
    std::optional<dpp::interaction_modal_response> form;
    bool adding = false;
    {
        const std::scoped_lock lock(mutex_);
        steal_request* request = live_request(id);
        if (request != nullptr) {
            const int page = ui::clamp_page(state.page, request->emotes.size(), emotes_per_page);
            request->page = page;
            stolen_emote& entry = request->emotes.at(static_cast<std::size_t>(page));

            if (state.view == steal_name_view) {
                form = name_form(page, state.argument, entry);
            } else {
                if (state.view == steal_pick_view) {
                    entry.server = dpp::snowflake(chosen);
                    entry.note.clear();
                } else if (state.view == steal_add_view && !entry.server.empty() && !entry.adding) {
                    entry.adding = true;
                    entry.note.clear();
                    adding = true;
                }
                updated = render_steal_panel(*request, servers, page, id);
            }
        }
    }

    if (form) {
        event.dialog(*form);
    } else {
        ui::update_panel(event, updated ? std::move(*updated) : dpp::message(std::string(expired_reply)));
    }

    // After the answer, since an upload can take longer than an answer may:
    // it shows how it went by editing the panel with this press's token.
    if (adding) detach_(add(id, state.page, event.command.token), "adding a stolen emote");
    return true;
}

auto steal_panel::on_form(const dpp::form_submit_t& event, const ui::page_state& state) -> bool {
    if (state.view != steal_form_view) return false;

    const ui::form_values fields = ui::form_fields(event);
    const auto typed = fields.find("name");
    const std::string name(util::trim(typed == fields.end() ? std::string_view{} : std::string_view(typed->second)));

    const std::uint64_t id = read_id(state.argument);
    const std::vector<ports::emoji_server> servers = targets();

    std::optional<dpp::message> updated;
    std::optional<std::string> refused;
    {
        const std::scoped_lock lock(mutex_);
        if (steal_request* request = live_request(id)) {
            const int page = ui::clamp_page(state.page, request->emotes.size(), emotes_per_page);
            request->page = page;
            stolen_emote& entry = request->emotes.at(static_cast<std::size_t>(page));
            if (entry.emote.sticker && !valid_sticker_name(name)) {
                refused = std::format("\"{}\" won't do: a sticker's name is 2 to 30 characters", name);
            } else if (!entry.emote.sticker && !valid_emote_name(name)) {
                refused = std::format("\"{}\" won't do: a name is 2 to 32 letters, numbers and underscores", name);
            } else {
                entry.name = name;
                entry.note.clear();
                updated = render_steal_panel(*request, servers, page, id);
            }
        }
    }
    if (refused) {
        ui::answer_privately(event, *refused);
        return true;
    }
    ui::update_panel(event, updated ? std::move(*updated) : dpp::message(std::string(expired_reply)));
    return true;
}

auto steal_panel::add(std::uint64_t id, int page, std::string token) -> dpp::task<void> {
    dpp::snowflake user;
    std::optional<stolen_emote> entry;
    {
        const std::scoped_lock lock(mutex_);
        if (const steal_request* request = live_request(id)) {
            user = request->user;
            entry = request->emotes.at(static_cast<std::size_t>(ui::clamp_page(page, request->emotes.size(), emotes_per_page)));
        }
    }
    if (!entry) co_return;

    const std::optional<std::string> refused = co_await upload(user, *entry);
    const std::vector<ports::emoji_server> servers = targets();

    std::optional<dpp::message> updated;
    {
        const std::scoped_lock lock(mutex_);
        if (steal_request* request = live_request(id)) {
            stolen_emote& stored =
                request->emotes.at(static_cast<std::size_t>(ui::clamp_page(page, request->emotes.size(), emotes_per_page)));
            stored.adding = false;
            if (refused) {
                stored.note = *refused;
            } else {
                stored.note = std::format("Added `{}` to {}.", entry->name, server_name(servers, entry->server));
                stored.added_to.push_back(entry->server);
                if (entry->emote.sticker) ++request->sticker_counts[entry->server];
                stored.server = dpp::snowflake{};
            }
            // Whichever emote they have moved on to, not the one just added.
            updated = render_steal_panel(*request, servers, request->page, id);
        }
    }
    if (!updated) co_return;

    const auto edited = co_await discord_->edit_interaction_response(std::move(token), std::move(*updated));
    if (!edited.has_value()) util::log().warn("could not show how adding emote {} went: {}", entry->emote.id, edited.error().message);
}

auto steal_panel::upload(dpp::snowflake user, const stolen_emote& entry) -> dpp::task<std::optional<std::string>> {
    const std::vector<ports::emoji_server> servers = targets();
    const auto target = std::ranges::find(servers, entry.server, &ports::emoji_server::id);
    if (target == servers.end()) co_return "i'm not in that server any more";
    if (!target->bot_can_add) co_return std::format("i can't add to {} any more", target->name);

    sticker_counts counts;
    if (entry.emote.sticker) {
        // Discord has the last word when it won't say how many there are.
        const auto count = co_await discord_->guild_sticker_count(entry.server);
        counts[entry.server] = count.value_or(0);
    }
    if (room_for(*target, entry.emote, counts) == 0) {
        co_return std::format("{} has no {} slots free any more", target->name, kind_of(entry.emote));
    }

    const auto image = co_await download(entry.emote);
    if (!image) {
        co_return std::format("i couldn't download its image, or it's over Discord's {} KiB limit", entry.emote.sticker ? 512 : 256);
    }

    // A sticker's tags are only suggestions, and its own aren't in the message.
    dpp::task<ports::result<dpp::snowflake>> creating =
        entry.emote.sticker
            ? discord_->create_guild_sticker(entry.server, entry.name, entry.name, *image, *entry.emote.sticker == dpp::sf_gif)
            : discord_->create_guild_emoji(entry.server, entry.name, *image, entry.emote.animated);
    const auto made = co_await std::move(creating);
    if (!made.has_value()) {
        util::log().warn("could not add {} {} to guild {} for {}: {}", kind_of(entry.emote), entry.emote.id, entry.server, user,
                         made.error().message);
        co_return std::format("Discord wouldn't take it: {}", made.error().message);
    }

    util::log().info("{} added {} {} to guild {} as {} ({})", user, kind_of(entry.emote), entry.emote.id, entry.server, entry.name,
                     made.value());
    co_return std::nullopt;
}

auto steal_panel::download(const found_emote& emote) -> dpp::task<std::optional<std::string>> {
    const std::size_t limit = emote.sticker ? sticker_image_limit : emote_image_limit;

    // Full size, then smaller for one too big to upload as it is.
    for (const std::string_view size : {std::string_view{}, std::string_view{"?size=96"}}) {
        const auto response = co_await http_->send({.url = image_url(emote) + std::string(size),
                                                    .method = ports::http_method::get,
                                                    .body = {},
                                                    .content_type = {},
                                                    .headers = {}});
        if (!response.has_value() || response.value().status != 200) {
            util::log().debug("could not download emote {}: {}", emote.id,
                              response.has_value() ? std::format("status {}", response.value().status) : response.error().message);
            co_return std::nullopt;
        }
        if (response.value().body.size() <= limit) co_return response.value().body;
    }
    co_return std::nullopt;
}

// --------------------------------------------------------------------------
// The command
// --------------------------------------------------------------------------

steal_command::steal_command(steal_panel& panel)
    : info_{.name = std::string(steal_command_name),
            .description = "Add this message's emotes and stickers to a server.",
            .aliases = {},
            .required_bot_permissions = 0,
            .default_member_permissions = std::nullopt,
            .guild_only = true,
            .responses = {.result = dpp::m_ephemeral, .refusal = dpp::m_ephemeral, .post = 0},
            .subcommand_responses = {}},
      panel_(&panel) {}

auto steal_command::build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand {
    // As Fire a trigger's: DPP lowercases a slash command's name, which a
    // menu's keeps as typed, so it is set again once the type says it is one.
    dpp::slashcommand payload = command::build(name, application_id);
    payload.set_type(dpp::ctxm_message);
    payload.set_name(name);
    return payload;
}

auto steal_command::execute(const dpp::slashcommand_t& event) -> dpp::task<void> {
    const auto& resolved = event.command.resolved.messages;
    if (resolved.empty()) {
        co_await event.co_reply(refusal(event, "i couldn't see which message that was"));
        co_return;
    }

    const dpp::message& message = resolved.begin()->second;
    std::vector<found_emote> emotes = find_emotes(message.content);
    const std::vector<found_emote> stickers = find_stickers(message.stickers);
    const std::size_t left_out = message.stickers.size() - stickers.size();
    emotes.insert(emotes.end(), stickers.begin(), stickers.end());
    if (emotes.empty()) {
        co_await event.co_reply(refusal(event, left_out > 0 ? "that sticker is in Lottie format, which only verified servers can upload"
                                                            : "there are no custom emotes or stickers in that message"));
        co_return;
    }

    // Counting every server's stickers can take longer than an answer may.
    co_await defer(event);
    const std::uint64_t id = co_await panel_->open(event.command.get_issuing_user().id, std::move(emotes), left_out);
    co_await answer_deferred(event, result(event, panel_->first_page(id)));
}

} // namespace latibot::commands
