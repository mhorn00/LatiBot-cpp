// Fuzzes the link scanner and replacement planning, which see every message
// (plan §9.1, §17.5).

#include "core/events/url_rules.hpp"
#include "core/util/url_scan.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace {

/// The `||` markers that start before `before` and are outside every code
/// span, found the slow way: each occurrence, checked against each span.
auto markers_outside_code(std::string_view text, const std::vector<latibot::util::text_span>& code, std::size_t before) -> std::size_t {
    std::size_t markers = 0;
    for (std::size_t at = text.find("||"); at != std::string_view::npos && at + 2 <= before; at = text.find("||", at + 2)) {
        const bool in_code =
            std::ranges::any_of(code, [&](const latibot::util::text_span& span) { return span.begin <= at && at < span.end; });
        if (!in_code) ++markers;
    }
    return markers;
}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    const std::vector<latibot::util::text_span> code = latibot::util::code_spans(text);

    std::size_t previous_end = 0;
    for (const latibot::util::found_link& link : latibot::util::find_links(text)) {
        // In order, inside the text, not overlapping, and exactly the text
        // the offsets describe.
        if (link.begin < previous_end || link.end > text.size() || link.begin >= link.end) std::abort();
        if (text.substr(link.begin, link.end - link.begin) != link.url) std::abort();
        if (!latibot::util::split_url(link.url)) std::abort();
        // A link is spoilered exactly when an odd number of markers outside
        // code precede it. The scanner counts incrementally between links;
        // this counts from the start, one marker at a time.
        if (link.spoilered != (markers_outside_code(text, code, link.begin) % 2 == 1)) std::abort();
        // Suppressed means written between < and >, and nothing else.
        const bool bracketed = link.begin > 0 && text[link.begin - 1] == '<' && link.end < text.size() && text[link.end] == '>';
        if (link.embed_suppressed && !bracketed) std::abort();
        previous_end = link.end;
    }

    static const std::vector<latibot::events::url_rule> rules{
        {.domain = "x.com", .mirrors = {{.host = "fxtwitter.com", .translate_suffix = "/en"}}},
    };
    for (const latibot::events::planned_link& link : latibot::events::plan_replacements(text, rules)) {
        (void)latibot::events::mirror_url(link, 0);
    }
    return 0;
}
