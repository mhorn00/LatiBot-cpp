#include "llm/module.hpp"

#include "advanced_triggers.hpp"
#include "aliases.hpp"
#include "anthropic.hpp"
#include "ask.hpp"
#include "config_check.hpp"
#include "conversation.hpp"
#include "core/capabilities/link_replacements.hpp"
#include "core/capabilities/speech.hpp"
#include "core/commands/registry.hpp"
#include "core/events/stage_order.hpp"
#include "core/modules/capability_registry.hpp"
#include "core/modules/host.hpp"
#include "core/ui/panel_routes.hpp"
#include "core/util/env.hpp"
#include "core/util/log.hpp"
#include "documents.hpp"
#include "guards.hpp"
#include "llm_command.hpp"
#include "llm_config.hpp"
#include "llm_module.hpp"
#include "memory.hpp"
#include "memory_tools.hpp"
#include "openai.hpp"
#include "provider.hpp"
#include "responder.hpp"
#include "settings.hpp"
#include "spend.hpp"
#include "stage.hpp"
#include "tools.hpp"

#include <dpp/dpp.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace latibot::llm {

auto keys_from_environment() -> provider_keys {
    provider_keys keys;
    if (const auto key = util::env_var("ANTHROPIC_API_KEY"); key && !key->empty()) keys.anthropic = *key;
    if (const auto key = util::env_var("OPENAI_API_KEY"); key && !key->empty()) keys.openai = *key;
    return keys;
}

namespace {

// Append only: once a version has shipped, its SQL is never edited, and a
// change becomes the next version.
constexpr std::array<db::migration, 1> llm_steps{{
    {.version = 1, .name = "the language model", .sql = R"sql(
        -- Every call to a model and what it cost. The spend caps are sums
        -- over this, so a restart does not reset them. The price is stored
        -- with each row rather than worked out when read, so a price change
        -- applies from then on.
        CREATE TABLE llm_usage (
            id                 INTEGER PRIMARY KEY,
            guild_id           INTEGER NOT NULL,
            model              TEXT    NOT NULL,
            input_tokens       INTEGER NOT NULL,
            output_tokens      INTEGER NOT NULL,
            cache_write_tokens INTEGER NOT NULL,
            cache_read_tokens  INTEGER NOT NULL,
            cost_usd           REAL    NOT NULL,
            at                 INTEGER NOT NULL      -- Unix seconds
        );

        CREATE INDEX llm_usage_by_time ON llm_usage (at);

        -- personality | system | trigger_style, one row per version. Nothing
        -- is ever overwritten: a revert is a new version with the old text,
        -- so it can itself be reverted.
        CREATE TABLE llm_documents (
            guild_id  INTEGER NOT NULL,
            kind      TEXT    NOT NULL,
            version   INTEGER NOT NULL,
            content   TEXT    NOT NULL,
            edited_by INTEGER NOT NULL,
            edited_at INTEGER NOT NULL,
            note      TEXT,
            PRIMARY KEY (guild_id, kind, version)
        ) WITHOUT ROWID;

        -- What the model chose to remember. subject_user_id is who it is
        -- about, NULL for the server in general; created_by is whom the model
        -- was answering when it wrote it.
        CREATE TABLE llm_memory (
            id              INTEGER PRIMARY KEY,
            guild_id        INTEGER NOT NULL,
            subject_user_id INTEGER,
            content         TEXT    NOT NULL,
            created_by      INTEGER,
            created_at      INTEGER NOT NULL
        );

        CREATE INDEX llm_memory_by_subject ON llm_memory (guild_id, subject_user_id);

        -- Full-text search over it, kept in step by the triggers below.
        CREATE VIRTUAL TABLE llm_memory_search USING fts5 (content, content = 'llm_memory', content_rowid = 'id');

        CREATE TRIGGER llm_memory_added AFTER INSERT ON llm_memory BEGIN
            INSERT INTO llm_memory_search (rowid, content) VALUES (new.id, new.content);
        END;
        CREATE TRIGGER llm_memory_removed AFTER DELETE ON llm_memory BEGIN
            INSERT INTO llm_memory_search (llm_memory_search, rowid, content) VALUES ('delete', old.id, old.content);
        END;
        CREATE TRIGGER llm_memory_changed AFTER UPDATE ON llm_memory BEGIN
            INSERT INTO llm_memory_search (llm_memory_search, rowid, content) VALUES ('delete', old.id, old.content);
            INSERT INTO llm_memory_search (rowid, content) VALUES (new.id, new.content);
        END;

        -- Who the model does not answer here: kind is user | role.
        CREATE TABLE llm_blacklist (
            guild_id  INTEGER NOT NULL,
            kind      TEXT    NOT NULL,
            target_id INTEGER NOT NULL,
            PRIMARY KEY (guild_id, kind, target_id)
        ) WITHOUT ROWID;

        -- Advanced triggers: a pattern, as the simple triggers match them,
        -- and a line telling the model what to say about it. probability is
        -- 0 to 1; the cooldown is per channel.
        CREATE TABLE llm_triggers (
            id             INTEGER PRIMARY KEY,
            guild_id       INTEGER NOT NULL,
            pattern        TEXT    NOT NULL,
            match_mode     TEXT    NOT NULL,
            context_prompt TEXT    NOT NULL,
            probability    REAL    NOT NULL,
            cooldown_s     INTEGER NOT NULL,
            enabled        INTEGER NOT NULL DEFAULT 1,
            created_by     INTEGER NOT NULL
        );

        CREATE INDEX llm_triggers_by_guild ON llm_triggers (guild_id);

        -- What the language model calls each person, in place of their
        -- Discord id and their name (src/modules/llm/docs/Language_Model.md 3.8).
        -- Random, one per person per server, and kept, so memories that name
        -- someone by alias still mean them later.
        CREATE TABLE llm_aliases (
            guild_id INTEGER NOT NULL,
            user_id  INTEGER NOT NULL,
            alias    TEXT    NOT NULL,

            -- What they were last seen called here, to put back into a reply
            -- when the bot's cache does not know them.
            name     TEXT    NOT NULL DEFAULT '',
            username TEXT    NOT NULL DEFAULT '',

            PRIMARY KEY (guild_id, user_id),
            UNIQUE (guild_id, alias)
        ) WITHOUT ROWID;
     )sql"},
}};

constexpr std::string_view module_name = "llm";

class llm_module final : public modules::module {
public:
    explicit llm_module(modules::host& bot)
        : bot_(&bot),
          section_(read_section(bot)),
          keys_(keys_from_environment()),
          usage_(bot.database()),
          documents_(bot.database()),
          memories_(bot.database()),
          aliases_(bot.database()),
          blacklist_(bot.database()),
          triggers_(bot.database()),
          windows_(bot.clock()),
          activity_(bot.clock()),
          anthropic_(keys_.anthropic ? std::make_unique<anthropic_provider>(bot.http(), *keys_.anthropic) : nullptr),
          openai_(keys_.openai ? std::make_unique<openai_provider>(bot.http(), *keys_.openai) : nullptr),
          panels_(services()) {}

    [[nodiscard]] auto name() const -> std::string_view override { return module_name; }
    [[nodiscard]] auto schema() const -> std::span<const db::migration> override { return llm_steps; }

    auto start(modules::host& bot) -> void override {
        // The keys are a sign-in, and never logged; the log channel masks
        // them in case something ever does.
        if (keys_.anthropic) bot.secret(*keys_.anthropic);
        if (keys_.openai) bot.secret(*keys_.openai);

        // The model's memory, as tools it can call
        // (src/modules/llm/docs/Language_Model.md §3.4).
        add_memory_tools(tools_, memories_);

        // Info: a missing key is the whole reason the model would never
        // answer, and the log is where that question gets asked.
        if (anthropic_ == nullptr && openai_ == nullptr) {
            util::log().info("the language model is off everywhere: neither ANTHROPIC_API_KEY nor OPENAI_API_KEY is set");
        } else {
            util::log().info("the language model can use {}{}{}; {} by default, capped at ${:.2f} a day and ${:.2f} a month",
                             anthropic_ != nullptr ? "Anthropic" : "", anthropic_ != nullptr && openai_ != nullptr ? " and " : "",
                             openai_ != nullptr ? "OpenAI" : "", section_.model, section_.spend_cap_daily_usd,
                             section_.spend_cap_monthly_usd);
        }

        // Spoken replies, when DECtalk is built in; null otherwise, and then
        // the model never speaks (docs/modules/Module_Plan_Final.md §5.3).
        auto* speech = bot.capabilities().find<capabilities::speech>();
        util::log().debug("the language model {}", speech != nullptr ? "speaks in voice channels with a session" : "never speaks");

        // Replies to link replacements left alone, when links is built in.
        const auto* replacements = bot.capabilities().find<capabilities::link_replacements>();

        const auto me = [this] { return bot_identity{.id = bot_->me().id, .name = bot_->me().username}; };
        responder_ =
            std::make_unique<responder>(responder_services{.discord = &bot.gateway(),
                                                           .clock = &bot.clock(),
                                                           .settings = &bot.settings(),
                                                           .section = &section_,
                                                           .documents = &documents_,
                                                           .memories = &memories_,
                                                           .usage = &usage_,
                                                           .tools = &tools_,
                                                           .aliases = &aliases_,
                                                           .provider_for = [this](provider_kind kind) { return provider_for(kind); },
                                                           .speech = speech},
                                        me);
        stage_ =
            std::make_unique<llm_stage>(stage_services{.settings = &bot.settings(),
                                                       .section = &section_,
                                                       .blacklist = &blacklist_,
                                                       .triggers = &triggers_,
                                                       .usage = &usage_,
                                                       .speech = speech,
                                                       .replacements = replacements,
                                                       .windows = &windows_,
                                                       .activity = &activity_,
                                                       .has_provider = [this](provider_kind kind) { return provider_for(kind) != nullptr; },
                                                       .me = me},
                                        bot.clock());

        bot.slash_commands().add(std::make_unique<commands::llm_command>(services()));
        bot.slash_commands().add(std::make_unique<commands::memory_command>(services()));
        bot.panels().add(panels_,
                         {commands::llm_settings_view, commands::llm_settings_pick_view, commands::llm_settings_form_view,
                          commands::llm_switch_view, commands::llm_document_form_view, commands::memory_list_view},
                         module_name);

        // Conversation mode waits for people to finish typing
        // (src/modules/llm/docs/Language_Model.md §2.10). Not privileged.
        // The bot's own typing, while it thinks, is not someone else's.
        bot.intents(dpp::i_guild_message_typing);
        bot.listen(bot.cluster().on_typing_start, "llm: who is typing", [this](const dpp::typing_start_t& event) {
            if (event.user_id == bot_->me().id) return;
            util::log().trace("llm: {} started typing in channel {}", event.user_id, event.typing_channel.id);
            activity_.started_typing(event.typing_channel.id, event.user_id);
        });

        // Last: it consumes what it answers, and a simple trigger's reply
        // before it keeps an advanced trigger quiet
        // (src/core/docs/Message_Pipeline.md §2.2).
        bot.add_stage(
            events::stage_order::model, "language model",
            events::carried_out_by<ask_llm>([this](const events::incoming_message& message) { return (*stage_)(message); },
                                            "answering with the language model", [this](ask_llm ask) { return answer(std::move(ask)); }));
    }

private:
    /// The section, refused when the bot could not keep the model's spending
    /// in check with it: that stops startup, as a bad config.json key does,
    /// before anything connects.
    static auto read_section(modules::host& bot) -> llm_config {
        llm_config read = llm_section().read(bot.section(module_name));
        check_config(read);
        return read;
    }

    /// The provider that serves a kind of model, or null without its key.
    [[nodiscard]] auto provider_for(provider_kind kind) const -> provider* {
        return kind == provider_kind::openai ? openai_.get() : anthropic_.get();
    }

    /// Everything `/llm`, `/memory` and their panels work with.
    [[nodiscard]] auto services() -> commands::llm_command_services {
        return {.settings = &bot_->settings(),
                .section = &section_,
                .documents = &documents_,
                .triggers = &triggers_,
                .blacklist = &blacklist_,
                .memories = &memories_,
                .aliases = &aliases_,
                .usage = &usage_,
                .http = &bot_->http(),
                .clock = &bot_->clock(),
                .has_provider = [this](provider_kind kind) { return provider_for(kind) != nullptr; }};
    }

    /// Waits out any pacing, or anyone typing, asks the check when it has
    /// to agree first, then has the model answer
    /// (src/modules/llm/docs/Language_Model.md).
    auto answer(ask_llm ask) -> dpp::task<void> {
        util::log().trace("llm: answering message {} in channel {} ({}{})", ask.message_id, ask.channel_id, to_string(ask.how),
                          ask.check_first ? ", check first" : "");

        // Bot-to-bot pacing (src/modules/llm/docs/Language_Model.md §2.7). The turn
        // was claimed when the stage decided, so the wait only spaces it out.
        if (ask.wait > std::chrono::seconds::zero()) {
            util::log().trace("llm: pacing message {}: waiting {}", ask.message_id, ask.wait);
            co_await bot_->cluster().co_sleep(static_cast<std::uint64_t>(ask.wait.count()));
        }

        // Unaddressed, it lets people finish, and answers only their latest
        // (src/modules/llm/docs/Language_Model.md §2.10).
        const bool unprompted = ask.how == approach::named || ask.how == approach::joined_in;
        if (unprompted) {
            co_await wait_for_typing(ask);
            if (!activity_.is_latest(ask.channel_id, ask.author_id, ask.message_id)) {
                util::log().debug("not checking message {} in channel {}: its author has said more since", ask.message_id, ask.channel_id);
                co_return;
            }
        }

        if (ask.check_first) {
            const check_report checked = co_await responder_->check(ask);
            if (!checked.yes) {
                util::log().trace("llm: message {} left alone: the check said no{}", ask.message_id,
                                  checked.failure.empty() ? "" : " (it failed)");
                co_return;
            }
            if (!stage_->claim_reply(ask)) {
                util::log().trace("llm: message {} left alone: the check said yes, but the rate limits are used up", ask.message_id);
                co_return;
            }
        }

        const answer_report report = co_await responder_->answer(ask);
        if (report.posted.empty()) {
            util::log().trace("llm: nothing posted for message {}: {}", ask.message_id,
                              report.failure.empty() ? "no reason given" : report.failure);
            co_return;
        }
        if (ask.how == approach::joined_in) {
            windows_.joined_in(ask.channel_id);
            util::log().trace("llm: the conversation in channel {} goes on", ask.channel_id);
        } else if (spoke_to_bot(ask)) {
            windows_.opened(ask.channel_id);
            util::log().trace("llm: a conversation is open in channel {}", ask.channel_id);
        }
    }

    /// Waits while anyone in the channel is typing, a second at a time, up
    /// to the guild's `llm_conversation_typing`.
    auto wait_for_typing(const ask_llm& ask) -> dpp::task<void> {
        const llm_settings settings = load_llm_settings(bot_->settings(), ask.guild_id, section_);
        std::chrono::seconds waited{0};
        for (; waited < settings.conversation.typing_wait && activity_.anyone_typing(ask.channel_id); waited += std::chrono::seconds{1}) {
            co_await bot_->cluster().co_sleep(1);
        }
        if (waited > std::chrono::seconds::zero()) {
            util::log().trace("llm: waited {} for typing to stop in channel {}{}", waited, ask.channel_id,
                              activity_.anyone_typing(ask.channel_id) ? ", and gave up" : "");
        }
    }

    modules::host* bot_;
    llm_config section_;
    provider_keys keys_;
    usage_store usage_;
    document_store documents_;
    memory_store memories_;
    alias_store aliases_;
    blacklist_store blacklist_;
    advanced_trigger_store triggers_;
    conversation_windows windows_;
    channel_activity activity_;
    tool_registry tools_;
    // A provider exists only when its key is set; the stage and the commands
    // ask `provider_for` rather than assume.
    std::unique_ptr<provider> anthropic_;
    std::unique_ptr<provider> openai_;
    commands::llm_panels panels_;
    // Made in start, once the speech capability can be looked up.
    std::unique_ptr<responder> responder_;
    std::unique_ptr<llm_stage> stage_;
};

} // namespace

auto llm_schema() noexcept -> db::module_schema {
    return {.module = module_name, .steps = llm_steps};
}

auto make_module(modules::host& bot) -> std::unique_ptr<modules::module> {
    return std::make_unique<llm_module>(bot);
}

auto config_defaults() -> nlohmann::ordered_json {
    return llm_section().defaults();
}

} // namespace latibot::llm
