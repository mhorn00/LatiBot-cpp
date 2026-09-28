#pragma once

#include "core/ports/clock.hpp"
#include "core/ports/discord_gateway.hpp"
#include "core/util/log.hpp"

#include <dpp/coro/task.h>
#include <dpp/snowflake.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::config {
class guild_settings;
}

namespace latibot::events {

// --------------------------------------------------------------------------
// Where the log goes
// --------------------------------------------------------------------------

/// The one channel the bot's own log is posted to, bot-wide, and the lowest
/// level posted there. Independent of the console's level: either can be
/// lower than the other.
struct log_destination {
    dpp::snowflake guild_id;
    dpp::snowflake channel_id;
    util::log_level level = util::log_level::info;

    auto operator==(const log_destination&) const -> bool = default;
};

/// Keeps the destination under `config::bot_wide`, so it survives a restart
/// and there is only ever one.
class log_destination_store {
public:
    explicit log_destination_store(config::guild_settings& settings) : settings_(&settings) {}

    /// Nothing when no channel is set, or what is stored cannot be read.
    [[nodiscard]] auto find() const -> std::optional<log_destination>;

    auto save(const log_destination& destination) -> void;

    /// True when a channel was set.
    auto clear() -> bool;

private:
    config::guild_settings* settings_;
};

// --------------------------------------------------------------------------
// Lines waiting to be posted
// --------------------------------------------------------------------------

/// How often what has been logged is posted.
inline constexpr std::chrono::seconds log_channel_tick{2};

/// At most this many messages each tick. Discord allows five messages in five
/// seconds in one channel, so this leaves room for anyone else posting there.
inline constexpr std::size_t log_messages_per_tick = 2;

/// Lines logged, waiting to be posted as messages.
///
/// Filled from whichever thread logged, under the logger's own lock, so
/// `push` does nothing but copy the line in. When lines arrive faster than
/// they can be posted, the oldest are kept and the rest counted, and the next
/// message says how many were dropped: the start of a flood is usually the
/// part worth reading.
class log_buffer {
public:
    /// How many lines wait at most.
    static constexpr std::size_t max_lines = 1000;

    /// Longest a single line may be, in characters, so that one always fits
    /// a message with room to spare.
    static constexpr std::size_t max_line = 1900;

    /// `secrets` are replaced by asterisks wherever they appear. The bot
    /// passes its token and API keys, which nothing should log, and which
    /// would be public in a channel if something ever did.
    explicit log_buffer(std::vector<std::string> secrets = {});

    auto push(std::chrono::sys_seconds stamp, util::log_level level, std::string_view message) -> void;

    /// Up to `max_messages` messages of waiting lines, oldest first, each a
    /// code block within Discord's 2000 characters. What they hold is no
    /// longer waiting.
    [[nodiscard]] auto take(std::size_t max_messages) -> std::vector<std::string>;

    auto clear() -> void;

    [[nodiscard]] auto waiting() const -> std::size_t;

private:
    mutable std::mutex mutex_;
    std::deque<std::string> lines_;
    std::size_t dropped_ = 0;
    std::vector<std::string> secrets_;
};

// --------------------------------------------------------------------------
// Posting
// --------------------------------------------------------------------------

/// How the channel is doing, for `/logs show`.
struct log_channel_status {
    std::optional<log_destination> destination;
    std::size_t waiting = 0;

    /// Why the last post failed, while posting is failing.
    std::optional<std::string> failure;

    /// How long until the next try, while posting is failing.
    std::chrono::seconds retry_in{0};
};

/// Posts the bot's log to its channel.
///
/// `start` hooks it into the logger; from then on every line at the
/// destination's level waits in a buffer until `flush`, which the bot calls
/// every `log_channel_tick`. A post that fails drops what it was posting and
/// waits before trying again, longer each time, so a channel that was deleted
/// or closed to the bot costs an occasional request rather than one every
/// tick. Lines keep waiting meanwhile, as many as the buffer holds.
class log_channel {
public:
    /// First wait after a failure, and the longest.
    static constexpr std::chrono::seconds first_backoff{30};
    static constexpr std::chrono::seconds longest_backoff{std::chrono::minutes{15}};

    log_channel(ports::discord_gateway& discord, ports::clock& clock, std::vector<std::string> secrets = {});

    /// Unhooks it from the logger, which outlives it.
    ~log_channel();

    log_channel(const log_channel&) = delete;
    auto operator=(const log_channel&) -> log_channel& = delete;

    /// Starts posting to `destination`, or moves there, or changes only the
    /// level. Lines already waiting go wherever it now points. Moving to
    /// another channel forgets a failure being waited out, since it was
    /// likely the old channel's.
    auto start(const log_destination& destination) -> void;

    /// Stops posting, and throws away what was waiting.
    auto stop() -> void;

    [[nodiscard]] auto status() const -> log_channel_status;

    /// Posts what is waiting, unless a failure is being waited out or the
    /// last flush is still going.
    auto flush() -> dpp::task<void>;

private:
    auto failed(const ports::api_error& error, std::size_t lost) -> void;
    auto succeeded() -> void;

    ports::discord_gateway* discord_;
    ports::clock* clock_;
    log_buffer buffer_;

    /// Never held while logging: the logger calls into the buffer with its
    /// own lock held, and logging with this one held would be the other
    /// order.
    mutable std::mutex mutex_;
    std::optional<log_destination> destination_;
    std::optional<std::string> failure_;
    std::chrono::seconds backoff_{0};
    std::chrono::steady_clock::time_point next_attempt_;

    std::atomic<bool> flushing_{false};
};

/// The message `/logs set` posts first, which is also how it finds out the
/// bot can post there.
[[nodiscard]] auto log_channel_greeting(util::log_level level) -> std::string;

} // namespace latibot::events
