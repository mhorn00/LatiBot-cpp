#include "events/log_channel.hpp"

#include "core/config/guild_settings.hpp"
#include "core/discord/message_flags.hpp"
#include "core/util/text.hpp"

#include <dpp/message.h>

#include <algorithm>
#include <format>
#include <utility>

namespace latibot::events {
namespace {

constexpr std::string_view guild_key = "log_channel_guild";
constexpr std::string_view channel_key = "log_channel";
constexpr std::string_view level_key = "log_channel_level";

/// Room for a message's lines once the code block's fences are counted.
constexpr std::size_t message_budget = 2000 - std::string_view("```\n```").size();

/// Secrets shorter than this are left alone: masking every "a" in the log
/// would hide nothing and ruin the rest.
constexpr std::size_t shortest_secret = 8;

auto replace_all(std::string& text, std::string_view from, std::string_view to) -> void {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
}

/// A zero-width space after every second backtick in a row, so nothing a line
/// holds can close the code block it is posted in. Replacing each "```"
/// would not do: two replacements side by side leave three backticks where
/// they meet.
auto defuse_fences(std::string_view text) -> std::string {
    std::string defused;
    defused.reserve(text.size());

    std::size_t run = 0;
    for (const char letter : text) {
        if (letter != '`') {
            run = 0;
        } else if (++run > 2) {
            defused += "​";
            run = 1;
        }
        defused += letter;
    }
    return defused;
}

/// Lowers a flag however the scope ends, a coroutine's included.
class lowered_on_exit {
public:
    explicit lowered_on_exit(std::atomic<bool>& flag) noexcept : flag_(&flag) {}
    ~lowered_on_exit() { flag_->store(false); }

    lowered_on_exit(const lowered_on_exit&) = delete;
    auto operator=(const lowered_on_exit&) -> lowered_on_exit& = delete;

private:
    std::atomic<bool>* flag_;
};

} // namespace

// --------------------------------------------------------------------------
// log_destination_store
// --------------------------------------------------------------------------

auto log_destination_store::find() const -> std::optional<log_destination> {
    const auto guild = util::parse_snowflake(settings_->get(config::bot_wide, guild_key, ""));
    const auto channel = util::parse_snowflake(settings_->get(config::bot_wide, channel_key, ""));
    if (!guild || !channel) return std::nullopt;

    // A level that cannot be read is the default rather than no channel at
    // all: the channel is the part somebody chose on purpose.
    const auto level = util::log_level_from_string(settings_->get(config::bot_wide, level_key, "info"));
    return log_destination{.guild_id = *guild, .channel_id = *channel, .level = level.value_or(util::log_level::info)};
}

auto log_destination_store::save(const log_destination& destination) -> void {
    settings_->set(config::bot_wide, guild_key, destination.guild_id.str());
    settings_->set(config::bot_wide, channel_key, destination.channel_id.str());
    settings_->set(config::bot_wide, level_key, util::to_string(destination.level));
}

auto log_destination_store::clear() -> bool {
    const bool had = settings_->erase(config::bot_wide, channel_key);
    settings_->erase(config::bot_wide, guild_key);
    settings_->erase(config::bot_wide, level_key);
    return had;
}

// --------------------------------------------------------------------------
// log_buffer
// --------------------------------------------------------------------------

log_buffer::log_buffer(std::vector<std::string> secrets) : secrets_(std::move(secrets)) {
    std::erase_if(secrets_, [](const std::string& secret) { return secret.size() < shortest_secret; });
}

auto log_buffer::add_secret(std::string secret) -> void {
    if (secret.size() < shortest_secret) return;
    const std::unique_lock guard(secrets_mutex_);
    secrets_.push_back(std::move(secret));
}

auto log_buffer::push(std::chrono::sys_seconds stamp, util::log_level level, std::string_view message) -> void {
    // Everything that costs anything happens before the lock, which every
    // thread that logs is waiting on.
    std::string line = std::format("{:%H:%M:%S} [{}] {}", stamp, util::to_string(level), message);
    {
        const std::shared_lock reading(secrets_mutex_);
        for (const std::string& secret : secrets_) {
            replace_all(line, secret, "*****");
        }
    }
    line = defuse_fences(line);
    if (util::character_count(line) > max_line) line = util::truncate(line, max_line);

    const std::scoped_lock guard(mutex_);
    if (lines_.size() >= max_lines) {
        ++dropped_;
        return;
    }
    lines_.push_back(std::move(line));
}

auto log_buffer::take(std::size_t max_messages) -> std::vector<std::string> {
    std::vector<std::string> messages;
    const std::scoped_lock guard(mutex_);

    std::string notice;
    if (dropped_ > 0 && max_messages > 0) {
        notice = std::format("… {} line(s) dropped: they came faster than they could be posted\n", dropped_);
        dropped_ = 0;
    }

    while (messages.size() < max_messages && (!lines_.empty() || !notice.empty())) {
        std::string body = std::exchange(notice, {});
        std::size_t used = util::character_count(body);

        while (!lines_.empty()) {
            const std::size_t next = util::character_count(lines_.front()) + 1;
            if (used > 0 && used + next > message_budget) break;
            body += lines_.front();
            body += '\n';
            used += next;
            lines_.pop_front();
        }
        messages.push_back(std::format("```\n{}```", body));
    }
    return messages;
}

auto log_buffer::clear() -> void {
    const std::scoped_lock guard(mutex_);
    lines_.clear();
    dropped_ = 0;
}

auto log_buffer::waiting() const -> std::size_t {
    const std::scoped_lock guard(mutex_);
    return lines_.size();
}

// --------------------------------------------------------------------------
// log_channel
// --------------------------------------------------------------------------

log_channel::log_channel(ports::discord_gateway& discord, ports::clock& clock, std::vector<std::string> secrets)
    : discord_(&discord), clock_(&clock), buffer_(std::move(secrets)) {}

log_channel::~log_channel() {
    try {
        util::log().set_tap({}, util::log_level::off);
    } catch (...) { // NOLINT(bugprone-empty-catch)
        // Only a lock failing could get here, and there is nothing a
        // destructor could do about that.
    }
}

auto log_channel::start(const log_destination& destination) -> void {
    {
        const std::scoped_lock guard(mutex_);
        if (!destination_ || destination_->channel_id != destination.channel_id) {
            failure_.reset();
            backoff_ = std::chrono::seconds::zero();
            next_attempt_ = {};
        }
        destination_ = destination;
    }
    util::log().set_tap(
        [this](util::log_level level, std::string_view message) {
            buffer_.push(std::chrono::floor<std::chrono::seconds>(clock_->now()), level, message);
        },
        destination.level);
}

auto log_channel::stop() -> void {
    util::log().set_tap({}, util::log_level::off);
    buffer_.clear();

    const std::scoped_lock guard(mutex_);
    destination_.reset();
    failure_.reset();
}

auto log_channel::status() const -> log_channel_status {
    log_channel_status status{.destination = std::nullopt, .waiting = buffer_.waiting(), .failure = std::nullopt, .retry_in = {}};

    const std::scoped_lock guard(mutex_);
    status.destination = destination_;
    status.failure = failure_;
    if (failure_) {
        const auto left = std::chrono::ceil<std::chrono::seconds>(next_attempt_ - clock_->steady_now());
        status.retry_in = std::max(left, std::chrono::seconds::zero());
    }
    return status;
}

auto log_channel::flush() -> dpp::task<void> {
    // One flush at a time: the timer does not wait for a slow post, and two
    // at once would post out of order.
    if (flushing_.exchange(true)) co_return;
    const lowered_on_exit done(flushing_);

    std::optional<log_destination> destination;
    {
        const std::scoped_lock guard(mutex_);
        if (!destination_ || clock_->steady_now() < next_attempt_) co_return;
        destination = destination_;
    }

    const std::vector<std::string> messages = buffer_.take(log_messages_per_tick);
    for (std::size_t posted = 0; posted < messages.size(); ++posted) {
        dpp::message message(destination->channel_id, messages[posted]);
        discord::apply_flags(message, discord::channel_message_flags);
        message.set_allowed_mentions();

        const auto sent = co_await discord_->send_message(std::move(message));
        if (!sent) {
            failed(sent.error(), messages.size() - posted);
            co_return;
        }
    }
    if (!messages.empty()) succeeded();
}

auto log_channel::failed(const ports::api_error& error, std::size_t lost) -> void {
    bool first = false;
    dpp::snowflake channel;
    std::chrono::seconds wait{};
    {
        const std::scoped_lock guard(mutex_);
        first = !failure_;
        backoff_ = first ? first_backoff : std::min(backoff_ * 2, longest_backoff);
        next_attempt_ = clock_->steady_now() + backoff_;
        failure_ = error.message;
        wait = backoff_;
        if (destination_) channel = destination_->channel_id;
    }

    // Logged once when posting starts failing, not on every try: this line
    // waits to be posted too, and a warning each try would only repeat it.
    if (first) {
        util::log().warn("could not post the log to channel {}: {}; {} message(s) of it lost, trying again in {}", channel, error.message,
                         lost, wait);
    } else {
        util::log().debug("still cannot post the log to channel {}: {}; trying again in {}", channel, error.message, wait);
    }
}

auto log_channel::succeeded() -> void {
    bool recovered = false;
    {
        const std::scoped_lock guard(mutex_);
        recovered = failure_.has_value();
        failure_.reset();
        backoff_ = std::chrono::seconds::zero();
    }
    if (recovered) util::log().info("posting the log to its channel again");
}

auto log_channel_greeting(util::log_level level) -> std::string {
    if (level == util::log_level::trace) return "from now on, everything i log is posted in this channel";
    return std::format("from now on, what i log at {} and above is posted in this channel", util::to_string(level));
}

} // namespace latibot::events
