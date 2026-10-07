#pragma once

#include <dpp/coro/task.h>
#include <dpp/snowflake.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::ports {
class clock;
class discord_gateway;
class http_client;
} // namespace latibot::ports

namespace latibot::events {

// The bot's own copies of the custom emojis it has seen, as application
// emojis, so a statistic can still show an emote after its server deletes it
// (src/modules/linkstats/docs/Link_Stats.md §10).

/// Discord's cap on an emoji's image.
inline constexpr std::size_t emoji_image_limit = std::size_t{256} * 1024;

/// Discord's cap on how many emojis one application may own.
inline constexpr std::size_t application_emoji_limit = 2000;

/// What became of the image of one custom emoji.
enum class image_state : std::uint8_t {
    /// Downloaded, and its hash known.
    fetched,
    /// Discord's CDN has no such image any more.
    lost,
    /// Too big to upload, even at a smaller size.
    too_big,
    /// Something went wrong that may not next time.
    failed,
};

[[nodiscard]] auto to_string(image_state state) noexcept -> std::string_view;

/// How long before something that could not be had is tried again. The lost
/// ones are tried again too: the CDN has been known to bring images back.
[[nodiscard]] auto retry_after(image_state state) noexcept -> std::chrono::hours;

/// Whether a GIF has more than one frame, by counting its graphic control
/// blocks: a still one has one at most.
[[nodiscard]] auto is_animated_gif(std::string_view image) noexcept -> bool;

/// SHA-256 of `bytes`, as 64 lowercase hex digits.
[[nodiscard]] auto sha256_hex(std::string_view bytes) -> std::string;

/// A name Discord accepts for an application emoji, from the original's:
/// letters, digits and underscores, 2 to 32 of them.
[[nodiscard]] auto copy_name(std::string_view original, dpp::snowflake id) -> std::string;

/// One application emoji, standing in for every emoji with its image.
struct stored_copy {
    std::string image_sha256;
    dpp::snowflake copy_id;
    std::string name;
};

/// What one round of copying should do.
struct copy_plan {
    /// Custom emojis to download, and to upload when their image is not
    /// copied yet, most used first.
    std::vector<std::string> fetch;

    /// Copies no emoji wants any more. Only once nothing is left to fetch,
    /// since an image not yet downloaded may turn out to be one of these.
    std::vector<stored_copy> prune;
};

/// `emoji_images` and `emoji_copies`.
class emoji_copy_store {
public:
    explicit emoji_copy_store(db::database& db) : db_(&db) {}

    /// Works out what to copy and what to delete, for copies of every emote
    /// used at least `min_uses` times.
    ///
    /// An emote is an emoji and every emoji merged into it by an alias; its
    /// uses are all of theirs together, and it gets one copy between them:
    /// the image of the one kept, or when that is lost, the next that can be
    /// had. Emojis with the same image share a copy anyway.
    [[nodiscard]] auto plan(std::int64_t min_uses, std::chrono::sys_seconds now) const -> copy_plan;

    /// The original's name and whether it is known to be animated.
    struct emoji_facts {
        std::string name;
        bool animated = false;
    };
    [[nodiscard]] auto facts(std::string_view emoji_key) const -> emoji_facts;

    auto record_image(std::string_view emoji_key, image_state state, const std::optional<std::string>& image_sha256, bool animated,
                      std::chrono::sys_seconds now) -> void;

    [[nodiscard]] auto copy_for(std::string_view image_sha256) const -> std::optional<stored_copy>;
    [[nodiscard]] auto name_taken(std::string_view name) const -> bool;
    [[nodiscard]] auto copies() const -> std::size_t;

    auto add_copy(const stored_copy& copy, bool animated, std::chrono::sys_seconds now) -> void;
    auto remove_copy(std::string_view image_sha256) -> void;

private:
    db::database* db_;
};

/// How many emojis one round downloads, uploads or deletes. Discord limits
/// how fast emojis are made, and DPP waits out those limits, so a small
/// round every minute keeps well inside them.
inline constexpr std::size_t copies_per_round = 10;

/// How often a round runs.
inline constexpr std::chrono::seconds copy_round_interval{60};

/// Keeps the bot's copies in step with the emojis it has seen.
///
/// Each round asks `emoji_copy_store::plan` what to do, then does some of
/// it: downloads an emoji's image from Discord's CDN, uploads it as an
/// application emoji unless one with the same image exists, and deletes
/// copies nothing wants since the threshold went up or aliases merged them.
class emoji_copier {
public:
    /// `min_uses` of 0 or less turns copying off, and leaves any copies be.
    emoji_copier(emoji_copy_store& store, ports::http_client& http, ports::discord_gateway& discord, ports::clock& clock,
                 std::int64_t min_uses);

    /// What one round did.
    struct round_report {
        int copied = 0;
        int shared = 0;
        int lost = 0;
        int failed = 0;
        int pruned = 0;
    };

    /// One round, of at most `limit` emojis. A round started while another
    /// runs does nothing.
    auto run_round(std::size_t limit = copies_per_round) -> dpp::task<round_report>;

    [[nodiscard]] auto enabled() const noexcept -> bool { return min_uses_ > 0; }

private:
    /// A downloaded image, or why there is none.
    struct download {
        image_state state = image_state::failed;
        std::string image;
        bool animated = false;
    };

    /// What one host gave in one format.
    struct attempt {
        std::optional<download> image;
        bool transient = false;
        bool too_big = false;
    };

    /// The image from Discord's CDN, trying both of its hosts: a GIF when
    /// it moves, a PNG when it does not.
    auto fetch(dpp::snowflake id) -> dpp::task<download>;

    /// One host, one format, at full size and then smaller.
    auto fetch_format(std::string_view host, dpp::snowflake id, std::string_view format) -> dpp::task<attempt>;
    auto copy_one(const std::string& emoji_key, round_report& report) -> dpp::task<void>;
    auto prune_one(const stored_copy& copy, round_report& report) -> dpp::task<void>;

    [[nodiscard]] auto now() const -> std::chrono::sys_seconds;

    emoji_copy_store* store_;
    ports::http_client* http_;
    ports::discord_gateway* discord_;
    ports::clock* clock_;
    std::int64_t min_uses_;

    std::atomic<bool> running_{false};
};

} // namespace latibot::events
