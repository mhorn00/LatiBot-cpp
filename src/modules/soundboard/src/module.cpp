#include "soundboard/module.hpp"

#include "clips.hpp"
#include "core/commands/registry.hpp"
#include "core/modules/host.hpp"
#include "core/ports/clock.hpp"
#include "core/util/log.hpp"
#include "core/util/process.hpp"
#include "soundboard_command.hpp"
#include "soundboard_config.hpp"
#include "sounds.hpp"

#include <dpp/dpp.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace latibot::soundboard {
namespace {

constexpr std::string_view module_name = "soundboard";

/// How often every server's sounds are read again. Discord tells bots of
/// sounds added and removed, but DPP does not pass those events on
/// (src/modules/soundboard/docs/Soundboard.md §2).
constexpr std::chrono::seconds refresh_interval = std::chrono::minutes{30};

/// How old a server's sounds may be when someone looks through them, before
/// they are read again for the next keystroke.
constexpr std::chrono::seconds stale_after = std::chrono::minutes{1};

class soundboard_module final : public modules::module {
public:
    explicit soundboard_module(modules::host& bot)
        : bot_(&bot),
          section_(soundboard_section().read(bot.section(module_name))),
          ffmpeg_(util::locate_program("ffmpeg", section_.ffmpeg_path)),
          source_(bot.raw()),
          clips_(bot.http(), ffmpeg_) {}

    [[nodiscard]] auto name() const -> std::string_view override { return module_name; }

    auto start(modules::host& bot) -> void override {
        if (ffmpeg_) {
            util::log().info("the soundboard uses ffmpeg at {}", ffmpeg_->string());
        } else {
            util::log().warn(
                "the soundboard found no ffmpeg beside the bot or on PATH, nor at soundboard.ffmpeg_path in config.json; "
                "/soundboard will say so");
        }

        bot.slash_commands().add(std::make_unique<commands::soundboard_command>(commands::soundboard_services{
            .sounds = &catalog_, .clips = &clips_, .http = &bot.http(), .looked_in = [this](dpp::snowflake guild) {
                read_if_stale(guild);
            }}));

        bot.listen(bot.cluster().on_ready, "soundboard: Discord's own sounds",
                   [this](const dpp::ready_t& /*event*/) { bot_->detach(read_defaults(), "reading Discord's soundboard sounds"); });
        bot.listen(bot.cluster().on_guild_create, "soundboard: a server's sounds", [this](const dpp::guild_create_t& event) {
            const dpp::guild& guild = event.created;
            if (guild.is_unavailable()) return;
            {
                const std::scoped_lock lock(mutex_);
                known_[guild.id].name = guild.name;
            }
            read_guild(guild.id);
        });
        bot.listen(bot.cluster().on_guild_delete, "soundboard: servers left", [this](const dpp::guild_delete_t& event) {
            // An outage makes a server unavailable for a while; its sounds
            // are still there when it comes back.
            if (event.deleted.is_unavailable()) return;
            {
                const std::scoped_lock lock(mutex_);
                known_.erase(event.guild_id);
            }
            catalog_.forget_guild(event.guild_id);
        });

        bot.every(refresh_interval, "the soundboard refresh", [this] {
            bot_->detach(read_defaults(), "reading Discord's soundboard sounds");
            for (const dpp::snowflake guild : known_guilds()) {
                read_guild(guild);
            }
        });
    }

private:
    /// A server the bot is in, and when its sounds were last asked for.
    struct known_guild {
        std::string name;
        std::optional<std::chrono::steady_clock::time_point> asked_at;
        bool reading = false;
    };

    auto read_defaults() -> dpp::task<void> {
        auto sounds = co_await source_.default_sounds();
        if (!sounds.has_value()) {
            util::log().warn("could not read Discord's own soundboard sounds: {}", sounds.error().message);
            co_return;
        }
        util::log().debug("Discord has {} soundboard sounds of its own", sounds->size());
        catalog_.set_defaults(std::move(sounds.value()));
    }

    /// Reads a server's sounds again, unless that is already under way.
    auto read_guild(dpp::snowflake guild) -> void {
        std::string name;
        {
            const std::scoped_lock lock(mutex_);
            const auto found = known_.find(guild);
            if (found == known_.end() || found->second.reading) return;
            found->second.reading = true;
            found->second.asked_at = bot_->clock().steady_now();
            name = found->second.name;
        }
        bot_->detach(read_guild_sounds(guild, std::move(name)), "reading a server's soundboard sounds");
    }

    /// Reads a server's sounds again when they were last asked for long
    /// enough ago, failures included, so a server Discord will not list is
    /// not asked on every keystroke.
    auto read_if_stale(dpp::snowflake guild) -> void {
        {
            const std::scoped_lock lock(mutex_);
            const auto found = known_.find(guild);
            if (found == known_.end()) return;
            const auto asked_at = found->second.asked_at;
            if (asked_at && bot_->clock().steady_now() - *asked_at < stale_after) return;
        }
        read_guild(guild);
    }

    auto read_guild_sounds(dpp::snowflake guild, std::string name) -> dpp::task<void> {
        auto sounds = co_await source_.guild_sounds(guild);
        {
            const std::scoped_lock lock(mutex_);
            if (const auto found = known_.find(guild); found != known_.end()) found->second.reading = false;
        }
        if (!sounds.has_value()) {
            util::log().warn("could not read the soundboard sounds of guild {}: {}", guild, sounds.error().message);
            co_return;
        }
        util::log().debug("guild {} has {} soundboard sounds", guild, sounds->size());
        catalog_.set_guild(guild, std::move(name), std::move(sounds.value()));
    }

    [[nodiscard]] auto known_guilds() -> std::vector<dpp::snowflake> {
        const std::scoped_lock lock(mutex_);
        std::vector<dpp::snowflake> guilds;
        guilds.reserve(known_.size());
        for (const auto& [id, known] : known_) {
            guilds.push_back(id);
        }
        return guilds;
    }

    modules::host* bot_;
    soundboard_config section_;
    /// Looked for once, at startup; without it /soundboard says so.
    std::optional<std::filesystem::path> ffmpeg_;
    sound_catalog catalog_;
    discord_sounds source_;

    std::mutex mutex_;
    std::map<dpp::snowflake, known_guild> known_;

    /// Last, so its worker stops before anything else goes.
    ffmpeg_clips clips_;
};

} // namespace

auto make_module(modules::host& bot) -> std::unique_ptr<modules::module> {
    return std::make_unique<soundboard_module>(bot);
}

auto config_defaults() -> nlohmann::ordered_json {
    return soundboard_section().defaults();
}

} // namespace latibot::soundboard
