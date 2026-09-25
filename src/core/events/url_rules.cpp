#include "core/events/url_rules.hpp"

#include "core/config/guild_settings.hpp"
#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "core/util/url_scan.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <utility>

namespace latibot::events {
namespace {

/// Removes a scheme a person pasted along with a host.
std::string_view without_scheme(std::string_view text) {
    if (const std::size_t separator = text.find("://"); separator != std::string_view::npos) {
        text.remove_prefix(separator + 3);
    }
    return text;
}

} // namespace

std::optional<mirror> parse_mirror(std::string_view text) {
    text = without_scheme(util::trim(text));

    const std::size_t slash = text.find('/');
    const std::string host = util::rule_host(text.substr(0, slash));
    if (host.empty()) {
        return std::nullopt;
    }

    // Whatever follows the host is the suffix, minus a trailing slash: "/en/"
    // and "/en" ask for the same thing.
    const std::string_view suffix = slash == std::string_view::npos ? std::string_view{} : util::comparable_path(text.substr(slash));
    return mirror{.host = host, .translate_suffix = std::string(suffix)};
}

std::string format_mirror(const mirror& entry) {
    return entry.host + entry.translate_suffix;
}

std::optional<std::string> normalise_domain(std::string_view text) {
    text = without_scheme(util::trim(text));
    std::string domain = util::rule_host(text.substr(0, text.find_first_of("/?#")));
    if (domain.empty()) {
        return std::nullopt;
    }
    return domain;
}

std::string_view to_string(link_decision decision) noexcept {
    switch (decision) {
    case link_decision::replaced:
        return "replaced";
    case link_decision::no_rule:
        return "no rule for this site";
    case link_decision::preview_off:
        return "written as <link>, which turns its preview off";
    case link_decision::in_code:
        return "inside code, where Discord never previews it";
    case link_decision::duplicate:
        return "the same link again";
    case link_decision::over_limit:
        return "past the limit of links per message";
    }
    return "unknown";
}

std::vector<link_verdict> explain_links(std::string_view content, std::span<const url_rule> rules) {
    std::vector<link_verdict> verdicts;
    std::size_t replaced = 0;

    for (const util::found_link& found : util::find_links(content)) {
        const auto parts = util::split_url(found.url);
        if (!parts) {
            continue;
        }

        link_verdict verdict{.decision = link_decision::replaced,
                             .link = {.original_url = std::string(found.url),
                                      .domain = util::rule_host(parts->authority),
                                      .spoilered = found.spoilered,
                                      .mirrors = {}}};

        const auto rule = std::ranges::find(rules, verdict.link.domain, &url_rule::domain);
        const bool seen_before = std::ranges::any_of(verdicts, [&](const link_verdict& earlier) {
            return earlier.decision == link_decision::replaced && earlier.link.original_url == found.url;
        });

        // In order of what the person could do about it: a link they wrote
        // to have no preview is theirs to decide, before any rule matters.
        if (found.embed_suppressed) {
            verdict.decision = link_decision::preview_off;
        } else if (found.in_code) {
            verdict.decision = link_decision::in_code;
        } else if (rule == rules.end() || rule->mirrors.empty()) {
            verdict.decision = link_decision::no_rule;
        } else if (seen_before) {
            // The same link twice is one preview, not two.
            verdict.decision = link_decision::duplicate;
        } else if (replaced == max_links_per_message) {
            verdict.decision = link_decision::over_limit;
        } else {
            verdict.link.mirrors = rule->mirrors;
            ++replaced;
        }

        verdicts.push_back(std::move(verdict));
    }

    return verdicts;
}

std::vector<planned_link> plan_replacements(std::string_view content, std::span<const url_rule> rules) {
    std::vector<planned_link> planned;
    if (rules.empty()) {
        return planned;
    }

    for (link_verdict& verdict : explain_links(content, rules)) {
        if (verdict.decision == link_decision::replaced) {
            planned.push_back(std::move(verdict.link));
        }
    }
    return planned;
}

std::string mirror_url(const planned_link& link, std::size_t index) {
    const auto parts = util::split_url(link.original_url);
    if (!parts || link.mirrors.empty()) {
        return link.original_url;
    }

    const mirror& chosen = link.mirrors[std::min(index, link.mirrors.size() - 1)];
    return util::rehost(*parts, chosen.host, chosen.translate_suffix);
}

// --------------------------------------------------------------------------

std::vector<url_rule> url_rule_store::for_guild(dpp::snowflake guild_id) const {
    std::vector<url_rule> rules;

    auto query = db_->prepare("SELECT domain, host, translate_suffix FROM url_rules WHERE guild_id = ? ORDER BY domain, position",
                              static_cast<std::uint64_t>(guild_id));
    while (query.step()) {
        auto domain = query.get<std::string>(0);
        if (rules.empty() || rules.back().domain != domain) {
            rules.push_back({.domain = std::move(domain), .mirrors = {}});
        }
        rules.back().mirrors.push_back(
            {.host = query.get<std::string>(1), .translate_suffix = query.get<std::optional<std::string>>(2).value_or(std::string{})});
    }

    return rules;
}

std::optional<url_rule> url_rule_store::find(dpp::snowflake guild_id, std::string_view domain) const {
    url_rule rule{.domain = std::string(domain), .mirrors = {}};

    auto query = db_->prepare("SELECT host, translate_suffix FROM url_rules WHERE guild_id = ? AND domain = ? ORDER BY position",
                              static_cast<std::uint64_t>(guild_id), domain);
    while (query.step()) {
        rule.mirrors.push_back(
            {.host = query.get<std::string>(0), .translate_suffix = query.get<std::optional<std::string>>(1).value_or(std::string{})});
    }

    if (rule.mirrors.empty()) {
        return std::nullopt;
    }
    return rule;
}

void url_rule_store::set(dpp::snowflake guild_id, const url_rule& rule) {
    const auto guard = db_->lock();
    db::transaction tx(*db_);
    write(guild_id, rule);
    tx.commit();
}

void url_rule_store::rename(dpp::snowflake guild_id, std::string_view previous, const url_rule& rule) {
    const auto guard = db_->lock();
    db::transaction tx(*db_);
    db_->prepare("DELETE FROM url_rules WHERE guild_id = ? AND domain = ?", static_cast<std::uint64_t>(guild_id), previous).run();
    write(guild_id, rule);
    tx.commit();
}

void url_rule_store::write(dpp::snowflake guild_id, const url_rule& rule) {
    db_->prepare("DELETE FROM url_rules WHERE guild_id = ? AND domain = ?", static_cast<std::uint64_t>(guild_id), rule.domain).run();

    int position = 0;
    for (const mirror& entry : rule.mirrors) {
        const std::optional<std::string> suffix =
            entry.translate_suffix.empty() ? std::nullopt : std::optional<std::string>(entry.translate_suffix);
        db_->prepare("INSERT INTO url_rules (guild_id, domain, position, host, translate_suffix) VALUES (?, ?, ?, ?, ?)",
                     static_cast<std::uint64_t>(guild_id), rule.domain, position++, entry.host, suffix)
            .run();
        remember_mirror(guild_id, entry.host, rule.domain);
    }
}

bool url_rule_store::remove(dpp::snowflake guild_id, std::string_view domain) {
    const auto guard = db_->lock();
    db_->prepare("DELETE FROM url_rules WHERE guild_id = ? AND domain = ?", static_cast<std::uint64_t>(guild_id), domain).run();
    return db_->changes() > 0;
}

bool url_rule_store::enabled(dpp::snowflake guild_id) const {
    return config::guild_settings(*db_).get_bool(guild_id, url_replacement_enabled_key, false);
}

void url_rule_store::set_enabled(dpp::snowflake guild_id, bool enabled) {
    config::guild_settings(*db_).set_bool(guild_id, url_replacement_enabled_key, enabled);
}

bool url_rule_store::opted_out(dpp::snowflake guild_id, dpp::snowflake user_id) const {
    auto query = db_->prepare("SELECT 1 FROM url_opt_outs WHERE guild_id = ? AND user_id = ?", static_cast<std::uint64_t>(guild_id),
                              static_cast<std::uint64_t>(user_id));
    return query.step();
}

bool url_rule_store::toggle_opt_out(dpp::snowflake guild_id, dpp::snowflake user_id) {
    const auto guard = db_->lock();

    db_->prepare("DELETE FROM url_opt_outs WHERE guild_id = ? AND user_id = ?", static_cast<std::uint64_t>(guild_id),
                 static_cast<std::uint64_t>(user_id))
        .run();
    if (db_->changes() > 0) {
        return false;
    }

    db_->prepare("INSERT INTO url_opt_outs (guild_id, user_id) VALUES (?, ?)", static_cast<std::uint64_t>(guild_id),
                 static_cast<std::uint64_t>(user_id))
        .run();
    return true;
}

mirror_map url_rule_store::known_mirrors(dpp::snowflake guild_id) const {
    mirror_map known;
    auto query = db_->prepare("SELECT host, domain FROM known_mirrors WHERE guild_id = ?", static_cast<std::uint64_t>(guild_id));
    while (query.step()) {
        known.emplace(query.get<std::string>(0), query.get<std::string>(1));
    }
    return known;
}

void url_rule_store::remember_mirror(dpp::snowflake guild_id, std::string_view host, std::string_view domain) {
    // The latest rule wins when a host moves between domains, which only
    // happens when somebody fixes a mistake.
    db_->prepare(
           "INSERT INTO known_mirrors (guild_id, host, domain) VALUES (?, ?, ?) "
           "ON CONFLICT (guild_id, host) DO UPDATE SET domain = excluded.domain",
           static_cast<std::uint64_t>(guild_id), host, domain)
        .run();
}

// --------------------------------------------------------------------------

legacy_rules parse_legacy_rules(std::string_view text) {
    legacy_rules parsed;

    // One rule per line: "domain|mirror^mirror". A line that cannot be read is
    // reported by number and skipped, so one typo does not lose the file.
    std::size_t line_number = 0;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t newline = text.find('\n', at);
        const std::string_view line =
            util::trim(text.substr(at, newline == std::string_view::npos ? std::string_view::npos : newline - at));
        at = newline == std::string_view::npos ? text.size() + 1 : newline + 1;
        ++line_number;

        if (line.empty()) {
            continue;
        }

        const std::size_t bar = line.find('|');
        if (bar == std::string_view::npos) {
            parsed.problems.push_back(std::format("line {}: expected domain|mirror^mirror, got \"{}\"", line_number, line));
            continue;
        }

        const auto domain = normalise_domain(line.substr(0, bar));
        if (!domain) {
            parsed.problems.push_back(std::format("line {}: no domain before the '|'", line_number));
            continue;
        }

        // The mirrors, in order, split on '^'. A mirror that cannot be read is
        // dropped on its own; the line fails only if none are left.
        url_rule rule{.domain = *domain, .mirrors = {}};
        std::string_view rest = line.substr(bar + 1);
        while (!rest.empty()) {
            const std::size_t caret = rest.find('^');
            if (const auto entry = parse_mirror(rest.substr(0, caret))) {
                rule.mirrors.push_back(*entry);
            }
            rest = caret == std::string_view::npos ? std::string_view{} : rest.substr(caret + 1);
        }

        if (rule.mirrors.empty()) {
            parsed.problems.push_back(std::format("line {}: {} has no mirrors", line_number, rule.domain));
            continue;
        }

        // A domain listed twice: the later line wins, as it did in the Java
        // bot, which read the file into a map.
        std::erase_if(parsed.rules, [&](const url_rule& earlier) { return earlier.domain == rule.domain; });
        parsed.rules.push_back(std::move(rule));
    }

    return parsed;
}

std::optional<int> import_url_rules_file(url_rule_store& store, dpp::snowflake guild_id, const std::filesystem::path& file) {
    const std::ifstream input(file);
    if (!input) {
        return std::nullopt;
    }

    std::ostringstream contents;
    contents << input.rdbuf();

    const legacy_rules parsed = parse_legacy_rules(contents.str());
    for (const std::string& problem : parsed.problems) {
        util::log().warn("{}: {}", file.generic_string(), problem);
    }

    int added = 0;
    for (const url_rule& rule : parsed.rules) {
        if (store.find(guild_id, rule.domain)) {
            continue;
        }
        store.set(guild_id, rule);
        ++added;
    }
    return added;
}

} // namespace latibot::events
