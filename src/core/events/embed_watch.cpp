#include "core/events/embed_watch.hpp"

#include "core/ports/clock.hpp"
#include "core/ui/paginator.hpp"
#include "core/util/log.hpp"
#include "core/util/url_scan.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace latibot::events {
namespace {

/// How long a preview update for a message nobody is watching yet is kept,
/// in case the watch is about to start.
constexpr std::chrono::seconds early_update_lifetime{30};

/// And how many of them, since every message update in every guild passes
/// through here and almost none of them are ours.
constexpr std::size_t early_update_limit = 256;

std::size_t at_least_one(std::size_t per_mirror) {
    return std::max<std::size_t>(per_mirror, 1);
}

/// Whether a preview's URL points at what `link` points at.
///
/// Mirrors tend to report the original site as their URL, so the host is no
/// help; the path is. A translation suffix is taken off first, since it is
/// part of the mirror's address and not of the post.
bool same_target(std::string_view embed_url, const planned_link& link) {
    const auto embed = util::split_url(embed_url);
    const auto original = util::split_url(link.original_url);
    if (!embed || !original) {
        return false;
    }

    const std::string_view wanted = util::comparable_path(original->path);
    std::string_view seen = util::comparable_path(embed->path);
    if (seen == wanted) {
        return true;
    }

    for (const mirror& candidate : link.mirrors) {
        if (!candidate.translate_suffix.empty() && seen.ends_with(candidate.translate_suffix)) {
            seen.remove_suffix(candidate.translate_suffix.size());
            return util::comparable_path(seen) == wanted;
        }
    }
    return false;
}

std::vector<std::string> urls_of(std::span<const std::string> urls) {
    return {urls.begin(), urls.end()};
}

} // namespace

dpp::message build_edit(const edit_replacement& edit) {
    dpp::message message(edit.channel_id, edit.content);
    message.id = edit.message_id;

    if (!edit.failed) {
        // Zero also clears the suppression a failure note set, which is what
        // a Retry needs before its first attempt can embed.
        message.flags = 0;
        return message;
    }

    message.flags = dpp::m_suppress_embeds;
    if (const auto id = ui::encode({.view = std::string(url_retry_view), .page = 0, .argument = edit.message_id.str()})) {
        dpp::component row;
        row.set_type(dpp::cot_action_row)
            .add_component(dpp::component().set_type(dpp::cot_button).set_style(dpp::cos_secondary).set_label("Retry").set_id(*id));
        message.add_component(row);
    }
    return message;
}

std::string current_url(const watched_link& link, std::size_t per_mirror) {
    return mirror_url(link.link, link.attempt / at_least_one(per_mirror));
}

std::string render_replacement(std::span<const watched_link> links, std::size_t per_mirror) {
    std::string content;
    for (const watched_link& link : links) {
        if (!content.empty()) {
            content.push_back('\n');
        }

        const std::string target = std::format("[_]({})", current_url(link, per_mirror));
        content += link.link.spoilered ? std::format("🔗 ||{}||", target) : std::format("🔗 {}", target);
    }
    return content;
}

std::string render_failure(std::span<const planned_link> links) {
    // Every mirror tried, across all the links, each named once and in the
    // order it was first tried.
    std::vector<std::string_view> hosts;
    for (const planned_link& link : links) {
        for (const mirror& candidate : link.mirrors) {
            if (std::ranges::find(hosts, candidate.host) == hosts.end()) {
                hosts.push_back(candidate.host);
            }
        }
    }

    // "a", "a or b", "a, b or c".
    std::string tried;
    for (std::size_t index = 0; index < hosts.size(); ++index) {
        if (index > 0) {
            tried += index + 1 == hosts.size() ? " or " : ", ";
        }
        tried += hosts[index];
    }

    return std::format(
        "🔗 couldn't get a preview for {} from {}. The original's own preview is back; press Retry to try the "
        "mirrors again.",
        links.size() == 1 ? "that link" : "those links", tried.empty() ? std::string("any mirror") : tried);
}

std::vector<bool> embedded_links(std::span<const watched_link> links, std::span<const std::string> embed_urls) {
    std::vector<bool> covered(links.size(), false);
    std::size_t unmatched = 0;

    for (const std::string& url : embed_urls) {
        // A preview that matches any link is that link's, even one already
        // covered: a post with several images gets several previews with the
        // same URL, and those must not spill over onto the next link.
        const auto owner = std::ranges::find_if(links, [&](const watched_link& candidate) { return same_target(url, candidate.link); });
        if (owner == links.end()) {
            ++unmatched;
        } else {
            covered[static_cast<std::size_t>(owner - links.begin())] = true;
        }
    }

    // What is left goes to the uncovered links in order, which is the order
    // Discord builds previews in.
    for (std::size_t index = 0; index < covered.size() && unmatched > 0; ++index) {
        if (!covered[index]) {
            covered[index] = true;
            --unmatched;
        }
    }

    return covered;
}

// --------------------------------------------------------------------------

embed_tracker::embed_tracker(replacement_store& store, ports::clock& clock, std::chrono::milliseconds timeout)
    : store_(&store), clock_(&clock), timeout_(timeout) {}

bool embed_tracker::absorb(watch_state& state, std::span<const std::string> embed_urls) {
    const std::vector<bool> covered = embedded_links(state.links, embed_urls);
    for (std::size_t index = 0; index < covered.size(); ++index) {
        if (covered[index]) {
            state.links[index].progress = link_progress::embedded;
        }
    }

    return std::ranges::none_of(state.links, [](const watched_link& link) { return link.progress == link_progress::waiting; });
}

std::vector<embed_action> embed_tracker::watch(watch_request request, std::span<const std::string> embed_urls) {
    const auto now = clock_->steady_now();
    const dpp::snowflake id = request.message_id;

    watch_state state{.request = std::move(request), .links = {}, .deadline = now + timeout_};
    for (const planned_link& link : state.request.links) {
        state.links.push_back({.link = link, .attempt = 0, .progress = link_progress::waiting});
    }

    const std::scoped_lock guard(mutex_);
    prune_early(now);

    // Previews can already be there: on the message as it was sent, or in
    // an update that arrived before this watch started (plan §21.11). Either
    // can settle the watch before it begins.
    bool settled = absorb(state, embed_urls);
    if (const auto early = early_.find(id); early != early_.end()) {
        settled = absorb(state, early->second.embed_urls);
        early_.erase(early);
    }

    if (settled) {
        return finish(state);
    }

    util::log().debug("watching message {} for {} preview(s)", id, state.links.size());
    watches_.insert_or_assign(id, std::move(state));
    return {};
}

std::vector<embed_action> embed_tracker::on_embeds(dpp::snowflake message_id, std::span<const std::string> embed_urls) {
    const std::scoped_lock guard(mutex_);

    // An update for a message nobody is watching may be for one about to be
    // watched, so it is kept for a while (plan §21.11). The buffer is capped:
    // every message update in every guild passes through here.
    const auto found = watches_.find(message_id);
    if (found == watches_.end()) {
        if (!embed_urls.empty()) {
            const auto now = clock_->steady_now();
            prune_early(now);
            if (early_.size() >= early_update_limit) {
                early_.erase(early_.begin());
            }
            early_.insert_or_assign(message_id, early_update{.embed_urls = urls_of(embed_urls), .seen = now});
        }
        return {};
    }

    if (!absorb(found->second, embed_urls)) {
        return {};
    }

    std::vector<embed_action> actions = finish(found->second);
    watches_.erase(found);
    return actions;
}

std::vector<embed_action> embed_tracker::tick() {
    const auto now = clock_->steady_now();
    std::vector<embed_action> actions;

    const std::scoped_lock guard(mutex_);
    prune_early(now);

    for (auto entry = watches_.begin(); entry != watches_.end();) {
        // Only watches whose current try has run out of time move on.
        watch_state& state = entry->second;
        if (now < state.deadline) {
            ++entry;
            continue;
        }

        // Each link still waiting moves to its next try: the same mirror
        // again until it has had `per_mirror` tries, then the next mirror.
        // A link out of mirrors has failed; links that already embedded
        // stay as they are.
        const std::size_t per_mirror = at_least_one(state.request.per_mirror);
        bool any_waiting = false;
        for (watched_link& link : state.links) {
            if (link.progress != link_progress::waiting) {
                continue;
            }
            ++link.attempt;
            if (link.attempt >= link.link.mirrors.size() * per_mirror) {
                link.progress = link_progress::failed;
            } else {
                any_waiting = true;
            }
        }

        if (any_waiting) {
            // Editing is what makes Discord fetch the preview again, even
            // when the URL is the same as last time (the second try of a
            // mirror).
            util::log().debug("no preview yet for message {}; trying again", entry->first);
            actions.emplace_back(edit_replacement{.channel_id = state.request.channel_id,
                                                  .message_id = state.request.message_id,
                                                  .content = render_replacement(state.links, per_mirror),
                                                  .failed = false});
            state.deadline = now + timeout_;
            ++entry;
            continue;
        }

        // A watch whose ending cannot be recorded is kept, to try again
        // once another timeout has passed, and the rest carry on. Letting
        // the error out would lose the actions already gathered for watches
        // this tick has erased: their Retry notes would never be posted.
        try {
            std::vector<embed_action> finished = finish(state);
            actions.insert(actions.end(), std::make_move_iterator(finished.begin()), std::make_move_iterator(finished.end()));
            entry = watches_.erase(entry);
        } catch (const std::exception& error) {
            util::log().error("could not record how message {} ended; trying again in {}: {}", entry->first, timeout_, error.what());
            state.deadline = now + timeout_;
            ++entry;
        }
    }

    return actions;
}

std::vector<embed_action> embed_tracker::finish(const watch_state& state) {
    const watch_request& request = state.request;
    const auto embedded = std::ranges::count(state.links, link_progress::embedded, &watched_link::progress);
    const auto wall_now = std::chrono::floor<std::chrono::seconds>(clock_->now());
    // An old replacement whose original was never identified has nothing to
    // switch.
    const bool has_original = !request.original_message_id.empty();
    std::vector<embed_action> actions;

    // Success is any link with a preview, not all of them: one working
    // preview is worth keeping over a failure note.
    if (embedded > 0) {
        if (request.retry) {
            store_->mark_retried(request.message_id, replacement_state::ok, wall_now);
            // The failure turned the original's previews back on; a working
            // replacement means they go off again (plan §9.4).
            if (has_original) {
                actions.emplace_back(
                    set_original_embeds{.channel_id = request.channel_id, .message_id = request.original_message_id, .suppressed = true});
            }
        } else {
            store_->set_state(request.message_id, replacement_state::ok);
        }

        if (static_cast<std::size_t>(embedded) < state.links.size()) {
            util::log().info("message {}: {} of {} link(s) got a preview; the rest are left on their last mirror", request.message_id,
                             embedded, state.links.size());
        } else {
            util::log().debug("message {}: every link has a preview{}", request.message_id, request.retry ? " after a retry" : "");
        }
        return actions;
    }

    // Nothing embedded. The original's preview comes back unless this was a
    // Retry, whose failure already turned it back on the first time.
    if (request.retry) {
        store_->mark_retried(request.message_id, replacement_state::failed, wall_now);
    } else {
        store_->set_state(request.message_id, replacement_state::failed);
        // Better the original's own preview than none at all.
        if (has_original) {
            actions.emplace_back(
                set_original_embeds{.channel_id = request.channel_id, .message_id = request.original_message_id, .suppressed = false});
        }
    }

    util::log().info("message {}: no mirror produced a preview{}; leaving a Retry button", request.message_id,
                     request.retry ? " on retry either" : "");
    actions.emplace_back(edit_replacement{
        .channel_id = request.channel_id, .message_id = request.message_id, .content = render_failure(request.links), .failed = true});
    return actions;
}

void embed_tracker::forget(dpp::snowflake message_id) {
    const std::scoped_lock guard(mutex_);
    if (watches_.erase(message_id) > 0) {
        util::log().debug("message {} was deleted while its previews were being checked", message_id);
    }
    early_.erase(message_id);
}

bool embed_tracker::watching(dpp::snowflake message_id) const {
    const std::scoped_lock guard(mutex_);
    return watches_.contains(message_id);
}

void embed_tracker::prune_early(std::chrono::steady_clock::time_point now) {
    std::erase_if(early_, [&](const auto& entry) { return now - entry.second.seen > early_update_lifetime; });
}

} // namespace latibot::events
