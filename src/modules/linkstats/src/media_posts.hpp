#pragma once

#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <chrono>
#include <map>
#include <mutex>
#include <string_view>

namespace latibot::config {
class guild_settings;
}

namespace latibot::ports {
class clock;
}

namespace latibot::events {

class replacement_store;

// Reactions on the images and videos people post, counted alongside the
// bot's link replacements once a server turns it on
// (src/modules/linkstats/docs/Link_Stats.md §9).

/// The `guild_settings` key that turns it on. Off until somebody with Manage
/// Server runs `/linkstats images on`: it counts every image posted in every
/// channel, which is a bigger step than counting the bot's own messages.
inline constexpr std::string_view images_enabled_key = "linkstats_images";

[[nodiscard]] auto images_enabled(const config::guild_settings& settings, dpp::snowflake guild_id) -> bool;
auto set_images_enabled(config::guild_settings& settings, dpp::snowflake guild_id, bool enabled) -> void;

/// Whether an attachment is an image or a video, by its type, or by its
/// file's extension when Discord gave no type.
[[nodiscard]] auto is_media_attachment(std::string_view content_type, std::string_view filename) -> bool;

/// Whether an embed is a link's own picture or video, which Discord shows in
/// place of the link, rather than a site's preview of a page.
///
/// Discord makes an `image` or `video` embed with no provider for a direct
/// link to a file. A site's preview names its provider (YouTube's `video`
/// embeds do), and `gifv`, Tenor and Giphy's looping GIFs, is left out.
[[nodiscard]] auto is_direct_media_embed(std::string_view type, bool has_provider) -> bool;

/// Whether a message carries an image or a video, attached or linked.
[[nodiscard]] auto has_media(const dpp::message& message) -> bool;

/// How long a message with links waits for Discord to add its previews.
inline constexpr std::chrono::seconds media_preview_wait{60};

/// Records image and video posts as they happen, in a guild that counts them.
///
/// An upload is known when the message arrives. A link to an image is only
/// known once Discord adds its preview, a moment later, in an update that
/// often carries no author. So a message with links is kept here, author and
/// all, until an update shows it was an image, or `media_preview_wait`
/// passes.
class media_tracker {
public:
    media_tracker(replacement_store& posts, config::guild_settings& settings, ports::clock& clock);

    /// A message just posted, reduced to what this needs.
    struct posted {
        dpp::snowflake message_id;
        dpp::snowflake guild_id;
        dpp::snowflake channel_id;
        dpp::snowflake author_id;

        /// Somebody, not a bot or a webhook.
        bool from_person = false;
        bool has_media = false;
        bool has_links = false;
    };

    /// True when it was recorded as an image post now.
    auto on_message(const posted& message) -> bool;

    /// Discord updated a message, usually to add its previews. True when that
    /// showed a waiting message to be an image post, and it was recorded.
    auto on_update(dpp::snowflake message_id, bool has_media) -> bool;

    /// How many messages are waiting for their previews, for tests.
    [[nodiscard]] auto waiting() const -> std::size_t;

private:
    struct waiting_post {
        dpp::snowflake guild_id;
        dpp::snowflake channel_id;
        dpp::snowflake author_id;
        std::chrono::steady_clock::time_point until;
    };

    /// Forgets messages whose previews never came. Called with the lock held.
    auto expire(std::chrono::steady_clock::time_point now) -> void;

    replacement_store* posts_;
    config::guild_settings* settings_;
    ports::clock* clock_;

    mutable std::mutex mutex_;
    std::map<dpp::snowflake, waiting_post> waiting_;
};

} // namespace latibot::events
