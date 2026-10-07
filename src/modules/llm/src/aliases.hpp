#pragma once

#include "core/ports/discord_gateway.hpp"

#include <dpp/snowflake.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::llm {

// Who the model is told about, without being told who they are
// (src/modules/llm/docs/Language_Model.md §3.8). Every person is an alias, like
// u7kx3q, random and kept per server; their Discord id and their name are
// never sent. Where the model wants a name it writes a marker, <u7kx3q:name>,
// which the bot replaces in what it posts.

/// A new alias: "u" and six characters with no vowels, so it never spells a
/// word that could be mistaken for one, nor be mistaken for a word.
[[nodiscard]] auto random_alias() -> std::string;

/// Whether `text` is shaped like an alias.
[[nodiscard]] auto is_alias(std::string_view text) -> bool;

/// `llm_aliases`: each person's alias in each server, and what they were
/// last seen called there.
class alias_store {
public:
    explicit alias_store(db::database& db) : db_(&db) {}

    /// Their alias in this server: made the first time it is asked for, the
    /// same ever after.
    auto alias_for(dpp::snowflake guild_id, dpp::snowflake user_id) -> std::string;

    /// Who an alias in this server is.
    [[nodiscard]] auto user_for(dpp::snowflake guild_id, std::string_view alias) const -> std::optional<dpp::snowflake>;

    /// Keeps what someone is called here, to put back into a reply when the
    /// bot's cache does not know them. Never sent to the model.
    auto note_names(dpp::snowflake guild_id, dpp::snowflake user_id, std::string_view name, std::string_view username) -> void;

    /// What they were last noted as: `display_name` holds the name.
    [[nodiscard]] auto noted_names(dpp::snowflake guild_id, dpp::snowflake user_id) const -> std::optional<ports::member_names>;

private:
    db::database* db_;
};

/// The people in one request to the model: what it is told about them, and
/// how what it writes is turned back into names.
///
/// Everyone is met first (`meet`), then texts are sanitized: the names of
/// everyone met are found in them. Not thread-safe; one per request.
class people {
public:
    people(alias_store& aliases, const ports::discord_gateway& discord, dpp::snowflake guild_id, dpp::snowflake bot_id,
           std::string bot_name);

    /// Someone in the request, with what a message said they are called.
    /// The bot's cache and the store fill in the rest. Gives their alias;
    /// the bot itself has none, and gets its own name.
    auto meet(dpp::snowflake user_id, std::string_view shown = {}, std::string_view username = {}) -> std::string;

    /// Meets everyone `text` mentions as `<@id>`.
    auto meet_mentioned(std::string_view text) -> void;

    /// Who an alias is: someone met, or anyone in this server's store.
    [[nodiscard]] auto user_for(std::string_view alias) const -> std::optional<dpp::snowflake>;

    /// `text` as the model may see it. Mentions of people become their
    /// aliases, and of the bot its name; roles, channels, custom emoji,
    /// timestamps and commands become their names. With `names_too`, the
    /// names of everyone met become markers wherever they are written.
    [[nodiscard]] auto sanitize(std::string_view text, bool names_too = true) -> std::string;

    /// What the model wrote, as it is posted: markers and bare aliases become
    /// names, and `<alias:mention>` a mention, or for speech a name too. A
    /// marker for someone unknown becomes "someone".
    [[nodiscard]] auto restore(std::string_view text, bool for_speech = false) const -> std::string;

    [[nodiscard]] auto bot_name() const -> const std::string& { return bot_name_; }

private:
    struct person {
        dpp::snowflake id;
        std::string alias;
        /// What people see here.
        std::string shown;
        std::string username;
        /// Every name to look for in what people write, username last.
        std::vector<std::string> names;
    };

    /// Someone met, by alias; nothing for someone not met.
    [[nodiscard]] auto find(std::string_view alias) const -> const person*;
    /// What `<inner>` becomes, or nothing to leave it as text.
    [[nodiscard]] auto sanitize_tag(std::string_view inner) -> std::optional<std::string>;
    /// Names become markers in text with no tags in it.
    [[nodiscard]] auto replace_names(std::string_view text) const -> std::string;
    /// What a marker, or a bare alias with `kind` "name", becomes.
    [[nodiscard]] auto restore_marker(std::string_view alias, std::string_view kind, bool for_speech) const -> std::string;

    alias_store* aliases_;
    const ports::discord_gateway* discord_;
    dpp::snowflake guild_id_;
    dpp::snowflake bot_id_;
    std::string bot_name_;

    std::map<dpp::snowflake, person> met_;
    std::map<std::string, dpp::snowflake, std::less<>> by_alias_;
};

/// `text` with markers and bare aliases made into `<@id>` mentions, for
/// showing memories to people in Discord, which shows each as a name.
[[nodiscard]] auto aliases_as_mentions(std::string_view text, dpp::snowflake guild_id, const alias_store& aliases) -> std::string;

} // namespace latibot::llm
