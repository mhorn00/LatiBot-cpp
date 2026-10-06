#include "core/llm/aliases.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <random>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace latibot::llm {
namespace {

/// No vowels, no y, and none of 0, 1 and l, which look like letters.
constexpr std::string_view alias_letters = "bcdfghjkmnpqrstvwxz23456789";
constexpr std::size_t alias_length = 7;

/// The longest tag looked at: a mention, an emoji or a timestamp is far
/// shorter.
constexpr std::size_t longest_tag = 120;

/// The shortest name looked for in what people write: shorter ones are
/// mostly words.
constexpr std::size_t shortest_name = 3;

auto all_digits(std::string_view text) -> bool {
    return !text.empty() && std::ranges::all_of(text, [](char letter) { return letter >= '0' && letter <= '9'; });
}

/// Part of a word: letters, digits, underscores, and anything not ASCII,
/// which is mostly letters.
auto is_word_byte(char letter) -> bool {
    const auto byte = static_cast<unsigned char>(letter);
    return byte >= 0x80 || byte == '_' || (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
}

auto is_marker_kind(std::string_view kind) -> bool {
    return !kind.empty() && std::ranges::all_of(kind, [](char letter) { return (letter >= 'a' && letter <= 'z') || letter == '_'; });
}

/// `alias:kind` split, when `inner` is a marker.
auto split_marker(std::string_view inner) -> std::optional<std::pair<std::string_view, std::string_view>> {
    const std::size_t colon = inner.find(':');
    if (colon == std::string_view::npos) return std::nullopt;
    const std::string_view alias = inner.substr(0, colon);
    const std::string_view kind = inner.substr(colon + 1);
    if (!is_alias(alias) || !is_marker_kind(kind)) return std::nullopt;
    return std::pair{alias, kind};
}

/// `text` with each `<...>` handed to `on_tag`, which gives what it
/// becomes or nothing to leave it, and the text between handed to
/// `on_text`.
template <typename OnTag, typename OnText>
auto rewrite(std::string_view text, OnTag on_tag, OnText on_text) -> std::string {
    std::string out;
    std::size_t plain = 0;
    std::size_t open = text.find('<');
    while (open != std::string_view::npos) {
        const std::size_t close = text.find('>', open + 1);
        if (close == std::string_view::npos) break;
        if (close - open <= longest_tag) {
            if (std::optional<std::string> made = on_tag(text.substr(open + 1, close - open - 1))) {
                out += on_text(text.substr(plain, open - plain));
                out += *made;
                plain = close + 1;
                open = text.find('<', plain);
                continue;
            }
        }
        open = text.find('<', open + 1);
    }
    out += on_text(text.substr(plain));
    return out;
}

/// `text` with every bare alias handed to `on_alias`, which gives what it
/// becomes or nothing to leave it.
template <typename OnAlias>
auto rewrite_bare_aliases(std::string_view text, OnAlias on_alias) -> std::string {
    std::string out;
    std::size_t at = 0;
    while (at < text.size()) {
        const bool starts_word = at == 0 || !is_word_byte(text[at - 1]);
        const bool ends_word = at + alias_length >= text.size() || !is_word_byte(text[at + alias_length]);
        if (starts_word && ends_word && at + alias_length <= text.size() && is_alias(text.substr(at, alias_length))) {
            if (std::optional<std::string> made = on_alias(text.substr(at, alias_length))) {
                out += *made;
                at += alias_length;
                continue;
            }
        }
        out += text[at];
        ++at;
    }
    return out;
}

/// The id in a mention of a person, `@id` or the older `@!id`.
auto mentioned_person(std::string_view inner) -> std::optional<dpp::snowflake> {
    if (!inner.starts_with('@') || inner.starts_with("@&")) return std::nullopt;
    const std::string_view id = inner.substr(inner.starts_with("@!") ? 2 : 1);
    if (!all_digits(id)) return std::nullopt;
    return util::parse_snowflake(id);
}

/// A custom emoji, `:name:id` or `a:name:id`, by name.
auto emoji_text(std::string_view inner) -> std::optional<std::string> {
    if (!inner.starts_with(':') && !inner.starts_with("a:")) return std::nullopt;
    const std::string_view rest = inner.substr(inner.starts_with('a') ? 2 : 1);
    const std::size_t colon = rest.rfind(':');
    if (colon == std::string_view::npos || colon == 0 || !all_digits(rest.substr(colon + 1))) return std::nullopt;
    return std::format(":{}:", rest.substr(0, colon));
}

/// A timestamp, `t:seconds` or `t:seconds:style`, as a date and time.
auto timestamp_text(std::string_view inner) -> std::optional<std::string> {
    if (!inner.starts_with("t:")) return std::nullopt;
    std::string_view seconds = inner.substr(2);
    if (const std::size_t colon = seconds.find(':'); colon != std::string_view::npos && colon + 2 == seconds.size()) {
        seconds = seconds.substr(0, colon);
    }
    if (!all_digits(seconds) || seconds.size() > 12) return std::nullopt;
    const std::chrono::sys_seconds when{std::chrono::seconds{std::stoll(std::string(seconds))}};
    return std::format("{:%Y-%m-%d %H:%M} UTC", when);
}

/// A command, `/name:id`, by name.
auto command_text(std::string_view inner) -> std::optional<std::string> {
    if (!inner.starts_with('/')) return std::nullopt;
    const std::size_t colon = inner.rfind(':');
    if (colon == std::string_view::npos || colon <= 1 || !all_digits(inner.substr(colon + 1))) return std::nullopt;
    return std::string(inner.substr(0, colon));
}

auto first_of(std::initializer_list<std::string_view> names) -> std::string {
    for (const std::string_view name : names) {
        if (!util::is_blank(name)) return std::string(util::trim(name));
    }
    return {};
}

} // namespace

auto random_alias() -> std::string {
    thread_local std::mt19937_64 random{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick(0, alias_letters.size() - 1);
    std::string alias = "u";
    while (alias.size() < alias_length) {
        alias += alias_letters[pick(random)];
    }
    return alias;
}

auto is_alias(std::string_view text) -> bool {
    return text.size() == alias_length && text.front() == 'u' &&
           std::ranges::all_of(text.substr(1), [](char letter) { return alias_letters.contains(letter); });
}

// --------------------------------------------------------------------------
// The store
// --------------------------------------------------------------------------

auto alias_store::alias_for(dpp::snowflake guild_id, dpp::snowflake user_id) -> std::string {
    // Insert-or-ignore, then read back: two answers meeting someone new at
    // once agree on one alias, and an alias already taken here is ignored
    // and another tried.
    for (int attempt = 0; attempt < 20; ++attempt) {
        {
            db::statement found = db_->prepare("SELECT alias FROM llm_aliases WHERE guild_id = ? AND user_id = ?", guild_id, user_id);
            if (found.step()) return found.get<std::string>(0);
        }
        db::statement made = db_->prepare("INSERT OR IGNORE INTO llm_aliases (guild_id, user_id, alias) VALUES (?, ?, ?)", guild_id,
                                          user_id, random_alias());
        (void)made.step();
    }
    throw std::runtime_error("could not make an alias");
}

auto alias_store::user_for(dpp::snowflake guild_id, std::string_view alias) const -> std::optional<dpp::snowflake> {
    if (!is_alias(alias)) return std::nullopt;
    db::statement found = db_->prepare("SELECT user_id FROM llm_aliases WHERE guild_id = ? AND alias = ?", guild_id, std::string(alias));
    if (!found.step()) return std::nullopt;
    return found.get<dpp::snowflake>(0);
}

auto alias_store::note_names(dpp::snowflake guild_id, dpp::snowflake user_id, std::string_view name, std::string_view username) -> void {
    // An empty one keeps what was noted; an unchanged pair writes nothing.
    db::statement noted = db_->prepare(
        "UPDATE llm_aliases SET name = CASE WHEN ?1 = '' THEN name ELSE ?1 END, username = CASE WHEN ?2 = '' THEN username ELSE ?2 END "
        "WHERE guild_id = ?3 AND user_id = ?4 AND ((?1 != '' AND name != ?1) OR (?2 != '' AND username != ?2))",
        std::string(name), std::string(username), guild_id, user_id);
    (void)noted.step();
}

auto alias_store::noted_names(dpp::snowflake guild_id, dpp::snowflake user_id) const -> std::optional<ports::member_names> {
    db::statement found = db_->prepare("SELECT name, username FROM llm_aliases WHERE guild_id = ? AND user_id = ?", guild_id, user_id);
    if (!found.step()) return std::nullopt;
    return ports::member_names{.nickname = {}, .display_name = found.get<std::string>(0), .username = found.get<std::string>(1)};
}

// --------------------------------------------------------------------------
// The people in a request
// --------------------------------------------------------------------------

people::people(alias_store& aliases, const ports::discord_gateway& discord, dpp::snowflake guild_id, dpp::snowflake bot_id,
               std::string bot_name)
    : aliases_(&aliases), discord_(&discord), guild_id_(guild_id), bot_id_(bot_id), bot_name_(std::move(bot_name)) {}

auto people::meet(dpp::snowflake user_id, std::string_view shown, std::string_view username) -> std::string {
    if (user_id == bot_id_) return bot_name_;

    auto [entry, fresh] = met_.try_emplace(user_id);
    person& who = entry->second;
    if (fresh) {
        who.id = user_id;
        who.alias = aliases_->alias_for(guild_id_, user_id);
        by_alias_.emplace(who.alias, user_id);
    }

    // The cache first, for the server nickname, which a fetched message
    // does not carry; then what the message said; then what was noted.
    const ports::member_names cached = discord_->member_names(guild_id_, user_id).value_or(ports::member_names{});
    const ports::member_names noted =
        who.shown.empty() ? aliases_->noted_names(guild_id_, user_id).value_or(ports::member_names{}) : ports::member_names{};
    const std::string before_shown = who.shown;
    const std::string before_username = who.username;

    const std::string now_shown = first_of({cached.nickname, shown, cached.display_name, noted.display_name, username, cached.username});
    if (!now_shown.empty()) who.shown = now_shown;
    const std::string now_username = first_of({username, cached.username, noted.username});
    if (!now_username.empty()) who.username = now_username;

    for (const std::string_view name :
         std::initializer_list<std::string_view>{cached.nickname, shown, cached.display_name, noted.display_name, who.username}) {
        const std::string trimmed(util::trim(name));
        if (trimmed.empty()) continue;
        const std::string lower = util::to_lower(trimmed);
        if (std::ranges::none_of(who.names, [&lower](const std::string& known) { return util::to_lower(known) == lower; })) {
            who.names.push_back(trimmed);
        }
    }

    if (who.shown != before_shown || who.username != before_username) aliases_->note_names(guild_id_, user_id, who.shown, who.username);
    return who.alias;
}

auto people::meet_mentioned(std::string_view text) -> void {
    (void)rewrite(
        text,
        [this](std::string_view inner) -> std::optional<std::string> {
            const auto user = mentioned_person(inner);
            if (!user) return std::nullopt;
            meet(*user);
            return std::string{};
        },
        [](std::string_view) { return std::string{}; });
}

auto people::user_for(std::string_view alias) const -> std::optional<dpp::snowflake> {
    if (const auto found = by_alias_.find(alias); found != by_alias_.end()) return found->second;
    return aliases_->user_for(guild_id_, alias);
}

auto people::find(std::string_view alias) const -> const person* {
    const auto found = by_alias_.find(alias);
    if (found == by_alias_.end()) return nullptr;
    return &met_.at(found->second);
}

auto people::sanitize_tag(std::string_view inner) -> std::optional<std::string> {
    if (const auto user = mentioned_person(inner)) return "@" + meet(*user);
    // Roles and channels, by name; a name the cache does not have is not
    // worth a number.
    if (inner.starts_with("@&") && all_digits(inner.substr(2))) {
        const auto role = util::parse_snowflake(inner.substr(2));
        const auto name = role ? discord_->role_name(*role) : std::nullopt;
        return name ? "@" + *name : std::string("@a role");
    }
    if (inner.starts_with('#') && all_digits(inner.substr(1))) {
        const auto channel = util::parse_snowflake(inner.substr(1));
        const auto name = channel ? discord_->channel_name(*channel) : std::nullopt;
        return name ? "#" + *name : std::string("#a channel");
    }
    if (auto emoji = emoji_text(inner)) return emoji;
    if (auto time = timestamp_text(inner)) return time;
    if (auto command = command_text(inner)) return command;
    // A marker someone, or a memory, already wrote: kept as it is, and out
    // of reach of the names.
    if (split_marker(inner)) return std::format("<{}>", inner);
    return std::nullopt;
}

auto people::replace_names(std::string_view text) const -> std::string {
    // Every name of everyone met, longest first, so "Big Bob" is found
    // before "Bob".
    std::vector<std::tuple<std::string, std::string_view, std::string_view>> names;
    for (const auto& [id, who] : met_) {
        for (const std::string& name : who.names) {
            if (util::character_count(name) < shortest_name) continue;
            const std::string_view kind = util::to_lower(name) == util::to_lower(who.username) ? "username" : "name";
            names.emplace_back(util::to_lower(name), who.alias, kind);
        }
    }
    if (names.empty() || text.empty()) return std::string(text);
    std::ranges::sort(names, [](const auto& left, const auto& right) { return std::get<0>(left).size() > std::get<0>(right).size(); });

    const std::string lower = util::to_lower(text);
    std::vector<bool> claimed(text.size(), false);
    // Where each found name starts, its length, and its marker.
    std::map<std::size_t, std::pair<std::size_t, std::string>> found;
    for (const auto& [name, alias, kind] : names) {
        for (std::size_t at = lower.find(name); at != std::string::npos; at = lower.find(name, at + 1)) {
            const std::size_t end = at + name.size();
            const bool whole = (at == 0 || !is_word_byte(lower[at - 1])) && (end == lower.size() || !is_word_byte(lower[end]));
            if (!whole || std::any_of(claimed.begin() + static_cast<std::ptrdiff_t>(at), claimed.begin() + static_cast<std::ptrdiff_t>(end),
                                      [](bool taken) { return taken; })) {
                continue;
            }
            std::fill(claimed.begin() + static_cast<std::ptrdiff_t>(at), claimed.begin() + static_cast<std::ptrdiff_t>(end), true);
            found.emplace(at, std::pair{name.size(), std::format("<{}:{}>", alias, kind)});
        }
    }

    std::string out;
    std::size_t copied = 0;
    for (const auto& [at, replacement] : found) {
        out.append(text.substr(copied, at - copied));
        out += replacement.second;
        copied = at + replacement.first;
    }
    out.append(text.substr(copied));
    return out;
}

auto people::sanitize(std::string_view text, bool names_too) -> std::string {
    return rewrite(
        text, [this](std::string_view inner) { return sanitize_tag(inner); },
        [this, names_too](std::string_view plain) { return names_too ? replace_names(plain) : std::string(plain); });
}

auto people::restore_marker(std::string_view alias, std::string_view kind, bool for_speech) const -> std::string {
    std::string shown;
    std::string username;
    std::optional<dpp::snowflake> user;
    if (const person* who = find(alias)) {
        shown = who->shown;
        username = who->username;
        user = who->id;
    } else {
        // Someone the model knows of from before, not in this request.
        user = aliases_->user_for(guild_id_, alias);
        if (!user) return "someone";
        const ports::member_names cached = discord_->member_names(guild_id_, *user).value_or(ports::member_names{});
        const ports::member_names noted = aliases_->noted_names(guild_id_, *user).value_or(ports::member_names{});
        shown = first_of({cached.nickname, cached.display_name, noted.display_name, cached.username, noted.username});
        username = first_of({cached.username, noted.username});
    }

    if (kind == "mention" && !for_speech) return std::format("<@{}>", *user);
    if (kind == "username" && !username.empty()) return username;
    return shown.empty() ? std::string("someone") : shown;
}

auto people::restore(std::string_view text, bool for_speech) const -> std::string {
    return rewrite(
        text,
        [this, for_speech](std::string_view inner) -> std::optional<std::string> {
            const auto marker = split_marker(inner);
            if (!marker) return std::nullopt;
            return restore_marker(marker->first, marker->second, for_speech);
        },
        [this, for_speech](std::string_view plain) {
            return rewrite_bare_aliases(plain, [this, for_speech](std::string_view alias) -> std::optional<std::string> {
                if (find(alias) == nullptr && !aliases_->user_for(guild_id_, alias)) return std::nullopt;
                return restore_marker(alias, "name", for_speech);
            });
        });
}

auto aliases_as_mentions(std::string_view text, dpp::snowflake guild_id, const alias_store& aliases) -> std::string {
    const auto as_mention = [&](std::string_view alias) -> std::optional<std::string> {
        const auto user = aliases.user_for(guild_id, alias);
        if (!user) return std::nullopt;
        return std::format("<@{}>", *user);
    };
    return rewrite(
        text,
        [&](std::string_view inner) -> std::optional<std::string> {
            const auto marker = split_marker(inner);
            return marker ? as_mention(marker->first) : std::nullopt;
        },
        [&](std::string_view plain) { return rewrite_bare_aliases(plain, as_mention); });
}

} // namespace latibot::llm
