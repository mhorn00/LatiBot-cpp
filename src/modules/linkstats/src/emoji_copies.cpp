#include "emoji_copies.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/ports/clock.hpp"
#include "core/ports/discord_gateway.hpp"
#include "core/ports/http_client.hpp"
#include "core/util/log.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <map>
#include <set>
#include <utility>

namespace latibot::events {
namespace {

constexpr std::array<std::string_view, 4> state_names{"fetched", "lost", "too_big", "failed"};

auto state_from_string(std::string_view name) -> image_state {
    for (std::size_t index = 0; index < state_names.size(); ++index) {
        if (state_names[index] == name) return static_cast<image_state>(index);
    }
    return image_state::failed;
}

/// Where Discord keeps emoji images. The second is its media proxy, which
/// has served images the first no longer would.
constexpr std::array<std::string_view, 2> cdn_hosts{"cdn.discordapp.com", "media.discordapp.net"};

/// Asked for when the full size is over the limit.
constexpr std::string_view smaller = "?size=96";

/// A GIF's graphic control extension: one per frame of an animated GIF.
constexpr std::string_view frame_marker = "\x21\xF9\x04";

constexpr std::string_view custom_prefix = "c:";

/// Resets a flag when a round ends, however it ends.
class round_guard {
public:
    explicit round_guard(std::atomic<bool>& flag) : flag_(&flag) {}
    round_guard(const round_guard&) = delete;
    auto operator=(const round_guard&) -> round_guard& = delete;
    ~round_guard() { flag_->store(false); }

private:
    std::atomic<bool>* flag_;
};

/// Every custom emoji's reactions, in every guild, emotes sent as reactions
/// included.
auto reactions_by_emoji(db::database& db) -> std::map<std::string, std::int64_t, std::less<>> {
    std::map<std::string, std::int64_t, std::less<>> uses;
    auto query = db.prepare("SELECT emoji_key, COUNT(*) FROM counted_reactions WHERE emoji_key LIKE 'c:%' GROUP BY emoji_key");
    while (query.step()) {
        uses.emplace(query.get<std::string>(0), query.get<std::int64_t>(1));
    }
    return uses;
}

/// What each alias counts as, in whichever guild set it first.
auto alias_rows(db::database& db) -> std::map<std::string, std::string, std::less<>> {
    std::map<std::string, std::string, std::less<>> emote_of;
    auto query = db.prepare("SELECT emoji_key, canonical_key FROM emoji_aliases ORDER BY guild_id");
    while (query.step()) {
        emote_of.emplace(query.get<std::string>(0), query.get<std::string>(1));
    }
    return emote_of;
}

struct image_row {
    image_state state = image_state::failed;
    std::optional<std::string> sha256;
    std::chrono::sys_seconds checked_at;
};

auto image_rows(db::database& db) -> std::map<std::string, image_row, std::less<>> {
    std::map<std::string, image_row, std::less<>> images;
    auto query = db.prepare("SELECT emoji_key, state, image_sha256, checked_at FROM emoji_images");
    while (query.step()) {
        images.emplace(query.get<std::string>(0), image_row{.state = state_from_string(query.get<std::string>(1)),
                                                            .sha256 = query.get<std::optional<std::string>>(2),
                                                            .checked_at = query.get<std::chrono::sys_seconds>(3)});
    }
    return images;
}

/// The copies there are, by image.
auto copy_rows(db::database& db) -> std::map<std::string, stored_copy, std::less<>> {
    std::map<std::string, stored_copy, std::less<>> copied;
    auto query = db.prepare("SELECT image_sha256, copy_id, name FROM emoji_copies");
    while (query.step()) {
        stored_copy copy{
            .image_sha256 = query.get<std::string>(0), .copy_id = query.get<dpp::snowflake>(1), .name = query.get<std::string>(2)};
        copied.emplace(copy.image_sha256, std::move(copy));
    }
    return copied;
}

/// One emote: an emoji and every emoji merged into it, the one kept first
/// and the rest most used first, with all their reactions together.
struct emote {
    std::vector<std::string> members;
    std::int64_t uses = 0;
};

/// Every emote, most used first.
auto emotes(const std::map<std::string, std::int64_t, std::less<>>& uses, const std::map<std::string, std::string, std::less<>>& emote_of)
    -> std::vector<emote> {
    const auto uses_of = [&](const std::string& key) {
        const auto counted = uses.find(key);
        return counted == uses.end() ? std::int64_t{0} : counted->second;
    };

    std::map<std::string, emote, std::less<>> by_root;
    const auto join = [&](const std::string& key) {
        const auto alias = emote_of.find(key);
        const std::string& root = alias == emote_of.end() ? key : alias->second;
        emote& found = by_root[root];
        if (std::ranges::find(found.members, key) != found.members.end()) return;
        found.members.push_back(key);
        found.uses += uses_of(key);
    };
    for (const auto& [key, count] : uses) {
        join(key);
    }
    for (const auto& [key, root] : emote_of) {
        join(key);
        join(root);
    }

    std::vector<emote> ordered;
    ordered.reserve(by_root.size());
    for (auto& [root, found] : by_root) {
        const std::string kept = root;
        std::ranges::stable_sort(found.members, [&](const std::string& lhs, const std::string& rhs) {
            if ((lhs == kept) != (rhs == kept)) return lhs == kept;
            return uses_of(lhs) > uses_of(rhs);
        });
        ordered.push_back(std::move(found));
    }
    std::ranges::stable_sort(ordered, std::ranges::greater{}, &emote::uses);
    return ordered;
}

/// Works out which of an emote's emojis to copy. Adds whatever needs
/// downloading to `fetch`, and gives the image the emote wants, once one of
/// its emojis has been had.
auto choose_image(const emote& found, const std::map<std::string, image_row, std::less<>>& images,
                  const std::map<std::string, stored_copy, std::less<>>& copied, std::chrono::sys_seconds now,
                  std::vector<std::string>& fetch) -> std::optional<std::string> {
    for (const std::string& member : found.members) {
        if (!member.starts_with(custom_prefix)) continue;

        const auto image = images.find(member);
        if (image == images.end()) {
            // Never tried: that decides it, one way or the other.
            fetch.push_back(member);
            return std::nullopt;
        }
        if (image->second.state == image_state::fetched && image->second.sha256) {
            if (!copied.contains(*image->second.sha256)) fetch.push_back(member);
            return image->second.sha256;
        }
        // Could not be had last time. Tried again once in a while, and
        // meanwhile the next of the emote's emojis may stand in for it.
        if (image->second.checked_at + retry_after(image->second.state) <= now) fetch.push_back(member);
    }
    return std::nullopt;
}

} // namespace

auto to_string(image_state state) noexcept -> std::string_view {
    const auto index = static_cast<std::size_t>(state);
    return index < state_names.size() ? state_names[index] : "failed";
}

auto retry_after(image_state state) noexcept -> std::chrono::hours {
    switch (state) {
    case image_state::failed:
        return std::chrono::hours{24};
    case image_state::lost:
    case image_state::too_big:
        return std::chrono::hours{24 * 7};
    case image_state::fetched:
        break;
    }
    return std::chrono::hours{0};
}

auto is_animated_gif(std::string_view image) noexcept -> bool {
    if (!image.starts_with("GIF8")) return false;
    std::size_t frames = 0;
    for (std::size_t at = image.find(frame_marker); at != std::string_view::npos; at = image.find(frame_marker, at + 1)) {
        if (++frames > 1) return true;
    }
    return false;
}

auto sha256_hex(std::string_view bytes) -> std::string {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    EVP_Digest(bytes.data(), bytes.size(), digest.data(), &length, EVP_sha256(), nullptr);

    std::string hex;
    hex.reserve(static_cast<std::size_t>(length) * 2);
    for (unsigned int index = 0; index < length; ++index) {
        hex += std::format("{:02x}", digest[index]);
    }
    return hex;
}

auto copy_name(std::string_view original, dpp::snowflake id) -> std::string {
    std::string name;
    for (const char letter : original) {
        if (std::isalnum(static_cast<unsigned char>(letter)) != 0 || letter == '_') name += letter;
    }
    if (name.size() < 2) {
        // Nothing usable left: named after the end of its id instead.
        const std::string digits = id.str();
        name = "emoji_" + digits.substr(digits.size() > 6 ? digits.size() - 6 : 0);
    }
    return name.substr(0, 32);
}

// --------------------------------------------------------------------------

auto emoji_copy_store::plan(std::int64_t min_uses, std::chrono::sys_seconds now) const -> copy_plan {
    copy_plan planned;
    if (min_uses <= 0) return planned;

    const std::map<std::string, std::int64_t, std::less<>> uses = reactions_by_emoji(*db_);
    const std::map<std::string, image_row, std::less<>> images = image_rows(*db_);
    const std::map<std::string, stored_copy, std::less<>> copied = copy_rows(*db_);

    std::set<std::string, std::less<>> wanted;
    for (const emote& found : emotes(uses, alias_rows(*db_))) {
        if (found.uses < min_uses) continue;
        if (auto image = choose_image(found, images, copied, now, planned.fetch)) wanted.insert(std::move(*image));
    }

    if (planned.fetch.empty()) {
        for (const auto& [sha256, copy] : copied) {
            if (!wanted.contains(sha256)) planned.prune.push_back(copy);
        }
    }
    return planned;
}

auto emoji_copy_store::facts(std::string_view emoji_key) const -> emoji_facts {
    auto query = db_->prepare("SELECT name, animated FROM emojis WHERE emoji_key = ?", emoji_key);
    if (!query.step()) return {};
    return {.name = query.get<std::string>(0), .animated = query.get<bool>(1)};
}

auto emoji_copy_store::record_image(std::string_view emoji_key, image_state state, const std::optional<std::string>& image_sha256,
                                    bool animated, std::chrono::sys_seconds now) -> void {
    db_->prepare(
           "INSERT INTO emoji_images (emoji_key, state, image_sha256, animated, checked_at) VALUES (?, ?, ?, ?, ?) "
           "ON CONFLICT (emoji_key) DO UPDATE SET state = excluded.state, image_sha256 = excluded.image_sha256, "
           "animated = excluded.animated, checked_at = excluded.checked_at",
           emoji_key, to_string(state), image_sha256, animated, now)
        .run();
}

auto emoji_copy_store::copy_for(std::string_view image_sha256) const -> std::optional<stored_copy> {
    auto query = db_->prepare("SELECT copy_id, name FROM emoji_copies WHERE image_sha256 = ?", image_sha256);
    if (!query.step()) return std::nullopt;
    return stored_copy{
        .image_sha256 = std::string(image_sha256), .copy_id = query.get<dpp::snowflake>(0), .name = query.get<std::string>(1)};
}

auto emoji_copy_store::name_taken(std::string_view name) const -> bool {
    auto query = db_->prepare("SELECT 1 FROM emoji_copies WHERE name = ?", name);
    return query.step();
}

auto emoji_copy_store::copies() const -> std::size_t {
    auto query = db_->prepare("SELECT COUNT(*) FROM emoji_copies");
    return query.step() ? static_cast<std::size_t>(query.get<std::int64_t>(0)) : 0;
}

auto emoji_copy_store::add_copy(const stored_copy& copy, bool animated, std::chrono::sys_seconds now) -> void {
    db_->prepare("INSERT INTO emoji_copies (image_sha256, copy_id, name, animated, created_at) VALUES (?, ?, ?, ?, ?)", copy.image_sha256,
                 copy.copy_id, copy.name, animated, now)
        .run();
}

auto emoji_copy_store::remove_copy(std::string_view image_sha256) -> void {
    db_->prepare("DELETE FROM emoji_copies WHERE image_sha256 = ?", image_sha256).run();
}

// --------------------------------------------------------------------------

emoji_copier::emoji_copier(emoji_copy_store& store, ports::http_client& http, ports::discord_gateway& discord, ports::clock& clock,
                           std::int64_t min_uses)
    : store_(&store), http_(&http), discord_(&discord), clock_(&clock), min_uses_(min_uses) {}

auto emoji_copier::now() const -> std::chrono::sys_seconds {
    return std::chrono::floor<std::chrono::seconds>(clock_->now());
}

auto emoji_copier::run_round(std::size_t limit) -> dpp::task<round_report> {
    round_report report;
    if (!enabled() || running_.exchange(true)) co_return report;
    const round_guard guard(running_);

    const copy_plan planned = store_->plan(min_uses_, now());
    for (std::size_t index = 0; index < std::min(limit, planned.fetch.size()); ++index) {
        co_await copy_one(planned.fetch[index], report);
    }
    for (std::size_t index = 0; index < std::min(limit, planned.prune.size()); ++index) {
        co_await prune_one(planned.prune[index], report);
    }

    if (report.copied + report.shared + report.lost + report.failed + report.pruned > 0) {
        util::log().info("emoji copies: {} uploaded, {} sharing a copy, {} lost, {} failed, {} deleted; {} left to try", report.copied,
                         report.shared, report.lost, report.failed, report.pruned,
                         planned.fetch.size() - std::min(limit, planned.fetch.size()));
    }
    co_return report;
}

auto emoji_copier::fetch_format(std::string_view host, dpp::snowflake id, std::string_view format) -> dpp::task<attempt> {
    attempt tried;
    for (const std::string_view size : {std::string_view{}, smaller}) {
        const auto response = co_await http_->send({.url = std::format("https://{}/emojis/{}.{}{}", host, id, format, size),
                                                    .method = ports::http_method::get,
                                                    .body = {},
                                                    .content_type = {},
                                                    .headers = {}});
        if (!response.has_value() || response.value().status == 429 || response.value().status >= 500) {
            tried.transient = true;
            co_return tried;
        }
        if (response.value().status != 200) co_return tried;

        const std::string& image = response.value().body;
        // A still GIF is better had as a PNG.
        if (format == "gif" && !is_animated_gif(image)) co_return tried;
        if (image.size() > emoji_image_limit) {
            tried.too_big = true;
            continue;
        }
        tried.image = download{.state = image_state::fetched, .image = image, .animated = format == "gif"};
        co_return tried;
    }
    co_return tried;
}

auto emoji_copier::fetch(dpp::snowflake id) -> dpp::task<download> {
    bool transient = false;
    bool too_big = false;

    // A GIF first, so an animated emoji keeps moving; then a PNG.
    for (const std::string_view host : cdn_hosts) {
        for (const std::string_view format : {"gif", "png"}) {
            attempt tried = co_await fetch_format(host, id, format);
            if (tried.image) co_return std::move(*tried.image);
            transient = transient || tried.transient;
            too_big = too_big || tried.too_big;
        }
    }

    // Nothing usable anywhere: gone, unless something went wrong on the way.
    image_state state = image_state::lost;
    if (too_big) {
        state = image_state::too_big;
    } else if (transient) {
        state = image_state::failed;
    }
    co_return download{.state = state, .image = {}, .animated = false};
}

auto emoji_copier::copy_one(const std::string& emoji_key, round_report& report) -> dpp::task<void> {
    const dpp::snowflake id(std::string_view(emoji_key).substr(custom_prefix.size()));
    const download got = co_await fetch(id);
    if (got.state != image_state::fetched) {
        store_->record_image(emoji_key, got.state, std::nullopt, false, now());
        util::log().info("could not copy emoji {}: its image is {}", emoji_key, to_string(got.state));
        ++(got.state == image_state::failed ? report.failed : report.lost);
        co_return;
    }

    const std::string sha256 = sha256_hex(got.image);
    if (store_->copy_for(sha256)) {
        // The same image under another id, already copied.
        store_->record_image(emoji_key, image_state::fetched, sha256, got.animated, now());
        ++report.shared;
        co_return;
    }

    if (store_->copies() >= application_emoji_limit) {
        util::log().warn("could not copy emoji {}: the bot already has Discord's limit of {} emojis", emoji_key, application_emoji_limit);
        store_->record_image(emoji_key, image_state::failed, sha256, got.animated, now());
        ++report.failed;
        co_return;
    }

    // Names are unique per application, and servers reuse them.
    const std::string base = copy_name(store_->facts(emoji_key).name, id);
    std::string name = base;
    for (int suffix = 2; store_->name_taken(name); ++suffix) {
        const std::string tail = std::format("_{}", suffix);
        name = base.substr(0, 32 - tail.size()) + tail;
    }

    const auto uploaded = co_await discord_->create_application_emoji(name, got.image, got.animated);
    if (!uploaded.has_value()) {
        util::log().warn("could not upload a copy of emoji {} as {}: {}", emoji_key, name, uploaded.error().message);
        store_->record_image(emoji_key, image_state::failed, sha256, got.animated, now());
        ++report.failed;
        co_return;
    }

    store_->add_copy({.image_sha256 = sha256, .copy_id = uploaded.value(), .name = name}, got.animated, now());
    store_->record_image(emoji_key, image_state::fetched, sha256, got.animated, now());
    util::log().info("copied emoji {} as the bot's own {} ({})", emoji_key, name, uploaded.value());
    ++report.copied;
}

auto emoji_copier::prune_one(const stored_copy& copy, round_report& report) -> dpp::task<void> {
    const auto deleted = co_await discord_->delete_application_emoji(copy.copy_id);
    // Already gone is as good as deleted.
    if (!deleted.has_value() && deleted.error().http_status != 404) {
        util::log().warn("could not delete the bot's emoji {} ({}): {}", copy.name, copy.copy_id, deleted.error().message);
        co_return;
    }
    store_->remove_copy(copy.image_sha256);
    util::log().info("deleted the bot's emoji {} ({}): nothing uses it enough any more", copy.name, copy.copy_id);
    ++report.pruned;
}

} // namespace latibot::events
