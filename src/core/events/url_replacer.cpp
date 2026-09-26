#include "core/events/url_replacer.hpp"

#include "core/ports/clock.hpp"
#include "core/ports/discord_gateway.hpp"
#include "core/util/log.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace latibot::events {
namespace {

auto first_attempts(const std::vector<planned_link>& links) -> std::vector<watched_link> {
    std::vector<watched_link> watched;
    watched.reserve(links.size());
    for (const planned_link& link : links) {
        watched.push_back({.link = link, .attempt = 0, .progress = link_progress::waiting});
    }
    return watched;
}

auto embed_urls_of(const dpp::message& message) -> std::vector<std::string> {
    std::vector<std::string> urls;
    urls.reserve(message.embeds.size());
    for (const dpp::embed& embed : message.embeds) {
        urls.push_back(embed.url);
    }
    return urls;
}

} // namespace

auto url_replacer::operator()(const incoming_message& message) const -> stage_result {
    // Bots are heard only when a guild allows them, and even then their links
    // are theirs to post as they like. A DM has no rules to apply.
    if (message.from_bot || message.guild_id.empty()) {
        return {};
    }

    // Almost every message has no link at all, and this keeps them away from
    // the database. "://" rather than "http", because the scheme can be
    // written in any case.
    if (message.content.find("://") == std::string::npos) {
        return {};
    }

    // The author turned previews off themselves.
    if (message.embeds_suppressed) {
        return {};
    }

    // Off until somebody turns it on for this server.
    if (!rules_->enabled(message.guild_id)) {
        return {};
    }

    // Which links have a rule, after the ones the author suppressed, put in
    // code, repeated or wrote past the limit are taken out.
    const std::vector<url_rule> rules = rules_->for_guild(message.guild_id);
    std::vector<planned_link> links = plan_replacements(message.content, rules);
    if (links.empty()) {
        return {};
    }

    if (rules_->opted_out(message.guild_id, message.author_id)) {
        util::log().debug("{} opted out of URL replacement in guild {}; leaving {} link(s) alone", message.author_id, message.guild_id,
                          links.size());
        return {};
    }

    // The opt-out is checked after planning, so a message with no rule never
    // reaches the database for it. Then drop links from the end until the
    // replacement fits in one message.
    while (!links.empty() && render_replacement(first_attempts(links), attempts_per_mirror).size() > message_length_limit) {
        links.pop_back();
    }
    if (links.empty()) {
        util::log().debug("message {}: its links are too long to post even one", message.message_id);
        return {};
    }

    return {.actions = {replace_links{.guild_id = message.guild_id,
                                      .channel_id = message.channel_id,
                                      .message_id = message.message_id,
                                      .author_id = message.author_id,
                                      .links = std::move(links)}},
            .consumed = false};
}

auto post_replacement(ports::discord_gateway& discord, replacement_store& replacements, embed_tracker& tracker, ports::clock& clock,
                      replace_links request) -> dpp::task<void> {
    dpp::message ours(request.channel_id, render_replacement(first_attempts(request.links), attempts_per_mirror));
    // Suppressed notifications, as the Java bot did: a preview is not news.
    ours.set_flags(dpp::m_suppress_notifications);

    const auto sent = co_await discord.send_message(ours);
    if (!sent.ok()) {
        util::log().warn("could not post a replacement in channel {}: {}", request.channel_id, sent.error().message);
        co_return;
    }
    const dpp::snowflake ours_id = sent.value().id;

    replacements.record({.message_id = ours_id,
                         .guild_id = request.guild_id,
                         .channel_id = request.channel_id,
                         .original_message_id = request.message_id,
                         .original_author_id = request.author_id,
                         .state = replacement_state::pending,
                         .created_at = std::chrono::floor<std::chrono::seconds>(clock.now()),
                         .retried_at = std::nullopt,
                         .links = request.links});

    util::log().info("replaced {} link(s) from {} in channel {} with message {}", request.links.size(), request.author_id,
                     request.channel_id, ours_id);

    const auto suppressed = co_await discord.set_embeds_suppressed(request.channel_id, request.message_id, true);
    if (!suppressed.ok()) {
        // Almost always a missing Manage Messages, which the permission check
        // already warned about for this guild. Both previews showing is the
        // only consequence.
        util::log().debug("could not turn off the previews on message {}: {}", request.message_id, suppressed.error().message);
    }

    const std::vector<std::string> already = embed_urls_of(sent.value());
    std::vector<embed_action> actions = tracker.watch({.guild_id = request.guild_id,
                                                       .channel_id = request.channel_id,
                                                       .message_id = ours_id,
                                                       .original_message_id = request.message_id,
                                                       .links = std::move(request.links),
                                                       .per_mirror = attempts_per_mirror,
                                                       .retry = false},
                                                      already);
    co_await carry_out_embed_actions(discord, std::move(actions));
}

auto carry_out_embed_actions(ports::discord_gateway& discord, std::vector<embed_action> actions) -> dpp::task<void> {
    // One at a time, in the tracker's order, each awaited before the next,
    // so an edit and a flag change on the same message cannot cross.
    for (const embed_action& wanted : actions) {
        if (const auto* edit = std::get_if<edit_replacement>(&wanted)) {
            const auto edited = co_await discord.edit_message(build_edit(*edit));
            if (!edited.ok()) {
                util::log().warn("could not edit replacement {}: {}", edit->message_id, edited.error().message);
            }
        } else if (const auto* original = std::get_if<set_original_embeds>(&wanted)) {
            const auto changed = co_await discord.set_embeds_suppressed(original->channel_id, original->message_id, original->suppressed);
            if (!changed.ok()) {
                util::log().debug("could not turn the previews on message {} {}: {}", original->message_id,
                                  original->suppressed ? "off" : "back on", changed.error().message);
            }
        }
    }
}

auto settle_stranded(ports::discord_gateway& discord, replacement_store& replacements, const url_rule_store& rules, embed_tracker& tracker,
                     std::vector<replacement_record> stranded) -> dpp::task<void> {
    std::size_t working = 0;
    std::size_t noted = 0;
    std::size_t gone = 0;
    std::size_t later = 0;

    for (replacement_record& record : stranded) {
        const auto fetched = co_await discord.get_message(record.channel_id, record.message_id);
        if (!fetched.ok()) {
            // Waiting will not bring back a message that was deleted, or a
            // channel the bot can no longer see, and there is nothing left to
            // edit. Anything else may be passing, so it waits for next time.
            const int status = fetched.error().http_status;
            if (status == 403 || status == 404) {
                replacements.set_state(record.message_id, replacement_state::failed);
                util::log().debug("replacement {} can no longer be reached ({}); marked failed", record.message_id,
                                  fetched.error().message);
                ++gone;
            } else {
                util::log().warn("could not fetch replacement {} to settle it; trying again at the next start: {}", record.message_id,
                                 fetched.error().message);
                ++later;
            }
            continue;
        }

        // The mirrors are only for the failure note to name. They come from
        // the rules as they are now, which is also what Retry would use.
        for (planned_link& link : record.links) {
            if (const auto rule = rules.find(record.guild_id, link.domain)) {
                link.mirrors = rule->mirrors;
            }
        }

        const bool retry = record.state == replacement_state::retrying;
        std::vector<embed_action> actions = tracker.settle({.guild_id = record.guild_id,
                                                            .channel_id = record.channel_id,
                                                            .message_id = record.message_id,
                                                            .original_message_id = record.original_message_id.value_or(dpp::snowflake{}),
                                                            .links = std::move(record.links),
                                                            .per_mirror = retry ? retry_attempts_per_mirror : attempts_per_mirror,
                                                            .retry = retry},
                                                           embed_urls_of(fetched.value()));

        const bool failed = std::ranges::any_of(actions, [](const embed_action& wanted) {
            const auto* edit = std::get_if<edit_replacement>(&wanted);
            return edit != nullptr && edit->failed;
        });
        ++(failed ? noted : working);
        co_await carry_out_embed_actions(discord, std::move(actions));
    }

    if (!stranded.empty()) {
        util::log().info(
            "guild {}: settled {} replacement(s) the last run left unfinished; {} had previews, {} got Retry, {} were gone, "
            "{} wait for the next start",
            stranded.front().guild_id, stranded.size(), working, noted, gone, later);
    }
}

auto plan_retry(const replacement_store& replacements, const url_rule_store& rules, dpp::snowflake message_id, dpp::snowflake guild_id)
    -> std::variant<retry_plan, std::string> {
    const auto found = replacements.find(message_id);
    if (!found || found->guild_id != guild_id) {
        return std::string("that replacement isn't one i know about any more");
    }

    // A Retry is a replacement like any other, and turning the feature off
    // should not leave old buttons around that still post.
    if (!rules.enabled(guild_id)) {
        return std::string("link replacement has been turned off in this server");
    }

    switch (found->state) {
    case replacement_state::retrying:
        return std::string("already retrying that one");
    case replacement_state::ok:
    case replacement_state::pending:
        return std::string("that one's already working");
    case replacement_state::failed:
        break;
    }

    std::vector<planned_link> links;
    for (planned_link link : found->links) {
        if (const auto rule = rules.find(guild_id, link.domain)) {
            link.mirrors = rule->mirrors;
            links.push_back(std::move(link));
        }
    }
    if (links.empty()) {
        return std::string("there's no URL rule for that site any more, so there's nothing to retry with");
    }

    retry_plan plan{.request = {.guild_id = guild_id,
                                .channel_id = found->channel_id,
                                .message_id = message_id,
                                .original_message_id = found->original_message_id.value_or(dpp::snowflake{}),
                                .links = links,
                                .per_mirror = retry_attempts_per_mirror,
                                .retry = true},
                    .first = {.channel_id = found->channel_id,
                              .message_id = message_id,
                              .content = render_replacement(first_attempts(links), retry_attempts_per_mirror),
                              .failed = false}};
    return plan;
}

} // namespace latibot::events
