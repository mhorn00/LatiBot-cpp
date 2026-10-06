#include "linkstats/module.hpp"

#include "backfill.hpp"
#include "core/commands/registry.hpp"
#include "core/config/bootstrap.hpp"
#include "core/modules/host.hpp"
#include "core/ports/clock.hpp"
#include "core/ui/panel_routes.hpp"
#include "core/util/log.hpp"
#include "core/util/url_scan.hpp"
#include "emoji_copies.hpp"
#include "emote_reactions.hpp"
#include "legacy_replacements.hpp"
#include "links/replacements.hpp"
#include "links/url_rules.hpp"
#include "linkstats_command.hpp"
#include "linkstats_config.hpp"
#include "media_posts.hpp"
#include "reactions.hpp"

#include <dpp/dpp.h>

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::events {
namespace {

// Append only: once a version has shipped, its SQL is never edited, and a
// change becomes the next version.
constexpr std::array<db::migration, 1> linkstats_steps{{
    {.version = 1, .name = "reaction statistics", .sql = R"sql(
        -- Who reacted with what on a replacement message. The poster comes
        -- from replacement_messages, so one row answers both "who received"
        -- and "who gave". Kept forever.
        CREATE TABLE reactions (
            message_id INTEGER NOT NULL REFERENCES replacement_messages (message_id),
            user_id    INTEGER NOT NULL,

            -- u:<unicode> or c:<custom emoji id>
            emoji_key  TEXT    NOT NULL,

            -- Unix seconds, NULL when recomputed: Discord says who reacted,
            -- never when.
            reacted_at INTEGER,

            PRIMARY KEY (message_id, user_id, emoji_key)
        ) WITHOUT ROWID;

        CREATE INDEX reactions_by_user ON reactions (user_id);

        -- Every add and remove seen live, for questions nobody has asked yet.
        CREATE TABLE reaction_log (
            id         INTEGER PRIMARY KEY,
            message_id INTEGER NOT NULL,
            user_id    INTEGER NOT NULL,
            emoji_key  TEXT    NOT NULL,
            action     TEXT    NOT NULL,   -- add | remove
            at         INTEGER NOT NULL
        );

        -- What a key looks like, for showing it: a custom emoji is only an id
        -- in the rows above.
        CREATE TABLE emojis (
            emoji_key TEXT    PRIMARY KEY,
            name      TEXT    NOT NULL,
            animated  INTEGER NOT NULL DEFAULT 0
        ) WITHOUT ROWID;

        -- Emojis that should count as one: the same emote from another server,
        -- or one deleted and re-added. Applied when stats are read, so adding
        -- or removing one changes all of history at once.
        CREATE TABLE emoji_aliases (
            guild_id      INTEGER NOT NULL,
            emoji_key     TEXT    NOT NULL,
            canonical_key TEXT    NOT NULL,
            PRIMARY KEY (guild_id, emoji_key)
        ) WITHOUT ROWID;

        -- How far a /linkstats recompute got in each channel, so one that was
        -- cancelled or cut short by a restart carries on rather than starting
        -- over. A row belongs to one date range; a run over a different range
        -- starts that channel again.
        CREATE TABLE backfill_progress (
            guild_id          INTEGER NOT NULL,
            channel_id        INTEGER NOT NULL,

            -- The range, Unix seconds; until is NULL for "up to now".
            since             INTEGER NOT NULL,
            until             INTEGER,

            -- The oldest message looked at so far; NULL before the first page.
            oldest_scanned_id INTEGER,
            complete          INTEGER NOT NULL DEFAULT 0,
            updated_at        INTEGER NOT NULL,

            PRIMARY KEY (guild_id, channel_id)
        ) WITHOUT ROWID;

        -- What became of each custom emoji's image, for the bot's own copies
        -- of them (docs/features/Link_Stats.md 10).
        CREATE TABLE emoji_images (
            emoji_key    TEXT    PRIMARY KEY,   -- c:<id>

            -- fetched | lost (the CDN no longer has it) | too_big | failed
            state        TEXT    NOT NULL,

            -- SHA-256 of the image, hex; NULL unless fetched. Emojis with
            -- the same image share one copy.
            image_sha256 TEXT,
            animated     INTEGER NOT NULL DEFAULT 0,

            -- Unix seconds: when it was last tried, for retrying the lost
            -- and failed ones now and then.
            checked_at   INTEGER NOT NULL
        ) WITHOUT ROWID;

        CREATE INDEX emoji_images_by_image ON emoji_images (image_sha256);

        -- One application emoji per distinct image.
        CREATE TABLE emoji_copies (
            image_sha256 TEXT    PRIMARY KEY,
            copy_id      INTEGER NOT NULL,
            name         TEXT    NOT NULL UNIQUE,
            animated     INTEGER NOT NULL,
            created_at   INTEGER NOT NULL
        ) WITHOUT ROWID;

        -- Emojis somebody sent as a message of their own just after a post,
        -- or as a reply to it, which count as reactions to it
        -- (docs/features/Link_Stats.md 12).
        CREATE TABLE emote_reactions (
            message_id INTEGER NOT NULL REFERENCES replacement_messages (message_id),
            user_id    INTEGER NOT NULL,
            emoji_key  TEXT    NOT NULL,

            -- The message the emoji was sent in, which also dates it.
            source_id  INTEGER NOT NULL,

            PRIMARY KEY (message_id, user_id, emoji_key)
        ) WITHOUT ROWID;

        CREATE INDEX emote_reactions_by_source ON emote_reactions (source_id);

        -- What every statistic counts: the reactions, and the emotes sent as
        -- reactions that the same person did not also react with, so one
        -- person's emoji on one post counts once however it was given. An
        -- emote is dated by its message: a snowflake's top bits are
        -- milliseconds since 2015, Discord's epoch.
        CREATE VIEW counted_reactions AS
            SELECT message_id, user_id, emoji_key, reacted_at FROM reactions
            UNION ALL
            SELECT e.message_id, e.user_id, e.emoji_key, ((e.source_id >> 22) + 1420070400000) / 1000 FROM emote_reactions e
            WHERE NOT EXISTS (SELECT 1 FROM reactions r
                              WHERE r.message_id = e.message_id AND r.user_id = e.user_id AND r.emoji_key = e.emoji_key);
     )sql"},
}};

constexpr std::string_view module_name = "linkstats";

/// Every channel in a guild that holds ordinary messages, from DPP's cache,
/// for a recompute that was not given one. Threads are left out: listing the
/// archived ones is its own set of calls, and links in them are rare.
auto text_channels(dpp::snowflake guild_id) -> std::vector<dpp::snowflake> {
    std::vector<dpp::snowflake> found;
    const dpp::guild* guild = dpp::find_guild(guild_id);
    if (guild == nullptr) return found;

    for (const dpp::snowflake id : guild->channels) {
        const dpp::channel* channel = dpp::find_channel(id);
        if (channel != nullptr && (channel->is_text_channel() || channel->is_news_channel())) found.push_back(id);
    }
    return found;
}

/// The boards' paging, and `/linkstats duplicates`' picking and merging.
class stats_panel {
public:
    explicit stats_panel(reaction_store& store) : store_(&store) {}

    auto on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool {
        return commands::on_linkstats_component(*store_, event, state, chosen);
    }

private:
    reaction_store* store_;
};

class linkstats_module final : public modules::module {
public:
    explicit linkstats_module(modules::host& bot)
        : bot_(&bot),
          config_(linkstats::linkstats_section().read(bot.section(module_name))),
          recompute_bot_id_(config::recompute_bot_id_from_environment()),
          rules_(bot.database()),
          replacements_(bot.database()),
          media_(replacements_, bot.settings(), bot.clock()),
          reactions_(bot.database()),
          emotes_(replacements_, reactions_),
          progress_(bot.database()),
          backfill_(bot.gateway(), rules_, replacements_, reactions_, progress_, bot.clock()),
          copies_(bot.database()),
          copier_(copies_, bot.http(), bot.gateway(), bot.clock(), config_.emoji_copy_min_uses),
          panel_(reactions_) {}

    [[nodiscard]] auto name() const -> std::string_view override { return module_name; }
    [[nodiscard]] auto schema() const -> std::span<const db::migration> override { return linkstats_steps; }

    auto start(modules::host& bot) -> void override {
        // A warning rather than info: it changes what a recompute records,
        // and it should not be the kind of thing that stays set by accident.
        if (recompute_bot_id_) {
            util::log().warn(
                "LATIBOT_DEBUG_RECOMPUTE_BOT_ID is set: /linkstats recompute reads replacements posted by {}, not this bot's own. "
                "Pass fresh:true to go over channels already recomputed without it.",
                *recompute_bot_id_);
        }

        bot.slash_commands().add(std::make_unique<commands::linkstats_command>(
            reactions_,
            commands::recompute_support{.service = &backfill_,
                                        .discord = &bot.gateway(),
                                        .channels_of = [](dpp::snowflake guild) { return text_channels(guild); },
                                        .bot_id = [this] { return recompute_bot_id_.value_or(bot_->me().id); }},
            &bot.settings()));
        bot.panels().add(panel_, {commands::board_view, commands::similar_view, commands::keep_view, commands::merge_view}, module_name);

        bot.listen(bot.cluster().on_message_create, "linkstats: image posts and emotes sent as reactions",
                   [this](const dpp::message_create_t& event) {
                       // Somebody's image or video, in a server that counts
                       // reactions on them (docs/features/Link_Stats.md §9).
                       const dpp::message& message = event.msg;
                       media_.on_message({.message_id = message.id,
                                          .guild_id = message.guild_id,
                                          .channel_id = message.channel_id,
                                          .author_id = message.author.id,
                                          .from_person = !message.author.is_bot() && message.webhook_id.empty(),
                                          .has_media = has_media(message),
                                          .has_links = !util::find_links(message.content).empty()});
                       // Emotes sent as a message of their own after a post,
                       // which count as reactions to it
                       // (docs/features/Link_Stats.md §12).
                       if (!message.guild_id.empty()) emotes_.on_message(message.channel_id, as_emote_message(describe_history(message)));
                   });
        // The preview that shows a link was an image arrives as an update,
        // and emotes already sent after it are counted then.
        bot.listen(bot.cluster().on_message_update, "linkstats: a link turning out to be an image",
                   [this](const dpp::message_update_t& event) {
                       if (media_.on_update(event.msg.id, has_media(event.msg))) emotes_.on_post(event.msg.channel_id, event.msg.id);
                   });
        bot.listen(bot.cluster().on_message_delete, "linkstats: emotes deleted",
                   [this](const dpp::message_delete_t& event) { emotes_.on_delete(event.channel_id, event.id); });

        // Reaction statistics (docs/features/Link_Stats.md §3). Every
        // reaction in every channel arrives here; the store counts the ones on
        // our replacements and ignores the rest in the same statement that
        // would have recorded them.
        bot.listen(bot.cluster().on_message_reaction_add, "linkstats: reactions added", [this](const dpp::message_reaction_add_t& event) {
            const dpp::emoji& emoji = event.reacting_emoji;
            const auto reacted = reaction_emoji(emoji.id, emoji.name, emoji.is_animated());
            if (reactions_.add(event.message_id, event.reacting_user.id, reacted, now_seconds())) {
                util::log().debug("{} reacted {} to replacement {}", event.reacting_user.id, reacted.key, event.message_id);
            }
        });
        bot.listen(bot.cluster().on_message_reaction_remove, "linkstats: reactions taken back",
                   [this](const dpp::message_reaction_remove_t& event) {
                       const auto reacted = reaction_emoji(event.reacting_emoji.id, event.reacting_emoji.name);
                       if (reactions_.remove(event.message_id, event.reacting_user_id, reacted.key, now_seconds())) {
                           util::log().debug("{} took back {} on replacement {}", event.reacting_user_id, reacted.key, event.message_id);
                       }
                   });
        bot.listen(bot.cluster().on_message_reaction_remove_emoji, "linkstats: an emoji cleared",
                   [this](const dpp::message_reaction_remove_emoji_t& event) {
                       const auto reacted = reaction_emoji(event.reacting_emoji.id, event.reacting_emoji.name);
                       if (const int gone = reactions_.remove_emoji(event.message_id, reacted.key, now_seconds()); gone > 0) {
                           util::log().debug("{} cleared from replacement {}: {} reaction(s)", reacted.key, event.message_id, gone);
                       }
                   });
        bot.listen(bot.cluster().on_message_reaction_remove_all, "linkstats: every reaction cleared",
                   [this](const dpp::message_reaction_remove_all_t& event) {
                       if (const int gone = reactions_.remove_all(event.message_id, now_seconds()); gone > 0) {
                           util::log().debug("every reaction cleared from replacement {}: {}", event.message_id, gone);
                       }
                   });

        // The bot's own copies of the emojis it has seen, a few at a time.
        // The copies belong to the bot's application, which it only knows
        // once connected.
        if (copier_.enabled()) {
            bot.every(copy_round_interval, "copying emojis", [this] {
                if (!bot_->me().id.empty()) bot_->detach(copy_round(), "copying emojis");
            });
            util::log().info("keeping copies of emojis used at least {} time{}", config_.emoji_copy_min_uses,
                             config_.emoji_copy_min_uses == 1 ? "" : "s");
        } else {
            util::log().info("copying emojis is off");
        }
    }

private:
    /// One round of keeping the bot's own copies of emojis
    /// (docs/features/Link_Stats.md §10); the round logs what it did.
    auto copy_round() -> dpp::task<void> { co_await copier_.run_round(); }

    /// The clock's time to the second, which is what the database stores.
    [[nodiscard]] auto now_seconds() const -> std::chrono::sys_seconds {
        return std::chrono::floor<std::chrono::seconds>(bot_->clock().now());
    }

    modules::host* bot_;
    linkstats::linkstats_config config_;
    std::optional<dpp::snowflake> recompute_bot_id_;
    // Links' stores, over the same database: they hold nothing but it.
    url_rule_store rules_;
    replacement_store replacements_;
    media_tracker media_;
    reaction_store reactions_;
    emote_tracker emotes_;
    backfill_progress_store progress_;
    backfill_service backfill_;
    emoji_copy_store copies_;
    emoji_copier copier_;
    stats_panel panel_;
};

} // namespace

auto linkstats_schema() noexcept -> db::module_schema {
    return {.module = module_name, .steps = linkstats_steps};
}

} // namespace latibot::events

namespace latibot::linkstats {

auto make_module(modules::host& bot) -> std::unique_ptr<modules::module> {
    return std::make_unique<events::linkstats_module>(bot);
}

auto config_defaults() -> nlohmann::ordered_json {
    return linkstats_section().defaults();
}

} // namespace latibot::linkstats
