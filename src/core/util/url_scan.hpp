#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::util {

/// One link in a message, as Discord would show it.
struct found_link {
    /// Byte offsets into the scanned text: `text.substr(begin, end - begin)`
    /// is the link.
    std::size_t begin = 0;
    std::size_t end = 0;

    /// The link itself, pointing into the scanned text.
    std::string_view url;

    /// Inside an open `||` spoiler, so anything posted in its place should be
    /// spoilered too (plan v4 §9.1).
    bool spoilered = false;

    /// Written as `<https://…>`, which is how somebody asks Discord for no
    /// preview. Replacing it would put back the embed they took away.
    bool embed_suppressed = false;

    /// Inside `code` or a ```block```, where Discord shows it as text and
    /// never embeds it.
    bool in_code = false;
};

/// Every http(s) link in `text`, in order.
///
/// Each link is found separately, which is the fix for the Java bot's worst
/// bug here: its pattern wrapped the URL in greedy `(.*)` groups, so the first
/// match swallowed the whole message and a second link was never seen (plan
/// v4 §9.1). Trailing punctuation that Discord leaves out of a link is left out
/// here too, so "look: https://x.com/a." does not carry the full stop along.
///
/// Built on CTRE, which compiles the pattern into ordinary code: no recursion,
/// no allocation while matching, and nothing to overflow on a long message.
[[nodiscard]] std::vector<found_link> find_links(std::string_view text);

/// A URL taken apart. Every part points into the original text.
struct url_parts {
    /// "https" or "http".
    std::string_view scheme;

    /// As written, which may include "www.", a port or credentials.
    std::string_view authority;

    /// From the first '/' after the host, or empty.
    std::string_view path;

    /// From '?', including it, or empty.
    std::string_view query;

    /// From '#', including it, or empty.
    std::string_view fragment;
};

/// Nothing when `url` is not an http(s) URL with a host.
[[nodiscard]] std::optional<url_parts> split_url(std::string_view url);

/// The host a URL rule is looked up by: lowercased, without credentials, port,
/// or a leading "www.". "https://WWW.X.com:443/a" and "https://x.com/a" are
/// the same site, and a rule written once should catch both.
[[nodiscard]] std::string rule_host(std::string_view authority);

/// The URL again, on `host`, with `path_suffix` appended to the path.
///
/// The suffix goes on the path, before any query or fragment, which is where
/// a mirror's translation switch has to be (plan v4 §9.1). A path ending in
/// '/' does not become "//en", and a path that already ends in the suffix does
/// not get it twice. The scheme is always https: every mirror worth using
/// serves it.
[[nodiscard]] std::string rehost(const url_parts& parts, std::string_view host, std::string_view path_suffix = {});

/// The path compared when deciding whether two links point at the same thing:
/// no trailing slash, so ".../status/1" and ".../status/1/" agree.
[[nodiscard]] std::string_view comparable_path(std::string_view path) noexcept;

} // namespace latibot::util
