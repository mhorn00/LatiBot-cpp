#include "core/commands/urlrepl.hpp"

#include "core/events/embed_watch.hpp"
#include "core/ui/paginator.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <dpp/cluster.h>
#include <dpp/dispatcher.h>
#include <dpp/permissions.h>

#include <algorithm>
#include <format>
#include <utility>

namespace latibot::commands {
namespace {

/// Room for the "…and more" line under Discord's 2000 characters.
constexpr std::size_t reply_budget = 1900;

/// Autocomplete shows at most this many choices; Discord's limit is 25.
constexpr std::size_t domain_choices = 25;

dpp::message ack(std::string_view text) {
    dpp::message reply(text);
    reply.set_flags(dpp::m_ephemeral);
    return reply;
}

std::string string_option(const dpp::slashcommand_t& event, const char* name) {
    const dpp::command_value value = event.get_parameter(name);
    const auto* text = std::get_if<std::string>(&value);
    return text == nullptr ? std::string{} : *text;
}

std::string subcommand_of(const dpp::slashcommand_t& event) {
    const dpp::command_interaction interaction = event.command.get_command_interaction();
    return interaction.options.empty() ? std::string{} : interaction.options.front().name;
}

dpp::component button(dpp::component_style style, std::string_view label, const std::string& id) {
    return dpp::component().set_type(dpp::cot_button).set_style(style).set_label(std::string(label)).set_id(id);
}

/// Splits on newlines, commas and spaces, which covers the modal's one per
/// line and a slash command's single line alike.
std::vector<std::string_view> mirror_words(std::string_view text) {
    std::vector<std::string_view> words;
    std::size_t at = 0;
    while (at < text.size()) {
        at = text.find_first_not_of(" ,\t\r\n", at);
        if (at == std::string_view::npos) {
            break;
        }
        const std::size_t end = std::min(text.find_first_of(" ,\t\r\n", at), text.size());
        words.push_back(text.substr(at, end - at));
        at = end;
    }
    return words;
}

/// What the invoker is allowed to do in this channel, from the interaction
/// itself. Empty when Discord did not say, which only happens in a DM.
dpp::permission invoker_permissions(const dpp::slashcommand_t& event) {
    const auto& resolved = event.command.resolved.member_permissions;
    const auto found = resolved.find(event.command.get_issuing_user().id);
    return found == resolved.end() ? dpp::permission{} : found->second;
}

/// Keeps a reply under Discord's limit, line by line, saying what was cut.
void append_line(std::string& out, std::string_view line, std::size_t& dropped) {
    if (dropped > 0 || out.size() + line.size() + 1 > reply_budget) {
        ++dropped;
        return;
    }
    out += line;
    out.push_back('\n');
}

} // namespace

std::string describe_mirrors(std::span<const events::mirror> mirrors) {
    std::string text;
    for (const events::mirror& entry : mirrors) {
        if (!text.empty()) {
            text += ", ";
        }
        text += events::format_mirror(entry);
    }
    return text;
}

std::string describe(const events::url_rule& rule) {
    return std::format("**{}** → {}", rule.domain, describe_mirrors(rule.mirrors));
}

std::string describe_state(bool enabled) {
    return enabled ? "Link replacement is **on** in this server."
                   : "Link replacement is **off** in this server, so nothing is replaced yet.";
}

bool switch_url_replacement(events::url_rule_store& store, dpp::snowflake guild_id, bool enabled, const user_label& who,
                            std::string_view from) {
    if (store.enabled(guild_id) == enabled) {
        return false;
    }
    store.set_enabled(guild_id, enabled);
    util::log().info("URL replacement turned {} in guild {} by {}{}", enabled ? "on" : "off", guild_id, who, from);
    return true;
}

std::string render_switch(bool changed, bool enabled, std::size_t rule_count) {
    if (!changed) {
        return std::format("Link replacement was already {} here.", enabled ? "on" : "off");
    }
    if (!enabled) {
        return "Link replacement is off in this server. The rules are kept, so `/urlrepl enable` picks up where it left off.";
    }
    if (rule_count == 0) {
        return "Link replacement is on in this server, but there are no rules yet. Add one with `/urlrepl set`.";
    }
    const std::string applying = rule_count == 1 ? std::string("Its one rule applies") : std::format("Its {} rules apply", rule_count);
    return std::format("Link replacement is on in this server. {} from now on; `/urlrepl list` shows them.", applying);
}

std::variant<events::url_rule, std::string> build_rule(std::string_view domain_text, std::string_view mirrors_text) {
    const auto domain = events::normalise_domain(domain_text);
    if (!domain || domain->find('.') == std::string::npos) {
        return std::format("\"{}\" doesn't look like a site; try something like x.com", util::trim(domain_text));
    }

    events::url_rule rule{.domain = *domain, .mirrors = {}};
    for (const std::string_view word : mirror_words(mirrors_text)) {
        const auto entry = events::parse_mirror(word);
        if (!entry || entry->host.find('.') == std::string::npos) {
            return std::format("\"{}\" doesn't look like a mirror; try something like fxtwitter.com", word);
        }
        if (entry->host == rule.domain) {
            return std::format("{} can't be its own mirror", rule.domain);
        }
        // A mirror listed twice would only be tried twice as long.
        if (std::ranges::find(rule.mirrors, entry->host, &events::mirror::host) == rule.mirrors.end()) {
            rule.mirrors.push_back(*entry);
        }
    }

    if (rule.mirrors.empty()) {
        return std::string("a rule needs at least one mirror to send links to");
    }
    if (rule.mirrors.size() > max_mirrors_per_rule) {
        return std::format("that's {} mirrors; {} is the most one rule takes", rule.mirrors.size(), max_mirrors_per_rule);
    }
    return rule;
}

std::string render_test(std::string_view content, std::span<const events::url_rule> rules, bool opted_out, bool enabled) {
    const std::vector<events::link_verdict> verdicts = events::explain_links(content, rules);
    if (verdicts.empty()) {
        return "There are no links in that.";
    }

    std::vector<events::watched_link> posted;
    for (const events::link_verdict& verdict : verdicts) {
        if (verdict.decision == events::link_decision::replaced) {
            posted.push_back({.link = verdict.link, .attempt = 0, .progress = events::link_progress::waiting});
        }
    }

    std::string reply;
    if (posted.empty()) {
        reply = "**Nothing would be posted.**\n";
    } else {
        // In a code block, so what shows is the message itself rather than
        // its previews.
        reply = std::format("**Would post:**\n```\n{}\n```\n", events::render_replacement(posted, events::attempts_per_mirror));
    }

    std::size_t dropped = 0;
    for (const events::link_verdict& verdict : verdicts) {
        std::string line = std::format("- `{}`: ", verdict.link.original_url);
        if (verdict.decision == events::link_decision::replaced) {
            line += std::format("replaced using the {} rule, trying {}{}", verdict.link.domain, describe_mirrors(verdict.link.mirrors),
                                verdict.link.spoilered ? " (spoilered, so the replacement is too)" : "");
        } else if (verdict.decision == events::link_decision::no_rule) {
            line += std::format("no rule for {}", verdict.link.domain);
        } else {
            line += events::to_string(verdict.decision);
        }
        append_line(reply, line, dropped);
    }
    if (dropped > 0) {
        reply += std::format("…and {} more\n", dropped);
    }

    if (!enabled && !posted.empty()) {
        reply += "\nLink replacement is off in this server, so nothing is posted until it's turned on with `/urlrepl enable`.";
    }
    if (opted_out && !posted.empty()) {
        reply += "\nYou've opted out with `/urltoggle`, so messages of yours are left alone.";
    }
    return reply;
}

dpp::message render_url_rule_list(const events::url_rule_store& store, dpp::snowflake guild_id, int page) {
    const std::vector<events::url_rule> rules = store.for_guild(guild_id);
    const int current = ui::clamp_page(page, rules.size(), url_rules_per_page);
    const ui::page_range window = ui::range_for(current, rules.size(), url_rules_per_page);

    const bool enabled = store.enabled(guild_id);
    std::string body = describe_state(enabled);
    if (!enabled) {
        body += " Turn it on with `/urlrepl enable`.";
    }
    body += "\n\n";

    if (rules.empty()) {
        body += "No URL rules here yet. Add one with `/urlrepl set`.";
    } else {
        for (std::size_t index = window.begin; index < window.end; ++index) {
            body += describe(rules[index]);
            body.push_back('\n');
        }
        body += std::format("\n_{}_", ui::page_label(current, rules.size(), url_rules_per_page));
    }

    dpp::message reply(body);
    reply.set_flags(dpp::m_ephemeral);
    if (const auto row =
            ui::controls({.view = std::string(url_list_view), .page = current, .argument = {}}, rules.size(), url_rules_per_page)) {
        reply.add_component(*row);
    }
    return reply;
}

// --------------------------------------------------------------------------
// The panel
// --------------------------------------------------------------------------

namespace {

std::optional<dpp::component> pick_menu(std::span<const events::url_rule> page_of, int page, std::string_view selected) {
    const auto id = ui::encode({.view = std::string(url_pick_view), .page = page, .argument = {}});
    if (page_of.empty() || !id) {
        return std::nullopt;
    }

    dpp::component menu;
    menu.set_type(dpp::cot_selectmenu).set_placeholder("Pick a rule to edit or delete").set_id(*id);
    for (const events::url_rule& rule : page_of) {
        // A select option's description is capped at 100 characters.
        std::string mirrors = describe_mirrors(rule.mirrors);
        if (mirrors.size() > 100) {
            mirrors.resize(97);
            mirrors += "...";
        }
        menu.add_select_option(dpp::select_option(rule.domain, rule.domain, mirrors).set_default(rule.domain == selected));
    }

    dpp::component row;
    row.set_type(dpp::cot_action_row).add_component(menu);
    return row;
}

std::optional<dpp::component> selection_row(int page, std::string_view selected, bool confirming_delete) {
    if (selected.empty()) {
        return std::nullopt;
    }

    const std::string chosen(selected);
    dpp::component row;
    row.set_type(dpp::cot_action_row);

    if (confirming_delete) {
        const auto yes = ui::encode({.view = std::string(url_confirm_view), .page = page, .argument = chosen});
        const auto no = ui::encode({.view = std::string(url_panel_view), .page = page, .argument = {}});
        if (!yes || !no) {
            return std::nullopt;
        }
        row.add_component(button(dpp::cos_danger, std::format("Delete {}", chosen).substr(0, 80), *yes));
        row.add_component(button(dpp::cos_secondary, "Cancel", *no));
        return row;
    }

    const auto edit = ui::encode({.view = std::string(url_edit_view), .page = page, .argument = chosen});
    const auto del = ui::encode({.view = std::string(url_delete_view), .page = page, .argument = chosen});
    if (!edit || !del) {
        return std::nullopt;
    }
    row.add_component(button(dpp::cos_primary, "Edit", *edit));
    row.add_component(button(dpp::cos_danger, "Delete", *del));
    return row;
}

std::optional<dpp::component> footer_row(int page, std::size_t total, bool enabled) {
    dpp::component row;
    row.set_type(dpp::cot_action_row);

    if (const auto add = ui::encode({.view = std::string(url_add_view), .page = page, .argument = {}})) {
        row.add_component(button(dpp::cos_success, "Add rule", *add));
    }
    if (const auto power = ui::encode({.view = std::string(url_switch_view), .page = page, .argument = enabled ? "off" : "on"})) {
        row.add_component(
            button(enabled ? dpp::cos_secondary : dpp::cos_primary, enabled ? "Turn replacement off" : "Turn replacement on", *power));
    }
    if (const auto paging = ui::controls({.view = std::string(url_panel_view), .page = page, .argument = {}}, total, url_rules_per_page)) {
        for (const dpp::component& one : paging->components) {
            row.add_component(one);
        }
    }
    return row.components.empty() ? std::nullopt : std::optional(row);
}

} // namespace

dpp::message render_url_panel(const events::url_rule_store& store, dpp::snowflake guild_id, int page, std::string_view selected,
                              bool confirming_delete) {
    const std::vector<events::url_rule> rules = store.for_guild(guild_id);
    int current = ui::clamp_page(page, rules.size(), url_rules_per_page);

    // A rule just added or renamed may sort onto another page; follow it
    // there, so the menu can show it selected.
    if (!selected.empty()) {
        if (const auto found = std::ranges::find(rules, selected, &events::url_rule::domain); found != rules.end()) {
            current = static_cast<int>(static_cast<std::size_t>(found - rules.begin()) / url_rules_per_page);
        }
    }

    const ui::page_range window = ui::range_for(current, rules.size(), url_rules_per_page);
    const std::span<const events::url_rule> page_of(rules.data() + window.begin, window.size());
    const bool shown = std::ranges::find(page_of, selected, &events::url_rule::domain) != page_of.end();

    const bool enabled = store.enabled(guild_id);
    std::string body =
        std::format("**URL rules**\n{}\nLinks to each site are posted again on its first mirror, then the next if no preview appears.\n\n",
                    describe_state(enabled));
    if (rules.empty()) {
        body += "Nothing here yet. **Add rule** below.";
    } else {
        for (const events::url_rule& rule : page_of) {
            body += describe(rule);
            body.push_back('\n');
        }
        body += std::format("\n_{}_", ui::page_label(current, rules.size(), url_rules_per_page));
    }

    dpp::message reply(body);
    reply.set_flags(dpp::m_ephemeral);

    if (const auto menu = pick_menu(page_of, current, shown ? selected : std::string_view{})) {
        reply.add_component(*menu);
    }
    if (const auto row = selection_row(current, shown ? selected : std::string_view{}, confirming_delete)) {
        reply.add_component(*row);
    }
    if (const auto footer = footer_row(current, rules.size(), enabled)) {
        reply.add_component(*footer);
    }
    return reply;
}

dpp::interaction_modal_response url_rule_form(int page, const events::url_rule* rule) {
    const std::string argument = rule == nullptr ? std::string{} : rule->domain;
    const auto custom_id = ui::encode({.view = std::string(url_form_view), .page = page, .argument = argument});

    std::string mirrors;
    if (rule != nullptr) {
        for (const events::mirror& entry : rule->mirrors) {
            if (!mirrors.empty()) {
                mirrors.push_back('\n');
            }
            mirrors += events::format_mirror(entry);
        }
    }

    dpp::interaction_modal_response form(custom_id.value_or(std::string(url_form_view) + ":0:"),
                                         rule == nullptr ? "Add a URL rule" : "Edit a URL rule");

    form.add_component(dpp::component()
                           .set_label("Site")
                           .set_placeholder("x.com")
                           .set_id("domain")
                           .set_type(dpp::cot_text)
                           .set_text_style(dpp::text_short)
                           .set_required(true)
                           .set_max_length(100)
                           .set_default_value(argument));

    form.add_row();
    form.add_component(dpp::component()
                           // The label is capped at 45 characters (plan v4
                           // §21.4); the details go in the placeholder.
                           .set_label("Mirrors, one per line, first tried first")
                           .set_placeholder("fxtwitter.com/en\nvxtwitter.com\n\n/en asks the mirror to translate")
                           .set_id("mirrors")
                           .set_type(dpp::cot_text)
                           .set_text_style(dpp::text_paragraph)
                           .set_required(true)
                           .set_max_length(1000)
                           .set_default_value(mirrors));

    return form;
}

// --------------------------------------------------------------------------
// /urlrepl
// --------------------------------------------------------------------------

urlrepl_command::urlrepl_command(events::url_rule_store& store)
    : info_{.name = "urlrepl",
            .description = "Manage which links get posted again on a mirror with a working preview.",
            .aliases = {},
            .required_bot_permissions = dpp::p_send_messages | dpp::p_embed_links | dpp::p_manage_messages,
            .default_member_permissions = dpp::permission(dpp::p_manage_guild),
            .guild_only = true},
      store_(&store) {}

dpp::slashcommand urlrepl_command::build(const std::string& name, dpp::snowflake application_id) const {
    dpp::slashcommand payload = command::build(name, application_id);

    const dpp::command_option enable(dpp::co_sub_command, "enable", "Start replacing links in this server. It's off until turned on.");
    const dpp::command_option disable(dpp::co_sub_command, "disable", "Stop replacing links in this server. The rules are kept.");

    const dpp::command_option list(dpp::co_sub_command, "list", "Show this server's URL rules.");

    dpp::command_option set(dpp::co_sub_command, "set", "Add a rule, or replace a rule's mirrors.");
    set.add_option(dpp::command_option(dpp::co_string, "domain", "The site whose links get replaced, like x.com.", true)
                       .set_auto_complete(true)
                       .set_max_length(100));
    set.add_option(dpp::command_option(dpp::co_string, "mirrors", "In the order to try them. Add /en to one to ask it to translate.", true)
                       .set_max_length(1000));

    dpp::command_option remove(dpp::co_sub_command, "remove", "Stop replacing a site's links.");
    remove.add_option(
        dpp::command_option(dpp::co_string, "domain", "The site to stop replacing.", true).set_auto_complete(true).set_max_length(100));

    dpp::command_option test(dpp::co_sub_command, "test", "Show what would be posted for a message, without posting it.");
    test.add_option(dpp::command_option(dpp::co_string, "text", "A message, or just a link.", true).set_max_length(4000));

    const dpp::command_option panel(dpp::co_sub_command, "panel", "Open the URL rule panel.");

    payload.add_option(enable);
    payload.add_option(disable);
    payload.add_option(list);
    payload.add_option(set);
    payload.add_option(remove);
    payload.add_option(test);
    payload.add_option(panel);
    return payload;
}

void urlrepl_command::autocomplete(const dpp::autocomplete_t& event) const {
    const dpp::command_option* focused = focused_option(event.options);
    if (focused == nullptr || focused->name != "domain" || event.owner == nullptr) {
        return;
    }

    const auto* typed = std::get_if<std::string>(&focused->value);
    const std::string wanted = events::normalise_domain(typed == nullptr ? std::string_view{} : *typed).value_or(std::string{});

    dpp::interaction_response reply(dpp::ir_autocomplete_reply);
    std::size_t offered = 0;
    for (const events::url_rule& rule : store_->for_guild(event.command.guild_id)) {
        if (offered == domain_choices) {
            break;
        }
        if (wanted.empty() || rule.domain.find(wanted) != std::string::npos) {
            reply.add_autocomplete_choice(dpp::command_option_choice(rule.domain, rule.domain));
            ++offered;
        }
    }

    event.owner->interaction_response_create(event.command.id, event.command.token, reply);
}

dpp::task<void> urlrepl_command::execute(const dpp::slashcommand_t& event) {
    const std::string action = subcommand_of(event);

    if (action == "enable" || action == "disable") {
        co_await this->turn(event, action == "enable");
    } else if (action == "list") {
        co_await event.co_reply(render_url_rule_list(*store_, event.command.guild_id, 0));
    } else if (action == "set") {
        co_await this->set(event);
    } else if (action == "remove") {
        co_await this->remove(event);
    } else if (action == "test") {
        co_await this->test(event);
    } else if (action == "panel") {
        co_await event.co_reply(render_url_panel(*store_, event.command.guild_id, 0));
    } else {
        co_await event.co_reply(ack("i don't know that subcommand"));
    }
}

dpp::task<void> urlrepl_command::turn(const dpp::slashcommand_t& event, bool enabled) {
    const dpp::snowflake guild = event.command.guild_id;
    const bool changed = switch_url_replacement(*store_, guild, enabled, describe_user(event.command.get_issuing_user()), "");
    co_await event.co_reply(ack(render_switch(changed, enabled, store_->for_guild(guild).size())));
}

dpp::task<void> urlrepl_command::set(const dpp::slashcommand_t& event) {
    const auto built = build_rule(string_option(event, "domain"), string_option(event, "mirrors"));
    if (const auto* problem = std::get_if<std::string>(&built)) {
        co_await event.co_reply(ack(*problem));
        co_return;
    }

    const auto& rule = std::get<events::url_rule>(built);
    const bool existed = store_->find(event.command.guild_id, rule.domain).has_value();
    store_->set(event.command.guild_id, rule);

    util::log().info("URL rule for {} {} in guild {} by {}: {}", rule.domain, existed ? "changed" : "added", event.command.guild_id,
                     describe_user(event.command.get_issuing_user()), describe_mirrors(rule.mirrors));
    co_await event.co_reply(
        ack(std::format("{}: links to {} now go to {}", existed ? "Updated" : "Added", rule.domain, describe_mirrors(rule.mirrors))));
}

dpp::task<void> urlrepl_command::remove(const dpp::slashcommand_t& event) {
    const std::string typed = string_option(event, "domain");
    const auto domain = events::normalise_domain(typed);
    if (!domain || !store_->remove(event.command.guild_id, *domain)) {
        co_await event.co_reply(ack(std::format("there's no rule for \"{}\" here", util::trim(typed))));
        co_return;
    }

    util::log().info("URL rule for {} removed from guild {} by {}", *domain, event.command.guild_id,
                     describe_user(event.command.get_issuing_user()));
    co_await event.co_reply(ack(std::format("Links to {} will be left alone from now on.", *domain)));
}

dpp::task<void> urlrepl_command::test(const dpp::slashcommand_t& event) {
    const dpp::snowflake guild = event.command.guild_id;
    const std::vector<events::url_rule> rules = store_->for_guild(guild);
    const bool opted_out = store_->opted_out(guild, event.command.get_issuing_user().id);

    dpp::message reply(render_test(string_option(event, "text"), rules, opted_out, store_->enabled(guild)));
    // The mirror links are the point of the reply, not their previews.
    reply.set_flags(dpp::m_ephemeral | dpp::m_suppress_embeds);
    co_await event.co_reply(reply);
}

// --------------------------------------------------------------------------
// /urltoggle
// --------------------------------------------------------------------------

urltoggle_command::urltoggle_command(events::url_rule_store& store)
    : info_{.name = "urltoggle",
            .description = "Stop the bot replacing your links here, or start again.",
            .aliases = {},
            .required_bot_permissions = dpp::p_send_messages,
            .default_member_permissions = std::nullopt,
            .guild_only = true},
      store_(&store) {}

dpp::slashcommand urltoggle_command::build(const std::string& name, dpp::snowflake application_id) const {
    dpp::slashcommand payload = command::build(name, application_id);
    payload.add_option(dpp::command_option(dpp::co_user, "user", "Somebody else. Needs Manage Server.", false));
    return payload;
}

dpp::task<void> urltoggle_command::execute(const dpp::slashcommand_t& event) {
    const dpp::snowflake guild = event.command.guild_id;
    const dpp::user& invoker = event.command.get_issuing_user();

    const dpp::command_value chosen = event.get_parameter("user");
    const auto* other = std::get_if<dpp::snowflake>(&chosen);
    const dpp::snowflake target = other == nullptr ? invoker.id : *other;
    const bool self = target == invoker.id;

    if (!self && !invoker_permissions(event).can(dpp::p_manage_guild)) {
        co_await event.co_reply(ack("changing that for somebody else needs Manage Server"));
        co_return;
    }

    const bool opted_out = store_->toggle_opt_out(guild, target);
    util::log().info("URL replacement {} for {} in guild {} by {}", opted_out ? "turned off" : "turned back on", target, guild,
                     describe_user(invoker));

    std::string text;
    if (self) {
        text =
            opted_out ? "Your links will be left alone here from now on. Run this again to undo it." : "Your links will be replaced again.";
    } else {
        text = std::format("Links from <@{}> will {}.", target, opted_out ? "be left alone from now on" : "be replaced again");
    }

    // The choice is kept either way; it just has nothing to act on yet.
    if (!store_->enabled(guild)) {
        text += " Link replacement is off in this server at the moment, so nobody's links are being replaced.";
    }

    co_await event.co_reply(ack(text));
}

} // namespace latibot::commands
