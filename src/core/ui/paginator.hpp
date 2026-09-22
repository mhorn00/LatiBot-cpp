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
/// (plan v4 §8.2).
struct page_state {
    /// Which view this button belongs to, e.g. "nicknames" or "triggers".
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
[[nodiscard]] std::optional<std::string> encode(const page_state& state);

/// The inverse. Nothing when the text is not one of ours.
[[nodiscard]] std::optional<page_state> decode(std::string_view custom_id);

/// Total pages for `total` items at `per_page`, never less than one: an empty
/// list is one empty page, not zero pages.
[[nodiscard]] int page_count(std::size_t total, std::size_t per_page);

/// `page` brought inside [0, page_count). Out-of-range input is clamped
/// rather than rejected, since a stale button from an older, longer list is
/// ordinary rather than an error.
[[nodiscard]] int clamp_page(int page, std::size_t total, std::size_t per_page);

/// The half-open range of item indices shown on `page`.
struct page_range {
    std::size_t begin = 0;
    std::size_t end = 0;

    [[nodiscard]] std::size_t size() const noexcept { return end - begin; }
    [[nodiscard]] bool empty() const noexcept { return begin == end; }
};

[[nodiscard]] page_range range_for(int page, std::size_t total, std::size_t per_page);

/// "Page 2 of 7", or "Page 1 of 1" for an empty list.
[[nodiscard]] std::string page_label(int page, std::size_t total, std::size_t per_page);

/// The ◀ / ▶ row for a view, with the ends disabled at the ends.
///
/// Returns an empty component when there is only one page: a row of two dead
/// buttons is worse than no row.
[[nodiscard]] std::optional<dpp::component> controls(const page_state& state, std::size_t total, std::size_t per_page);

} // namespace latibot::ui
