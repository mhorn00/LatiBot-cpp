#include "midnight/module.hpp"

#include "core/commands/registry.hpp"
#include "core/modules/host.hpp"
#include "core/util/log.hpp"
#include "midnight.hpp"
#include "midnight_command.hpp"

#include <array>
#include <string_view>
#include <variant>
#include <vector>

namespace latibot::events {
namespace {

// Append only: once a version has shipped, its SQL is never edited, and a
// change becomes the next version.
constexpr std::array<db::migration, 1> midnight_steps{{
    {.version = 1, .name = "midnight_messages", .sql = R"sql(
        -- A message posted once per local day, per timezone.
        CREATE TABLE midnight_messages (
            id              INTEGER PRIMARY KEY,
            guild_id        INTEGER NOT NULL,
            channel_id      INTEGER NOT NULL,

            -- An IANA name. Different entries in one guild may use different
            -- zones, which is the point of there being any number of them.
            timezone        TEXT    NOT NULL,

            message         TEXT    NOT NULL,
            enabled         INTEGER NOT NULL DEFAULT 1,

            -- The local date this last posted, YYYY-MM-DD, NULL for never.
            -- Saved rather than counted from, so a restart at 00:00:30 does
            -- not post a second time.
            last_fired_date TEXT,

            -- How it is posted, as Discord's message flags: 4096 is
            -- SUPPRESS_NOTIFICATIONS, 4 is SUPPRESS_EMBEDS.
            message_flags   INTEGER NOT NULL DEFAULT 4096
        );

        CREATE INDEX midnight_messages_by_guild ON midnight_messages (guild_id);
     )sql"},
}};

constexpr std::string_view module_name = "midnight";

class midnight_module final : public modules::module {
public:
    explicit midnight_module(modules::host& bot) : store_(bot.database()), scheduler_(store_, bot.clock()) {}

    [[nodiscard]] auto name() const -> std::string_view override { return module_name; }
    [[nodiscard]] auto schema() const -> std::span<const db::migration> override { return midnight_steps; }

    auto start(modules::host& bot) -> void override {
        bot.slash_commands().add(std::make_unique<commands::midnight_command>(store_, bot.clock()));

        // Polling the wall clock is the fix for the Java bot's random-fire
        // bug: it computed a delay from the wall clock and then waited on a
        // monotonic timer, so a machine that slept woke up and posted at
        // whatever time it happened to be (docs/features/Midnight.md §1).
        bot.every(midnight_tick, "the midnight tick", [this, &bot] {
            for (action& wanted : scheduler_.tick()) {
                // The scheduler only ever posts.
                if (auto* post = std::get_if<send_message>(&wanted)) bot.post(std::move(*post));
            }
        });
        util::log().debug("midnight messages checked every {}", midnight_tick);
    }

private:
    midnight_store store_;
    midnight_scheduler scheduler_;
};

} // namespace

auto midnight_schema() noexcept -> db::module_schema {
    return {.module = module_name, .steps = midnight_steps};
}

} // namespace latibot::events

namespace latibot::midnight {

auto make_module(modules::host& bot) -> std::unique_ptr<modules::module> {
    return std::make_unique<events::midnight_module>(bot);
}

} // namespace latibot::midnight
