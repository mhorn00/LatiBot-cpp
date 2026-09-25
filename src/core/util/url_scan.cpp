#include "core/util/url_scan.hpp"

#include "core/util/text.hpp"

#include <ctre.hpp>

#include <algorithm>
#include <cctype>
#include <span>

namespace latibot::util {
namespace {

/// A scheme, then everything up to the first character that cannot be part of
/// a link in a Discord message. `<` and `>` bracket a link whose preview was
/// turned off, `|` is a spoiler marker, and a backtick ends a code span.
constexpr ctll::fixed_string link_pattern = R"(https?://[^\s<>|`"]+)";

/// Punctuation Discord leaves off the end of a link, so "see https://x.com/a."
/// links to "https://x.com/a". Asterisks and tildes are there for the bold and
/// strikethrough markers that close around a link.
constexpr std::string_view trailing_punctuation = ".,:;!?'*~";

/// The `||` spoiler markers in `text[from, to)` that are outside code.
///
/// `next` is an index into `code` that only moves forward, as the ranges
/// asked about do, which keeps a message full of links linear.
std::size_t count_markers(std::string_view text, std::size_t from, std::size_t to, std::span<const text_span> code, std::size_t& next) {
    std::size_t markers = 0;
    while (from < to) {
        // Spans that ended before here are behind us for good.
        while (next < code.size() && code[next].end <= from) {
            ++next;
        }

        // Inside a span, nothing counts until it ends.
        if (next < code.size() && code[next].begin <= from) {
            from = code[next].end;
            continue;
        }

        // Outside, count up to the next span or the end of the range. A marker
        // cannot straddle into a span, which starts with a backtick.
        const std::size_t stop = next < code.size() ? std::min(to, code[next].begin) : to;
        markers += count_occurrences(text.substr(from, stop - from), "||");
        from = stop;
    }
    return markers;
}

/// Drops what Discord would not count as part of the link.
///
/// A closing bracket stays when the link opened one itself, so a Wikipedia
/// link ending "(film)" survives while "(see https://x.com/a)" loses the ")".
/// The counts are kept as it goes rather than recounted per character, so a
/// link followed by thousands of brackets is still linear.
std::string_view trim_link(std::string_view url) {
    const auto count = [&](char bracket) { return static_cast<std::size_t>(std::ranges::count(url, bracket)); };
    const std::size_t open_round = count('(');
    std::size_t close_round = count(')');
    const std::size_t open_square = count('[');
    std::size_t close_square = count(']');

    while (!url.empty()) {
        const char last = url.back();
        if (trailing_punctuation.find(last) != std::string_view::npos) {
            url.remove_suffix(1);
        } else if (last == ')' && close_round > open_round) {
            url.remove_suffix(1);
            --close_round;
        } else if (last == ']' && close_square > open_square) {
            url.remove_suffix(1);
            --close_square;
        } else {
            break;
        }
    }
    return url;
}

bool equals_ignoring_case(std::string_view lhs, std::string_view rhs) {
    return std::ranges::equal(lhs, rhs, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

} // namespace

std::vector<text_span> code_spans(std::string_view text) {
    std::vector<text_span> spans;

    std::size_t at = 0;
    while (at < text.size()) {
        // Find the next run of backticks. A run that ends the text opens
        // nothing, since there is nothing left for it to close over.
        at = text.find('`', at);
        if (at == std::string_view::npos) {
            break;
        }

        const std::size_t opening_end = text.find_first_not_of('`', at);
        if (opening_end == std::string_view::npos) {
            break;
        }
        const std::size_t width = opening_end - at;

        // Look for a later run of exactly the same width. Runs of any other
        // width are skipped as literal backticks inside the span.
        bool closed = false;
        for (std::size_t search = text.find('`', opening_end); search != std::string_view::npos;) {
            const std::size_t closing_end = std::min(text.find_first_not_of('`', search), text.size());
            if (closing_end - search == width) {
                spans.push_back({.begin = at, .end = closing_end});
                at = closing_end;
                closed = true;
                break;
            }
            search = closing_end < text.size() ? text.find('`', closing_end) : std::string_view::npos;
        }

        // Unclosed: the run was literal text, so carry on from just after
        // it, and a later run can still open a span of its own.
        if (!closed) {
            at = opening_end;
        }
    }

    return spans;
}

std::vector<found_link> find_links(std::string_view text) {
    std::vector<found_link> links;

    const std::vector<text_span> code = code_spans(text);
    auto next_code = code.begin();

    // Spoiler markers are counted incrementally between links rather than
    // from the start for each one, which keeps a message full of links
    // linear. Markers never straddle a link, since a link cannot contain '|'.
    // Those inside code are text to Discord, and skipped, with a second walk
    // over the code spans.
    std::size_t markers = 0;
    std::size_t counted_to = 0;
    std::size_t marker_code = 0;

    for (const auto& match : ctre::search_all<link_pattern, ctre::case_insensitive>(text)) {
        const std::string_view raw = match.to_view();
        const auto begin = static_cast<std::size_t>(raw.data() - text.data());

        // "foohttps://…" is not a link to Discord, and not one here either.
        if (begin > 0 && std::isalnum(static_cast<unsigned char>(text[begin - 1])) != 0) {
            continue;
        }

        // Written as <link>, which is how somebody asks Discord for no
        // preview. Discord takes everything between the brackets as the link,
        // trailing punctuation included, so this is judged on the match as
        // found: trimming first would move the end off the '>'.
        const std::size_t raw_end = begin + raw.size();
        const bool suppressed = begin > 0 && text[begin - 1] == '<' && raw_end < text.size() && text[raw_end] == '>';

        // Otherwise trim what Discord leaves off the end. Either way, make
        // sure what is left has a host: "https://" on its own is not a link.
        const std::string_view url = suppressed ? raw : trim_link(raw);
        const std::size_t end = begin + url.size();
        if (!split_url(url)) {
            continue;
        }

        // Count the markers between the previous link and this one only.
        markers += count_markers(text, counted_to, begin, code, marker_code);
        counted_to = end;

        // Links arrive in order, so the code spans are walked alongside them:
        // skip the spans that ended before this link, and the next one, if it
        // started before the link, contains it.
        while (next_code != code.end() && next_code->end <= begin) {
            ++next_code;
        }

        links.push_back({.begin = begin,
                         .end = end,
                         .url = url,
                         .spoilered = markers % 2 == 1,
                         .embed_suppressed = suppressed,
                         .in_code = next_code != code.end() && next_code->begin < begin});
    }

    return links;
}

std::optional<url_parts> split_url(std::string_view url) {
    const std::size_t separator = url.find("://");
    if (separator == std::string_view::npos) {
        return std::nullopt;
    }

    const std::string_view scheme = url.substr(0, separator);
    if (!equals_ignoring_case(scheme, "https") && !equals_ignoring_case(scheme, "http")) {
        return std::nullopt;
    }

    const std::string_view rest = url.substr(separator + 3);
    const std::size_t authority_end = rest.find_first_of("/?#");
    const std::string_view authority = rest.substr(0, authority_end);
    if (authority.empty()) {
        return std::nullopt;
    }

    url_parts parts{.scheme = scheme, .authority = authority, .path = {}, .query = {}, .fragment = {}};
    if (authority_end == std::string_view::npos) {
        return parts;
    }

    const std::string_view remainder = rest.substr(authority_end);
    const std::size_t path_end = remainder.find_first_of("?#");
    parts.path = remainder.substr(0, path_end);
    if (path_end == std::string_view::npos) {
        return parts;
    }

    const std::string_view after = remainder.substr(path_end);
    if (after.front() == '?') {
        const std::size_t hash = after.find('#');
        parts.query = after.substr(0, hash);
        parts.fragment = hash == std::string_view::npos ? std::string_view{} : after.substr(hash);
    } else {
        parts.fragment = after;
    }
    return parts;
}

std::string rule_host(std::string_view authority) {
    // Credentials first, since they may contain ':' themselves.
    if (const std::size_t at = authority.rfind('@'); at != std::string_view::npos) {
        authority.remove_prefix(at + 1);
    }

    // A port is ':' then digits at the end; an IPv6 literal has colons of its
    // own inside the brackets, which this leaves alone.
    if (const std::size_t colon = authority.rfind(':');
        colon != std::string_view::npos && authority.find(']', colon) == std::string_view::npos) {
        authority = authority.substr(0, colon);
    }

    std::string host(authority);
    std::ranges::transform(host, host.begin(), [](unsigned char letter) { return static_cast<char>(std::tolower(letter)); });

    while (host.ends_with('.')) {
        host.pop_back();
    }
    if (host.starts_with("www.")) {
        host.erase(0, 4);
    }
    return host;
}

std::string rehost(const url_parts& parts, std::string_view host, std::string_view path_suffix) {
    std::string rebuilt = "https://";
    rebuilt += host;

    if (path_suffix.empty()) {
        rebuilt += parts.path;
    } else {
        std::string path(parts.path);
        while (path.ends_with('/')) {
            path.pop_back();
        }

        std::string suffix(path_suffix);
        if (!suffix.starts_with('/')) {
            suffix.insert(suffix.begin(), '/');
        }
        if (!path.ends_with(suffix)) {
            path += suffix;
        }
        rebuilt += path;
    }

    rebuilt += parts.query;
    rebuilt += parts.fragment;
    return rebuilt;
}

std::string_view comparable_path(std::string_view path) noexcept {
    while (path.ends_with('/')) {
        path.remove_suffix(1);
    }
    return path;
}

} // namespace latibot::util
