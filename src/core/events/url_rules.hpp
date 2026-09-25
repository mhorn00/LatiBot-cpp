#pragma once

#include <dpp/snowflake.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::db {
class database;
}

namespace latibot::events {

/// Per-guild setting saying whether URL replacement is on. Absent means off.
inline constexpr std::string_view url_replacement_enabled_key = "url_replacement_enabled";

/// One site a link can be sent to instead of the original (plan §9).
struct mirror {
    /// "fxtwitter.com".
    std::string host;

    /// Appended to the path when set, for mirrors that translate a post when
    /// asked to: "/en". Empty for none.
    std::string translate_suffix;

    friend bool operator==(const mirror&, const mirror&) = default;
};

/// Where links to one site go instead, in the order to try them.
struct url_rule {
    /// The host the rule catches, as `util::rule_host` spells it: lowercase,
    /// no "www.".
    std::string domain;
    std::vector<mirror> mirrors;
};

/// "fxtwitter.com/en" as a mirror: everything after the host is the
/// translation suffix. A pasted scheme or trailing slash is tolerated, since
/// people copy these out of a browser. Nothing when there is no host.
[[nodiscard]] std::optional<mirror> parse_mirror(std::string_view text);

/// The inverse: "fxtwitter.com/en", or just the host.
[[nodiscard]] std::string format_mirror(const mirror& entry);

/// A domain as a person typed it, reduced to what a rule is keyed by:
/// "https://www.X.com/" is "x.com". Nothing when no host is left.
[[nodiscard]] std::optional<std::string> normalise_domain(std::string_view text);

/// A link a rule applies to, with everything needed to post it elsewhere.
struct planned_link {
    /// As the author wrote it.
    std::string original_url;
    std::string domain;

    /// Inside an open spoiler, so its replacement is spoilered too.
    bool spoilered = false;

    /// Copied from the rule when the message arrived, so editing a rule does
    /// not change a replacement that is already being tried.
    std::vector<mirror> mirrors;
};

/// The most links one replacement carries. Anything past it is left alone:
/// a message that is mostly links is somebody pasting a list, and a wall of
/// previews would bury the conversation.
inline constexpr std::size_t max_links_per_message = 5;

/// What happened to one link, and why.
enum class link_decision : std::uint8_t {
    replaced,
    no_rule,
    preview_off,
    in_code,
    duplicate,
    over_limit,
};

/// Why, in words, for `/urlrepl test`.
[[nodiscard]] std::string_view to_string(link_decision decision) noexcept;

struct link_verdict {
    link_decision decision = link_decision::replaced;

    /// Mirrors are filled in only for a replaced link.
    planned_link link;
};

/// Every link in `content`, with what URL replacement makes of it.
///
/// `plan_replacements` is this, filtered, so the dry run in `/urlrepl test`
/// cannot disagree with what a real message gets.
[[nodiscard]] std::vector<link_verdict> explain_links(std::string_view content, std::span<const url_rule> rules);

/// The links in `content` that a rule covers, in the order they appear, each
/// once.
///
/// A link to a site without a rule is skipped on its own. The Java bot gave up
/// on the whole message instead, so one unrelated link stopped every other
/// replacement (plan §9.1). Links in code, and links written as `<…>` to
/// turn their preview off, are left alone as well: Discord was not going to
/// embed those anyway.
[[nodiscard]] std::vector<planned_link> plan_replacements(std::string_view content, std::span<const url_rule> rules);

/// `link` on its mirror at `index`. An index past the last mirror uses the
/// last one, so a caller counting attempts cannot fall off the end.
[[nodiscard]] std::string mirror_url(const planned_link& link, std::size_t index);

// --------------------------------------------------------------------------
// Storage
// --------------------------------------------------------------------------

/// Host to domain: which site a mirror stood in for.
using mirror_map = std::map<std::string, std::string, std::less<>>;

/// URL rules, per-user opt-outs, and every mirror a guild has ever used.
class url_rule_store {
public:
    explicit url_rule_store(db::database& db) : db_(&db) {}

    [[nodiscard]] std::vector<url_rule> for_guild(dpp::snowflake guild_id) const;
    [[nodiscard]] std::optional<url_rule> find(dpp::snowflake guild_id, std::string_view domain) const;

    /// Replaces a domain's mirrors wholesale, which is how both the command
    /// and the panel edit them: the order is the list.
    void set(dpp::snowflake guild_id, const url_rule& rule);

    /// Replaces the rule for `previous` with `rule`, for another site, in one
    /// transaction: a rename that fails leaves the old rule in place rather
    /// than no rule at all.
    void rename(dpp::snowflake guild_id, std::string_view previous, const url_rule& rule);

    /// False when there was no such rule.
    bool remove(dpp::snowflake guild_id, std::string_view domain);

    /// Whether links are being replaced in this guild at all. Off until
    /// somebody with Manage Server turns it on, so a server that invited the
    /// bot for something else does not find its links rewritten; the rules
    /// can be written, imported and dry-run before then.
    [[nodiscard]] bool enabled(dpp::snowflake guild_id) const;

    /// Kept in `guild_settings`, so it outlasts a restart.
    void set_enabled(dpp::snowflake guild_id, bool enabled);

    /// Whether this member asked for their links to be left alone.
    [[nodiscard]] bool opted_out(dpp::snowflake guild_id, dpp::snowflake user_id) const;

    /// Flips a member's opt-out and says where it ended up: true for opted
    /// out. Stored, unlike the Java `/toggle`, which forgot on restart.
    bool toggle_opt_out(dpp::snowflake guild_id, dpp::snowflake user_id);

    /// Every mirror host this guild has had a rule for, including rules since
    /// removed. Recognising the bot's old replacements depends on it, and they
    /// do not stop existing when a rule changes (plan §9.7).
    [[nodiscard]] mirror_map known_mirrors(dpp::snowflake guild_id) const;

    /// Remembers a mirror without making a rule of it, for hosts the bot used
    /// before this database existed.
    void remember_mirror(dpp::snowflake guild_id, std::string_view host, std::string_view domain);

private:
    /// What `set` and `rename` share, for a caller already in a transaction.
    void write(dpp::snowflake guild_id, const url_rule& rule);

    db::database* db_;
};

// --------------------------------------------------------------------------
// Importing UrlReplacements.txt
// --------------------------------------------------------------------------

/// What reading the Java bot's rule file produced.
struct legacy_rules {
    std::vector<url_rule> rules;

    /// Lines that could not be read, with their line numbers, so a bad line
    /// is named rather than dropped quietly.
    std::vector<std::string> problems;
};

/// Parses `domain|mirror^mirror^…`, one rule per line. A domain listed twice
/// keeps its last line, which is what the Java bot's map did.
[[nodiscard]] legacy_rules parse_legacy_rules(std::string_view text);

/// Copies the Java bot's rules into a guild.
///
/// Nothing when the file does not exist, which is the ordinary case on a
/// fresh install. Returns the number of rules written; a rule the guild
/// already has is left as it is, so importing twice changes nothing.
std::optional<int> import_url_rules_file(url_rule_store& store, dpp::snowflake guild_id, const std::filesystem::path& file);

} // namespace latibot::events
