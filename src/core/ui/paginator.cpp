#include "core/ui/paginator.hpp"

#include <algorithm>
#include <charconv>
#include <format>

namespace latibot::ui {
namespace {

constexpr char separator = ':';

/// The view name and the page number are read back by splitting on ':', so
/// neither may contain one. The argument may: it is whatever is left.
auto usable_view_name(std::string_view view) -> bool {
    return !view.empty() && view.find(separator) == std::string_view::npos;
}

} // namespace

auto encode(const page_state& state) -> std::optional<std::string> {
    if (!usable_view_name(state.view) || state.page < 0) {
        return std::nullopt;
    }

    std::string id = std::format("{}{}{}{}{}", state.view, separator, state.page, separator, state.argument);
    if (id.size() > custom_id_limit) {
        return std::nullopt;
    }
    return id;
}

auto decode(std::string_view custom_id) -> std::optional<page_state> {
    const std::size_t first = custom_id.find(separator);
    if (first == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t second = custom_id.find(separator, first + 1);
    if (second == std::string_view::npos) {
        return std::nullopt;
    }

    const std::string_view view = custom_id.substr(0, first);
    const std::string_view page_text = custom_id.substr(first + 1, second - first - 1);
    if (!usable_view_name(view) || page_text.empty()) {
        return std::nullopt;
    }

    int page = 0;
    const char* begin = page_text.data();
    const char* end = begin + page_text.size();
    const auto [stop, error] = std::from_chars(begin, end, page);
    if (error != std::errc{} || stop != end || page < 0) {
        return std::nullopt;
    }

    return page_state{.view = std::string(view), .page = page, .argument = std::string(custom_id.substr(second + 1))};
}

auto page_count(std::size_t total, std::size_t per_page) -> int {
    if (per_page == 0 || total == 0) {
        return 1;
    }
    return static_cast<int>((total + per_page - 1) / per_page);
}

auto clamp_page(int page, std::size_t total, std::size_t per_page) -> int {
    return std::clamp(page, 0, page_count(total, per_page) - 1);
}

auto range_for(int page, std::size_t total, std::size_t per_page) -> page_range {
    if (per_page == 0) {
        return {.begin = 0, .end = total};
    }

    const auto first = static_cast<std::size_t>(clamp_page(page, total, per_page)) * per_page;
    if (first >= total) {
        return {.begin = total, .end = total};
    }
    return {.begin = first, .end = std::min(first + per_page, total)};
}

auto page_label(int page, std::size_t total, std::size_t per_page) -> std::string {
    const int pages = page_count(total, per_page);
    return std::format("Page {} of {}", clamp_page(page, total, per_page) + 1, pages);
}

auto controls(const page_state& state, std::size_t total, std::size_t per_page) -> std::optional<dpp::component> {
    const int pages = page_count(total, per_page);
    if (pages <= 1) {
        return std::nullopt;
    }

    const int current = clamp_page(state.page, total, per_page);
    const auto previous = encode({.view = state.view, .page = current - 1 < 0 ? 0 : current - 1, .argument = state.argument});
    const auto next = encode({.view = state.view, .page = std::min(current + 1, pages - 1), .argument = state.argument});
    if (!previous || !next) {
        return std::nullopt;
    }

    dpp::component row;
    row.set_type(dpp::cot_action_row);
    row.add_component(dpp::component()
                          .set_type(dpp::cot_button)
                          .set_style(dpp::cos_secondary)
                          .set_label("◀")
                          .set_id(*previous)
                          .set_disabled(current == 0));
    row.add_component(dpp::component()
                          .set_type(dpp::cot_button)
                          .set_style(dpp::cos_secondary)
                          .set_label("▶")
                          .set_id(*next)
                          .set_disabled(current >= pages - 1));
    return row;
}

} // namespace latibot::ui
