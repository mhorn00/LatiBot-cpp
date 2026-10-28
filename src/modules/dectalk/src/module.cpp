#include "dectalk/module.hpp"

#include "chat_command.hpp"
#include "core/capabilities/model_tools.hpp"
#include "core/capabilities/speech.hpp"
#include "core/commands/registry.hpp"
#include "core/discord/voice_message.hpp"
#include "core/modules/capability_registry.hpp"
#include "core/modules/host.hpp"
#include "core/ui/panel_routes.hpp"
#include "core/util/log.hpp"
#include "dectalk_engine.hpp"
#include "dectalk_speech.hpp"
#include "speak_command.hpp"
#include "speech_queue.hpp"
#include "speech_tools.hpp"
#include "voice/services.hpp"
#include "voice/voice_mixer.hpp"
#include "voice/voice_state.hpp"
#include "voice_lab.hpp"
#include "voice_store.hpp"

#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace latibot::audio {
namespace {

// Append only: once a version has shipped, its SQL is never edited, and a
// change becomes the next version.
constexpr std::array<db::migration, 1> dectalk_steps{{
    {.version = 1, .name = "tts_voices", .sql = R"sql(
        -- Custom voices, per guild: a built-in voice and the [:dv] edits made
        -- to it, as "ap 200 pr 150". Names are stored in lowercase and never
        -- match a built-in voice's.
        CREATE TABLE tts_voices (
            guild_id   INTEGER NOT NULL,
            name       TEXT    NOT NULL,
            base_voice TEXT    NOT NULL,
            params     TEXT    NOT NULL,
            created_by INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            PRIMARY KEY (guild_id, name)
        ) WITHOUT ROWID;
     )sql"},
}};

constexpr std::string_view module_name = "dectalk";

class dectalk_module final : public modules::module {
public:
    explicit dectalk_module(modules::host& bot) : bot_(&bot), voices_(bot.database()), drafts_(bot.clock()) {}

    [[nodiscard]] auto name() const -> std::string_view override { return module_name; }
    [[nodiscard]] auto schema() const -> std::span<const db::migration> override { return dectalk_steps; }

    /// The speech queue and the speech capability need voice's mixer and
    /// sessions, which voice, built first, has offered by now.
    auto offer(modules::capability_registry& offered) -> void override {
        voice::services& voice = voice::required(offered);
        queue_ = std::make_unique<speech_queue>(voice.mixer());
        speech_ = std::make_unique<dectalk_speech>(engine_, *queue_, voice.sessions(), bot_->settings(), &voices_);
        lab_ = std::make_unique<commands::voice_lab>(drafts_, voices_, bot_->clock(), services());

        // Speech waits for the connection to be ready, is told when each
        // utterance finishes playing, and is forgotten when the bot leaves
        // (src/modules/voice/docs/Voice_Channels.md §3).
        voice.on_ready([this](dpp::snowflake guild) { queue_->on_ready(guild); });
        voice.on_marker([this](dpp::snowflake guild, const std::string& marker) { queue_->on_marker(guild, marker); });
        voice.on_left([this](dpp::snowflake guild) { queue_->forget(guild); });

        // What the language model speaks through, when it is built
        // (docs/modules/Module_Plan_Final.md §5.3).
        offered.offer<capabilities::speech>(*speech_, module_name);
    }

    auto start(modules::host& bot) -> void override {
        bot.slash_commands().add(std::make_unique<commands::speak_command>(services()));
        bot.slash_commands().add(std::make_unique<commands::tts_command>(services(), *lab_));
        bot.slash_commands().add(std::make_unique<commands::chat_command>(services(), bot.http()));
        bot.panels().add(*lab_,
                         {commands::lab_edit_view, commands::lab_base_view, commands::lab_test_view, commands::lab_save_view,
                          commands::lab_reset_view, commands::lab_form_view, commands::lab_name_view, commands::lab_open_view},
                         module_name);

        // The language model's own /speak, /chat and /tts stop, when it is
        // built in.
        if (auto* tools = bot.capabilities().find<capabilities::model_tools>()) {
            commands::add_speech_tools(
                *tools, {.speech = services(),
                         .channel_of = [](dpp::snowflake guild, dpp::snowflake user) { return discord::voice_channel_of(guild, user); },
                         .bot_channel = [this](dpp::snowflake guild) { return discord::bot_voice_channel(bot_->cluster(), guild); },
                         .connect =
                             [this](dpp::snowflake guild, dpp::snowflake channel) {
                                 dpp::discord_client* shard = discord::shard_for(bot_->cluster(), guild);
                                 if (shard == nullptr) return false;
                                 shard->connect_voice(guild, channel);
                                 util::log().info("joined voice channel {} in guild {} to speak for the language model", channel, guild);
                                 return true;
                             },
                         .post_voice =
                             [this](dpp::snowflake channel, discord::voice_message message) {
                                 return discord::post_voice_message(bot_->http(), bot_->cluster().token, channel,
                                                                    dpp::m_suppress_notifications, std::move(message));
                             }});
        }
    }

private:
    [[nodiscard]] auto services() -> commands::speech_services {
        return {
            .engine = &engine_, .queue = queue_.get(), .settings = &bot_->settings(), .bootstrap = &bot_->bootstrap(), .voices = &voices_};
    }

    modules::host* bot_;
    // The engine starts its worker thread here, and is gone before the
    // cluster is.
    dectalk_engine engine_;
    voice_store voices_;
    commands::voice_drafts drafts_;
    // Made in offer, once voice's mixer can be found.
    std::unique_ptr<speech_queue> queue_;
    std::unique_ptr<dectalk_speech> speech_;
    std::unique_ptr<commands::voice_lab> lab_;
};

} // namespace

auto dectalk_schema() noexcept -> db::module_schema {
    return {.module = module_name, .steps = dectalk_steps};
}

} // namespace latibot::audio

namespace latibot::dectalk {

auto make_module(modules::host& bot) -> std::unique_ptr<modules::module> {
    return std::make_unique<audio::dectalk_module>(bot);
}

} // namespace latibot::dectalk
