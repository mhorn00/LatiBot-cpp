#include "core/events/url_replacer.hpp"

#include "core/ports/clock.hpp"
#include "core/ports/discord_gateway.hpp"
#include "core/util/log.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace latibot::events {
namespace {

std::vector<watched_link> first_attempts(const std::vector<planned_link>& links) {
    std::vector<watched_link> watched;
    watched.reserve(links.size());
    for (const planned_link& link : links) {
        watched.push_back({.link = link, .attempt = 0, .progress = link_progress::waiting});
    }
    return watched;
}

std::vector<std::string> embed_urls_of(const dpp::message& message) {
    std::vector<std::string> urls;
    urls.reserve(message.embeds.size());
    for (const dpp::embed& embed : message.embeds) {
        urls.push_back(embed.url);
    }
    return urls;
}

} // namespace

stage_result url_replacer::operator()(const incoming_message& message) const {
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

dpp::task<void> post_replacement(ports::discord_gateway& discord, replacement_store& replacements, embed_tracker& tracker,
                                 ports::clock& clock, replace_links request) {
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

dpp::task<void> carry_out_embed_actions(ports::discord_gateway& discord, std::vector<embed_action> actions) {
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

std::variant<retry_plan, std::string> plan_retry(const replacement_store& replacements, const url_rule_store& rules,
                                                 dpp::snowflake message_id, dpp::snowflake guild_id) {
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
