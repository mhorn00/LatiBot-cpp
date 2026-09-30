#include "core/events/media_posts.hpp"

#include "core/config/guild_settings.hpp"
#include "core/events/legacy_replacements.hpp"
#include "core/events/replacements.hpp"
#include "core/ports/clock.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <array>

namespace latibot::events {
namespace {

/// What a file is called when Discord did not say its type.
constexpr std::array<std::string_view, 10> media_extensions{".png",  ".jpg", ".jpeg", ".gif",  ".webp",
                                                            ".avif", ".mp4", ".mov",  ".webm", ".mkv"};

} // namespace

auto images_enabled(const config::guild_settings& settings, dpp::snowflake guild_id) -> bool {
    return settings.get_bool(guild_id, images_enabled_key, false);
}

auto set_images_enabled(config::guild_settings& settings, dpp::snowflake guild_id, bool enabled) -> void {
    settings.set_bool(guild_id, images_enabled_key, enabled);
}

auto is_media_attachment(std::string_view content_type, std::string_view filename) -> bool {
    if (!content_type.empty()) return content_type.starts_with("image/") || content_type.starts_with("video/");
    const std::string lowered = util::to_lower(filename);
    return std::ranges::any_of(media_extensions, [&](std::string_view extension) { return lowered.ends_with(extension); });
}

auto is_direct_media_embed(std::string_view type, bool has_provider) -> bool {
    return (type == "image" || type == "video") && !has_provider;
}

auto has_media(const dpp::message& message) -> bool {
    const bool attached = std::ranges::any_of(
        message.attachments, [](const dpp::attachment& file) { return is_media_attachment(file.content_type, file.filename); });
    const bool linked = std::ranges::any_of(
        message.embeds, [](const dpp::embed& embed) { return is_direct_media_embed(embed.type, embed.provider.has_value()); });
    return attached || linked;
}

// --------------------------------------------------------------------------

media_tracker::media_tracker(replacement_store& posts, config::guild_settings& settings, ports::clock& clock)
    : posts_(&posts), settings_(&settings), clock_(&clock) {}

auto media_tracker::on_message(const posted& message) -> bool {
    if (!message.from_person || message.guild_id.empty() || (!message.has_media && !message.has_links)) return false;
    if (!images_enabled(*settings_, message.guild_id)) return false;

    if (message.has_media) {
        const bool recorded = posts_->record_image_post(message.message_id, message.guild_id, message.channel_id, message.author_id,
                                                        created_at(message.message_id));
        if (recorded) {
            util::log().debug("image post {} by {} in guild {} is counted", message.message_id, message.author_id, message.guild_id);
        }
        return recorded;
    }

    // Links only: whether one is an image is up to the preview Discord adds.
    const std::scoped_lock guard(mutex_);
    const auto now = clock_->steady_now();
    expire(now);
    waiting_.insert_or_assign(message.message_id, waiting_post{.guild_id = message.guild_id,
                                                               .channel_id = message.channel_id,
                                                               .author_id = message.author_id,
                                                               .until = now + media_preview_wait});
    return false;
}

auto media_tracker::on_update(dpp::snowflake message_id, bool has_media) -> bool {
    waiting_post found;
    {
        const std::scoped_lock guard(mutex_);
        expire(clock_->steady_now());
        const auto waiting = waiting_.find(message_id);
        if (waiting == waiting_.end()) return false;
        // A preview that is not an image leaves it waiting: a message can be
        // updated more than once, and the image may be in a later one.
        if (!has_media) return false;
        found = waiting->second;
        waiting_.erase(waiting);
    }

    const bool recorded = posts_->record_image_post(message_id, found.guild_id, found.channel_id, found.author_id, created_at(message_id));
    if (recorded) util::log().debug("linked image {} by {} in guild {} is counted", message_id, found.author_id, found.guild_id);
    return recorded;
}

auto media_tracker::waiting() const -> std::size_t {
    const std::scoped_lock guard(mutex_);
    return waiting_.size();
}

auto media_tracker::expire(std::chrono::steady_clock::time_point now) -> void {
    std::erase_if(waiting_, [&](const auto& entry) { return entry.second.until <= now; });
}

} // namespace latibot::events
