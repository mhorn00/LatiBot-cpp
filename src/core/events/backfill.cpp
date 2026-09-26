#include "core/events/backfill.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/ports/clock.hpp"
#include "core/ports/discord_gateway.hpp"
#include "core/util/log.hpp"
#include "core/util/url_scan.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace latibot::events {
namespace {

/// Past this many, a report's list of problems stops growing: a channel the
/// bot cannot read fails every page the same way.
constexpr std::size_t problem_limit = 20;

auto note(backfill_report& report, std::string problem) -> void {
    util::log().warn("link stats recompute: {}", problem);
    if (report.problems.size() < problem_limit) report.problems.push_back(std::move(problem));
}

/// The links an old replacement carried, with the site each mirror stood in
/// for, which is what lets the statistics be filtered by site.
///
/// The original link is not known for every format, so these hold the
/// mirror link as posted; nothing retries an old replacement, so nothing
/// needs the original.
auto links_of(const legacy_match& match, const mirror_map& mirrors) -> std::vector<planned_link> {
    std::vector<planned_link> links;
    for (const std::string& url : match.mirror_urls) {
        const auto parts = util::split_url(url);
        if (!parts) continue;
        const auto site = mirrors.find(util::rule_host(parts->authority));
        if (site != mirrors.end()) links.push_back({.original_url = url, .domain = site->second, .spoilered = false, .mirrors = {}});
    }
    return links;
}

/// How the reactions endpoint wants an emoji: itself, or `name:id`.
auto emoji_for_api(const history_message::reaction_count& reaction) -> std::string {
    if (reaction.emoji_id.empty()) return reaction.emoji_name;
    return std::format("{}:{}", reaction.emoji_name.empty() ? "_" : reaction.emoji_name, reaction.emoji_id);
}

} // namespace

// --------------------------------------------------------------------------

auto backfill_progress_store::find(dpp::snowflake guild_id, dpp::snowflake channel_id, std::chrono::sys_seconds since,
                                   std::optional<std::chrono::sys_seconds> until) const -> std::optional<channel_progress> {
    auto query = db_->prepare(
        "SELECT oldest_scanned_id, complete FROM backfill_progress "
        "WHERE guild_id = ? AND channel_id = ? AND since = ? AND until IS ?",
        guild_id, channel_id, since, until);
    if (!query.step()) return std::nullopt;

    channel_progress found;
    found.oldest_scanned = query.get<std::optional<dpp::snowflake>>(0);
    found.complete = query.get<bool>(1);
    return found;
}

auto backfill_progress_store::save(dpp::snowflake guild_id, dpp::snowflake channel_id, std::chrono::sys_seconds since,
                                   std::optional<std::chrono::sys_seconds> until, const channel_progress& progress,
                                   std::chrono::sys_seconds now) -> void {
    db_->prepare(
           "INSERT INTO backfill_progress (guild_id, channel_id, since, until, oldest_scanned_id, complete, updated_at) "
           "VALUES (?, ?, ?, ?, ?, ?, ?) "
           "ON CONFLICT (guild_id, channel_id) DO UPDATE SET since = excluded.since, until = excluded.until, "
           "oldest_scanned_id = excluded.oldest_scanned_id, complete = excluded.complete, updated_at = excluded.updated_at",
           guild_id, channel_id, since, until, progress.oldest_scanned, progress.complete, now)
        .run();
}

// --------------------------------------------------------------------------

backfill_service::backfill_service(ports::discord_gateway& discord, const url_rule_store& rules, replacement_store& replacements,
                                   reaction_store& reactions, backfill_progress_store& progress, ports::clock& clock)
    : discord_(&discord), rules_(&rules), replacements_(&replacements), reactions_(&reactions), progress_(&progress), clock_(&clock) {}

auto backfill_service::begin(dpp::snowflake guild_id) -> bool {
    const std::scoped_lock guard(mutex_);
    return jobs_.try_emplace(guild_id, std::make_shared<std::atomic<bool>>(false)).second;
}

auto backfill_service::end(dpp::snowflake guild_id) -> void {
    const std::scoped_lock guard(mutex_);
    jobs_.erase(guild_id);
}

auto backfill_service::cancel(dpp::snowflake guild_id) -> bool {
    const std::scoped_lock guard(mutex_);
    const auto found = jobs_.find(guild_id);
    if (found == jobs_.end()) return false;
    found->second->store(true);
    return true;
}

auto backfill_service::running(dpp::snowflake guild_id) const -> bool {
    const std::scoped_lock guard(mutex_);
    return jobs_.contains(guild_id);
}

auto backfill_service::cancel_flag(dpp::snowflake guild_id) const -> backfill_service::flag {
    const std::scoped_lock guard(mutex_);
    const auto found = jobs_.find(guild_id);
    // A run nobody began cannot be cancelled, but it still needs a flag.
    return found == jobs_.end() ? std::make_shared<std::atomic<bool>>(false) : found->second;
}

auto backfill_service::run(backfill_request request, progress_fn progress) -> dpp::task<backfill_report> {
    backfill_report report;
    report.channels_total = request.channel_ids.size();

    // Every mirror this guild has ever used, so a rule removed years ago
    // still identifies the messages it produced.
    const mirror_map mirrors = rules_->known_mirrors(request.guild_id);
    if (mirrors.empty()) {
        note(report, "no mirrors are known here, so no replacement can be recognised; add the rules first");
        co_return report;
    }

    const flag cancelled = cancel_flag(request.guild_id);
    std::int64_t next_progress = progress_interval;

    util::log().info("link stats recompute in guild {}: {} channel(s), {} mirror host(s) known", request.guild_id,
                     request.channel_ids.size(), mirrors.size());

    for (const dpp::snowflake channel : request.channel_ids) {
        if (cancelled->load()) {
            report.cancelled = true;
            break;
        }

        co_await scan_channel({.request = &request, .channel_id = channel, .mirrors = &mirrors, .cancelled = cancelled}, report, progress,
                              next_progress);
        if (report.cancelled) break;
        ++report.channels_done;
    }

    util::log().info("link stats recompute in guild {} {}: {} scanned, {} replacements ({} attributed, {} not), {} unparsed, {} reactions",
                     request.guild_id, report.cancelled ? "stopped" : "finished", report.scanned, report.replacements, report.attributed,
                     report.unattributed, report.unparsed.size(), report.reactions);
    co_return report;
}

auto backfill_service::page_before(dpp::snowflake channel_id, dpp::snowflake before, backfill_report& report)
    -> dpp::task<std::optional<std::vector<history_message>>> {
    const auto page = co_await discord_->get_messages(channel_id, before, history_page_size);
    if (!page.ok()) {
        // Most often a channel the bot cannot read, which the permission
        // check names; say which one here.
        note(report, std::format("could not read channel {}: {}", channel_id, page.error().message));
        co_return std::nullopt;
    }

    std::vector<history_message> described;
    described.reserve(page.value().size());
    for (const dpp::message& message : page.value()) {
        described.push_back(describe_history(message));
    }
    co_return described;
}

auto backfill_service::starting_point(const channel_scan& scan) const -> std::optional<dpp::snowflake> {
    const backfill_request& request = *scan.request;
    const auto saved = request.fresh ? std::nullopt : progress_->find(request.guild_id, scan.channel_id, request.since, request.until);
    if (saved && saved->complete) return std::nullopt;

    // Newest first from the end of the range, or from where an earlier run
    // stopped.
    if (saved && saved->oldest_scanned) return *saved->oldest_scanned;
    return request.until ? first_id_at(*request.until) : dpp::snowflake{};
}

auto backfill_service::save_progress(const channel_scan& scan, channel_progress progress) -> void {
    progress_->save(scan.request->guild_id, scan.channel_id, scan.request->since, scan.request->until, progress,
                    std::chrono::floor<std::chrono::seconds>(clock_->now()));
}

auto backfill_service::scan_page(const channel_scan& scan, std::vector<history_message> page, backfill_report& report)
    -> dpp::task<std::optional<std::vector<history_message>>> {
    // The page after this one too, since the original a replacement answered
    // can be the first message of the next page. It is also the next page.
    std::vector<history_message> next;
    if (page.size() == history_page_size) {
        auto fetched = co_await page_before(scan.channel_id, page.back().id, report);
        if (!fetched) co_return std::nullopt;
        next = std::move(*fetched);
    }

    // Only this page's own messages are considered here; the next page is
    // appended so each one can look further back than the page's end. The
    // span over `page` stays valid across every await below, since this
    // frame owns `page` and waits on each `consider`.
    const std::size_t own = page.size();
    const dpp::snowflake oldest = page.back().id;
    page.insert(page.end(), next.begin(), next.end());
    const std::span<const history_message> window(page);

    // Newest first, so the first message older than `since` ends the range,
    // and the rest of the channel with it.
    bool reached_since = false;
    for (std::size_t index = 0; index < own; ++index) {
        if (created_at(window[index].id) < scan.request->since) {
            reached_since = true;
            break;
        }
        ++report.scanned;
        co_await consider(scan, window[index], window.subspan(index + 1), report);
    }

    // Saved after every page, so a cancel or a restart loses at most one
    // page. An empty result tells the caller this channel is finished.
    const bool done = reached_since || next.empty();
    save_progress(scan, {.oldest_scanned = oldest, .complete = done});
    if (done) next.clear();
    co_return std::move(next);
}

auto backfill_service::scan_channel(channel_scan scan, backfill_report& report, const progress_fn& progress, std::int64_t& next_progress)
    -> dpp::task<void> {
    const auto start = starting_point(scan);
    if (!start) {
        util::log().debug("link stats recompute: channel {} was finished by an earlier run", scan.channel_id);
        co_return;
    }

    auto first = co_await page_before(scan.channel_id, *start, report);
    if (!first) co_return;
    if (first->empty()) {
        save_progress(scan, {.oldest_scanned = std::nullopt, .complete = true});
        co_return;
    }

    std::vector<history_message> page = std::move(*first);
    while (!page.empty()) {
        if (scan.cancelled->load()) {
            report.cancelled = true;
            co_return;
        }

        // Empty once the range or the channel runs out.
        auto next = co_await scan_page(scan, std::move(page), report);
        if (!next) co_return;

        if (progress && report.scanned >= next_progress) {
            co_await progress(report);
            next_progress = report.scanned + progress_interval;
        }

        page = std::move(*next);
    }
}

auto backfill_service::consider(const channel_scan& scan, const history_message& message, std::span<const history_message> older,
                                backfill_report& report) -> dpp::task<void> {
    const backfill_request& request = *scan.request;
    const legacy_match match = classify(message, request.bot_id, *scan.mirrors);

    switch (match.what) {
    case legacy_match::kind::not_ours:
        co_return;
    case legacy_match::kind::webhook:
        ++report.webhooks_skipped;
        co_return;
    case legacy_match::kind::unrecognised:
        util::log().info("link stats recompute: message {} in channel {} looks like a replacement in no known format", message.id,
                         scan.channel_id);
        report.unparsed.push_back(message.id);
        co_return;
    case legacy_match::kind::recognised:
        break;
    }
    ++report.replacements;

    // A replacement the bot recorded as it posted it already knows whose it
    // was; only an old or unattributed one is worked out from the history.
    auto existing = replacements_->find(message.id);
    std::optional<dpp::snowflake> author = existing ? existing->original_author_id : std::nullopt;

    if (!author) {
        // Work out whose link it was from the surrounding history, fetching
        // a replied-to original that is older than the pages in hand.
        attribution found = attribute(message, match, older, request.bot_id);
        if (found.needs_fetch && found.original_message_id) {
            const auto original = co_await discord_->get_message(scan.channel_id, *found.original_message_id);
            if (original.ok()) found.author_id = original.value().author.id;
        }
        if (found.skipped_a_link) {
            util::log().debug("link stats recompute: message {} answered a link further back than the nearest one", message.id);
        }
        if (found.mismatched && !found.author_id) {
            // Reported rather than accepted (plan §9.7): crediting the
            // nearest link regardless would credit the wrong person.
            ++report.mismatched;
            util::log().info(
                "link stats recompute: message {} in channel {} follows links that are not the one it replaced; left "
                "unattributed",
                message.id, scan.channel_id);
        }

        // Recorded even when nobody could be credited: the reactions still
        // count as given, and a later run may attribute it. Links the bot
        // recorded itself are kept over the ones read back from the text.
        author = found.author_id;
        replacements_->record({.message_id = message.id,
                               .guild_id = request.guild_id,
                               .channel_id = scan.channel_id,
                               .original_message_id = found.original_message_id,
                               .original_author_id = found.author_id,
                               .state = replacement_state::ok,
                               .created_at = created_at(message.id),
                               .retried_at = std::nullopt,
                               .links = existing && !existing->links.empty() ? existing->links : links_of(match, *scan.mirrors)});
    }

    if (author) {
        ++report.attributed;
    } else {
        ++report.unattributed;
        util::log().debug("link stats recompute: nobody to credit for message {} in channel {}", message.id, scan.channel_id);
    }

    // Finally the reactions, rebuilt to match what Discord shows now.
    const auto seen = co_await reactors(scan.channel_id, message);
    if (!seen) {
        note(report, std::format("could not read the reactions on message {}; its old counts are kept", message.id));
        co_return;
    }
    report.reactions += reactions_->replace_for_message(message.id, *seen);
}

auto backfill_service::reactors(dpp::snowflake channel_id, const history_message& message)
    -> dpp::task<std::optional<std::vector<reaction_store::observed>>> {
    std::vector<reaction_store::observed> seen;

    for (const history_message::reaction_count& reaction : message.reactions) {
        if (reaction.count == 0) continue;

        const emoji_ref emoji = reaction_emoji(reaction.emoji_id, reaction.emoji_name);
        reactions_->remember(emoji);

        // Paged by user id, since the message only carries a count. Discord
        // never says when any of them reacted (plan §9.7).
        dpp::snowflake after{};
        while (true) {
            const auto page =
                co_await discord_->get_reaction_users(channel_id, message.id, emoji_for_api(reaction), after, reactor_page_size);
            if (!page.ok()) co_return std::nullopt;
            for (const dpp::snowflake user_id : page.value()) {
                seen.push_back({.user_id = user_id, .emoji_key = emoji.key});
                after = std::max(after, user_id);
            }
            if (page.value().size() < reactor_page_size) break;
        }
    }

    co_return seen;
}

} // namespace latibot::events
