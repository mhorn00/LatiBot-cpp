#pragma once

#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::ui {

/// Discord's limit on a component's custom_id. Everything a button has to
/// remember rides in there, so it is worth knowing where the ceiling is.
inline constexpr std::size_t custom_id_limit = 100;

/// Where a paginated view is, encoded into a button's custom_id.
///
/// Page state lives in the custom_id rather than in memory, so paging still
/// works after a restart and no per-message state has to be kept or expired
/// (plan §8.2).
struct page_state {
    /// Which view this button belongs to, e.g. "nicks" or "triggers".
    ///
    /// A new panel names its views with a short prefix of its own, then the
    /// action: "urlpanel", "urledit", "urlyes". The older names that do not
    /// follow it ("triggers", "linkboard", "nicks") stay as they are: they are
    /// in buttons already sent, and renaming one breaks those.
    std::string view;

    /// Zero-based.
    int page = 0;

    /// Whatever the view needs to rebuild itself: a user id, a domain, a
    /// filter. Opaque here, and may be empty.
    std::string argument;
};

/// `view:page:argument`, or nothing when the result would exceed
/// `custom_id_limit` or the view name is unusable.
///
/// Returning nothing rather than a truncated id matters: Discord rejects an
/// over-long custom_id at send time, and a silently truncated one produces a
/// button that decodes to the wrong page.
[[nodiscard]] auto encode(const page_state& state) -> std::optional<std::string>;

/// The inverse. Nothing when the text is not one of ours.
[[nodiscard]] auto decode(std::string_view custom_id) -> std::optional<page_state>;

/// Total pages for `total` items at `per_page`, never less than one: an empty
/// list is one empty page, not zero pages.
[[nodiscard]] auto page_count(std::size_t total, std::size_t per_page) -> int;

/// `page` brought inside [0, page_count). Out-of-range input is clamped
/// rather than rejected, since a stale button from an older, longer list is
/// ordinary rather than an error.
[[nodiscard]] auto clamp_page(int page, std::size_t total, std::size_t per_page) -> int;

/// The half-open range of item indices shown on `page`.
struct page_range {
    std::size_t begin = 0;
    std::size_t end = 0;

    [[nodiscard]] auto size() const noexcept -> std::size_t { return end - begin; }
    [[nodiscard]] auto empty() const noexcept -> bool { return begin == end; }
};

[[nodiscard]] auto range_for(int page, std::size_t total, std::size_t per_page) -> page_range;

/// "Page 2 of 7", or "Page 1 of 1" for an empty list.
[[nodiscard]] auto page_label(int page, std::size_t total, std::size_t per_page) -> std::string;

/// The ◀ / ▶ row for a view, with the ends disabled at the ends.
///
/// Returns nothing when there is only one page: a row of two dead buttons is
/// worse than no row. Also nothing when the state will not fit in a
/// custom_id, which the caller cannot tell apart from the first case.
[[nodiscard]] auto controls(const page_state& state, std::size_t total, std::size_t per_page) -> std::optional<dpp::component>;

} // namespace latibot::ui
